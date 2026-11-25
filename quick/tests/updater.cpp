// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>

void check(bool condition,const char *message){
 if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
QJsonObject release(const QString &tag){
 const QString name="install-lookout-"+tag.mid(1)+".exe";
 return {{"tag_name",tag},{"draft",false},{"body","Patch notes"},
  {"assets",QJsonArray{QJsonObject{{"name",name},{"size",123456},
   {"digest","sha256:"+QString(64,'a')},
   {"browser_download_url","https://github.com/technicallyalex/lookout/releases/download/"+tag+"/"+name}}}}};
}
struct LookoutUpdateTestAccess {
 static void downloads(){
  QTemporaryDir directory;check(directory.isValid(),"isolated updater download directory");
  QTcpServer server;check(server.listen(QHostAddress::LocalHost),"start local download fixture");
  QByteArray payload="verified installer fixture";
  QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
   auto *socket=server.nextPendingConnection();
   QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
    socket->readAll();socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\nConnection: close\r\n\r\n"+payload);
    socket->disconnectFromHost();
   });
   QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  });
  LookoutUpdater updater;updater.configure("1.0.0-rc.2",1234);
  updater.downloadDirectory_=directory.path();
  updater.release_={"1.0.0-rc.3","Notes","http://127.0.0.1:"+QString::number(server.serverPort())+"/installer.exe",
   QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()),payload.size()};
  int launches=0;
  updater.launchInstaller_=[&](const QString &file,const QStringList &args,const QString &){
   ++launches;QFile saved(file);check(saved.open(QIODevice::ReadOnly) && saved.readAll()==payload,"installer bytes are verified before execution");
   check(args==QStringList{"--wait-for-obs","1234"},"installation waits for the exact OBS process");return true;
  };
  auto finish=[&]{
   QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
   QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
   QObject::connect(&updater,&LookoutUpdater::changed,&loop,[&]{if(!updater.busy_)loop.quit();});
   timeout.start(3000);loop.exec();check(!updater.busy_,"download completed within fixture timeout");
  };
  updater.release_.digest=QString(64,'0');updater.install();finish();
  check(launches==0 && !QFile::exists(directory.filePath("lookout-update.exe")),"digest mismatch cannot commit or launch an installer");
  updater.release_.digest=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
  updater.release_.size=payload.size()+1;updater.install();finish();
  check(launches==0,"truncated downloads cannot launch an installer");
  updater.release_.size=payload.size();updater.install();updater.install();finish();
  if(launches!=1 || !updater.queued_)std::fprintf(stderr,"Download status: %s; launches=%d\n",qPrintable(updater.status_),launches);
  check(launches==1 && updater.queued_ && updater.state()["progress"].toInt()==100,"verified download queues exactly one installation");
  updater.install();updater.check();check(launches==1 && !updater.busy_,"queued updates cannot start duplicate downloads");
 }
};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 const auto args=app.arguments();
 if(args.contains("--hold")){QThread::sleep(3);return 0;}
 if(args.contains("--live")){
  LookoutUpdater updater;updater.configure(args.value(args.indexOf("--live")+1,"1.0.0-rc.3"),0);
  QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
  QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
  QObject::connect(&updater,&LookoutUpdater::changed,&loop,[&]{if(!updater.state()["busy"].toBool())loop.quit();});
  timeout.start(65000);updater.check();loop.exec();
  const auto state=updater.state();std::printf("%s\n",qPrintable(state["status"].toString()));
  check(!state["busy"].toBool() && (state["status"]=="lookout is up to date." || !state["version"].toString().isEmpty()),"live HTTPS GitHub check");
  return 0;
 }
 const QStringList versions{"0.9.0","1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta",
  "1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.2","1.0.0-rc.3","1.0.0-rc.10","1.0.0","1.0.1","2.0.0"};
 for(int i=0;i<versions.size();++i)for(int j=0;j<versions.size();++j){
  const int compared=LookoutUpdater::compareVersions(versions[i],versions[j]);
  check(i==j?compared==0:i<j?compared<0:compared>0,"semantic version ordering including RCs and stable releases");
 }
 check(LookoutUpdater::compareVersions("v1.0.0-rc.3+build","1.0.0-rc.3")==0,"build metadata does not create a newer release");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.3"),release("v1.0.0-rc.2")},"1.0.0-rc.2").version=="1.0.0-rc.3","RC releases are available to the updater");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.3"),release("v1.0.0-rc.4")},"1.0.0-rc.3").version=="1.0.0-rc.4","RC3 discovers the RC4 installer");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.4")},"1.0.0-rc.4").version.isEmpty(),"RC4 does not offer itself as an update");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.3")},"1.0.0").version.isEmpty(),"stable users cannot downgrade to an RC");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.3"),release("v1.0.0"),release("v1.1.0")},"1.0.0-rc.3").version=="1.1.0","release selection does not depend on API ordering");
 check(LookoutUpdater::selectRelease({release("v1.0.0-rc.03")},"1.0.0-rc.2").version.isEmpty(),"invalid prerelease numbers are ignored");
 auto draft=release("v2.0.0");draft["draft"]=true;
 check(LookoutUpdater::selectRelease({draft},"1.0.0").version.isEmpty(),"drafts are ignored");
 auto invalid=release("v2.0.0");auto asset=invalid["assets"].toArray().first().toObject();
 asset["browser_download_url"]="https://example.com/update.exe";invalid["assets"]=QJsonArray{asset};
 check(LookoutUpdater::selectRelease({invalid},"1.0.0").version.isEmpty(),"external installer URLs are rejected");
 asset["browser_download_url"]=release("v2.0.0")["assets"].toArray().first().toObject()["browser_download_url"];
 asset["digest"]="";invalid["assets"]=QJsonArray{asset};
 check(LookoutUpdater::selectRelease({invalid},"1.0.0").version.isEmpty(),"unverifiable installers are rejected");
 LookoutUpdateTestAccess::downloads();
 std::puts("PASS: update version ordering, release selection and official asset validation");
}
