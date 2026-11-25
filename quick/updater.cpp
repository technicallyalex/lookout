// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.hpp"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <algorithm>

namespace {
constexpr qint64 maxInstaller=256*1024*1024;
constexpr qint64 maxMetadata=4*1024*1024;
const QString releaseRoot="https://github.com/technicallyalex/lookout/releases/download/";
QRegularExpressionMatch versionParts(const QString &version){
 static const QRegularExpression pattern(
  "^v?((?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*))"
  "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$");
 const auto match=pattern.match(version);
 if(match.hasMatch())for(const auto &id:match.captured(2).split('.')){
  bool numeric=!id.isEmpty();for(const auto c:id)numeric&=c.isDigit();
  if(numeric && id.size()>1 && id.startsWith('0'))return {};
 }
 return match;
}
QNetworkRequest request(const QUrl &url){
 QNetworkRequest result(url);
 result.setRawHeader("User-Agent","lookout-updater");
 result.setRawHeader("Accept","application/vnd.github+json");
 result.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
 result.setTransferTimeout(60000);
 return result;
}
}

LookoutUpdater::LookoutUpdater(QObject *parent):QObject(parent),network_(this),
 downloadDirectory_(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/lookout/updates"),
 launchInstaller_([](const QString &file,const QStringList &args,const QString &directory){
  return QProcess::startDetached(file,args,directory);
 }){}
void LookoutUpdater::configure(const QString &version,quint32 obsPid){
 if(installed_==version && obsPid_==obsPid)return;
 installed_=version;obsPid_=obsPid;emit changed();
}
QVariantMap LookoutUpdater::state() const {
 return {{"status",status_},{"busy",busy_},{"queued",queued_},{"progress",progress_},
  {"version",release_.version},{"notes",release_.notes},
  {"canCheck",!installed_.isEmpty() && !busy_ && !queued_},
  {"canInstall",!release_.version.isEmpty() && !busy_ && !queued_ && obsPid_!=0}};
}
int LookoutUpdater::compareVersions(const QString &left,const QString &right){
 const auto a=versionParts(left),b=versionParts(right);
 if(!a.hasMatch() || !b.hasMatch())return 0;
 const auto coreA=a.captured(1).split('.'),coreB=b.captured(1).split('.');
 for(int i=0;i<3;++i){
  if(coreA[i].size()!=coreB[i].size())return coreA[i].size()<coreB[i].size()?-1:1;
  const int order=QString::compare(coreA[i],coreB[i]);if(order)return order;
 }
 const QString preA=a.captured(2),preB=b.captured(2);
 if(preA==preB)return 0;
 if(preA.isEmpty())return 1;
 if(preB.isEmpty())return -1;
 const auto idsA=preA.split('.'),idsB=preB.split('.');
 static const QRegularExpression numeric("^[0-9]+$");
 for(int i=0;i<std::min(idsA.size(),idsB.size());++i){
  const auto &x=idsA[i],&y=idsB[i];
  if(x==y)continue;
  const bool nx=numeric.match(x).hasMatch(),ny=numeric.match(y).hasMatch();
  if(nx!=ny)return nx?-1:1;
  if(nx && x.size()!=y.size())return x.size()<y.size()?-1:1;
  return QString::compare(x,y,Qt::CaseSensitive);
 }
 return idsA.size()<idsB.size()?-1:1;
}
LookoutUpdater::Release LookoutUpdater::selectRelease(const QJsonArray &releases,const QString &installed){
 Release best;
 if(!versionParts(installed).hasMatch())return best;
 static const QRegularExpression digestPattern("^sha256:([0-9a-fA-F]{64})$");
 for(const auto &entry:releases){
  const auto release=entry.toObject();const QString tag=release["tag_name"].toString();
  if(release["draft"].toBool() || !versionParts(tag).hasMatch() || compareVersions(tag,installed)<=0 ||
     (!best.version.isEmpty() && compareVersions(tag,best.version)<=0))continue;
  const QString version=tag.startsWith('v')?tag.mid(1):tag;
  const QString name="install-lookout-"+version+".exe";
  for(const auto &value:release["assets"].toArray()){
   const auto asset=value.toObject();const QString url=asset["browser_download_url"].toString();
   const auto digest=digestPattern.match(asset["digest"].toString());
   const double size=asset["size"].toDouble();
   if(asset["name"]!=name || url!=releaseRoot+tag+"/"+name || !digest.hasMatch() ||
      size<=0 || size>maxInstaller || qint64(size)!=size)continue;
   best={version,release["body"].toString().left(32768),url,digest.captured(1).toLower(),qint64(size)};
   break;
  }
 }
 return best;
}
void LookoutUpdater::fail(const QString &message){
 download_.reset();reply_=nullptr;busy_=false;progress_=-1;status_=message;emit changed();
}
void LookoutUpdater::check(){
 if(installed_.isEmpty() || busy_ || queued_)return;
 busy_=true;release_={};progress_=-1;status_="Checking GitHub…";emit changed();
 auto *reply=network_.get(request(QUrl("https://api.github.com/repos/technicallyalex/lookout/releases?per_page=100")));
 reply_=reply;reply->setReadBufferSize(maxMetadata+1);
 const auto bytes=std::make_shared<QByteArray>();
 connect(reply,&QNetworkReply::readyRead,this,[reply,bytes]{
  bytes->append(reply->readAll());if(bytes->size()>maxMetadata)reply->abort();
 });
 connect(reply,&QNetworkReply::finished,this,[this,reply,bytes]{
  reply->deleteLater();bytes->append(reply->readAll());
  if(bytes->size()>maxMetadata){fail("GitHub's release list is too large. Open GitHub Releases instead.");return;}
  if(reply->error()!=QNetworkReply::NoError){fail("Could not check GitHub. "+reply->errorString());return;}
  QJsonParseError error;const auto document=QJsonDocument::fromJson(*bytes,&error);
  if(error.error!=QJsonParseError::NoError || !document.isArray()){fail("GitHub returned an invalid release list.");return;}
  release_=selectRelease(document.array(),installed_);reply_=nullptr;busy_=false;
  status_=release_.version.isEmpty()?"lookout is up to date.":"lookout "+release_.version+" is available.";
  emit changed();
 });
}
void LookoutUpdater::consumeDownload(){
 if(!reply_ || !download_ || !downloadError_.isEmpty())return;
 const auto bytes=reply_->readAll();received_+=bytes.size();
 if(received_>release_.size || download_->write(bytes)!=bytes.size()){
  downloadError_="The update could not be saved. Check available disk space and try again.";
  reply_->abort();return;
 }
 hash_.addData(bytes);
 const int progress=int(received_*100/release_.size);
 if(progress!=progress_){progress_=progress;emit changed();}
}
void LookoutUpdater::install(){
 if(busy_ || queued_ || release_.version.isEmpty() || !obsPid_)return;
 const QString directory=downloadDirectory_;
 if(!QDir().mkpath(directory)){fail("Could not create the lookout update folder.");return;}
 const QString path=directory+"/lookout-update.exe";
 download_=std::make_unique<QSaveFile>(path);
 if(!download_->open(QIODevice::WriteOnly)){fail("Could not save the update. Another installer may still be running.");return;}
 hash_.reset();received_=0;downloadError_.clear();busy_=true;progress_=0;
 status_="Downloading lookout "+release_.version+"…";emit changed();
 auto *reply=network_.get(request(QUrl(release_.url)));reply_=reply;
 reply->setReadBufferSize(1024*1024);
 connect(reply,&QNetworkReply::readyRead,this,&LookoutUpdater::consumeDownload);
 connect(reply,&QNetworkReply::finished,this,[this,reply,path,directory]{
  reply->deleteLater();consumeDownload();
  if(!downloadError_.isEmpty()){fail(downloadError_);return;}
  if(reply->error()!=QNetworkReply::NoError){fail("Download failed. "+reply->errorString());return;}
  if(received_!=release_.size || QString::fromLatin1(hash_.result().toHex())!=release_.digest){
   fail("The update failed verification. Check for updates and try again.");return;
  }
  if(!download_->commit()){fail("Could not finish saving the update. Close any other lookout installer and try again.");return;}
  download_.reset();reply_=nullptr;
  if(!launchInstaller_(path,{"--wait-for-obs",QString::number(obsPid_)},directory)){
   fail("Could not start the update installer. Try downloading the release from GitHub.");return;
  }
  busy_=false;queued_=true;progress_=100;
  status_="Update verified. Close OBS when your session is safe to stop. The installer will finish, then prompt you to reopen OBS.";
  emit changed();
 });
}
