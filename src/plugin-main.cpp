// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include "lookout.hpp"
#include "settings-migration.hpp"
#include <QAction>
#include <QDir>
#include <QFileInfo>
OBS_DECLARE_MODULE()
namespace {
QPointer<LookoutDock> dock;
QPointer<QAction> settingsAction;
bool dockRegistered=false;
}
MODULE_EXPORT const char *obs_module_name(void) { return "lookout"; }
MODULE_EXPORT const char *obs_module_description(void) { return "Private desktop alerts and session monitoring for OBS."; }
bool obs_module_load(void) { return true; }
void obs_module_post_load(void) {
 if (!obs_frontend_get_main_window()) return;
 if(obs_get_module(LookoutMigration::legacyModule)){
  blog(LOG_WARNING,"[lookout] An earlier lookout installation is still loaded. Run the current installer to upgrade.");
  return;
 }
 char *path=obs_module_config_path("lookout.ini");
 if (!path) return;
 QString file=QString::fromUtf8(path); bfree(path);
 if(!QDir().mkpath(QFileInfo(file).absolutePath()) || !LookoutMigration::importSettings(file)){
  blog(LOG_ERROR,"[lookout] Could not prepare the settings file. Existing settings were kept.");
  return;
 }
 dock=new LookoutDock(file);
 const QString binary=QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
 dock->enableQuickRenderer(QFileInfo(binary).absoluteDir().filePath("ui/lookout-ui.exe"));
 dockRegistered=obs_frontend_add_dock_by_id("lookout-status","lookout",dock.data());
 settingsAction=static_cast<QAction*>(obs_frontend_add_tools_menu_qaction("lookout Settings"));
 if(settingsAction)QObject::connect(settingsAction.data(),&QAction::triggered,dock.data(),&LookoutDock::openSettings);
}
void obs_module_unload(void) {
 delete settingsAction.data();
 if(dockRegistered && dock)obs_frontend_remove_dock("lookout-status");
 dockRegistered=false;
 delete dock.data();
}
