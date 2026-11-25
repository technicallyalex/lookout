// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <memory>
#include <functional>

class LookoutUpdater final: public QObject {
 friend struct LookoutUpdateTestAccess;
 Q_OBJECT
 Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
 struct Release {
  QString version,notes,url,digest;
  qint64 size=0;
 };
 explicit LookoutUpdater(QObject *parent=nullptr);
 void configure(const QString &version,quint32 obsPid);
 QVariantMap state() const;
 Q_INVOKABLE void check();
 Q_INVOKABLE void install();
 static int compareVersions(const QString &left,const QString &right);
 static Release selectRelease(const QJsonArray &releases,const QString &installed);
signals:
 void changed();
private:
 QNetworkAccessManager network_;
 QPointer<QNetworkReply> reply_;
 QString installed_,status_,downloadError_;
 Release release_;
 bool busy_=false,queued_=false;
 int progress_=-1;
 quint32 obsPid_=0;
 QString downloadDirectory_;
 std::function<bool(const QString&,const QStringList&,const QString&)> launchInstaller_;
 qint64 received_=0;
 std::unique_ptr<QSaveFile> download_;
 QCryptographicHash hash_{QCryptographicHash::Sha256};
 void fail(const QString &message);
 void consumeDownload();
};
