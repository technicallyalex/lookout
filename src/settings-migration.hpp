// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

namespace LookoutMigration {
inline constexpr char legacyModule[] = "obs-sentinel";

inline QString previousSettingsPath(const QString &currentFile)
{
 return QFileInfo(currentFile).dir().absoluteFilePath("../obs-sentinel/sentinel.ini");
}

inline bool importSettings(const QString &currentFile)
{
 if(QFileInfo::exists(currentFile))return true;
 QFile previous(previousSettingsPath(currentFile));
 if(!previous.exists())return true;
 if(!previous.open(QIODevice::ReadOnly))return false;
 const auto contents=previous.readAll();
 if(previous.error()!=QFileDevice::NoError)return false;
 QSaveFile current(currentFile);
 return current.open(QIODevice::WriteOnly) && current.write(contents)==contents.size() && current.commit();
}

inline void migrateShortcuts(QSettings &settings,const char *const *keys,int count)
{
 for(int i=0;i<count;++i){
  const QString key=QString::fromLatin1(keys[i]);
  const QString oldKey="Sentinel."+key.section('.',1);
  if(!settings.contains(key) && settings.contains(oldKey))
   settings.setValue(key,settings.value(oldKey));
  settings.remove(oldKey);
 }
 if(!settings.contains("Lookout.ShowPanel") && settings.contains("Sentinel.Status"))
  settings.setValue("Lookout.ShowPanel",settings.value("Sentinel.Status"));
 for(const auto *key:{"Sentinel.Status","Sentinel.Storage","Sentinel.Dismiss"})settings.remove(key);
}
}
