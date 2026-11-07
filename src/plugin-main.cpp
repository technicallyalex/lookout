// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include "sentinel.hpp"
#include <QAction>
#include <QDir>
#include <QFileInfo>
OBS_DECLARE_MODULE()
namespace {
QPointer<SentinelDock> dock;
QPointer<QAction> settingsAction;
bool dockRegistered=false;
}
MODULE_EXPORT const char *obs_module_name(void) { return "lookout"; }
MODULE_EXPORT const char *obs_module_description(void) { return "Private desktop alerts and session monitoring for OBS."; }
bool obs_module_load(void) { return true; }
void obs_module_post_load(void) {
 if (!obs_frontend_get_main_window()) return;
 char *path=obs_module_config_path("sentinel.ini");
 if (!path) return;
 QString file=QString::fromUtf8(path); bfree(path);
 QDir().mkpath(QFileInfo(file).absolutePath());
 dock=new SentinelDock(file);
 const QString binary=QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
 dock->enableQuickRenderer(QFileInfo(binary).absoluteDir().filePath("ui/lookout-ui.exe"));
 dockRegistered=obs_frontend_add_dock_by_id("obs-sentinel-status","lookout",dock.data());
 settingsAction=static_cast<QAction*>(obs_frontend_add_tools_menu_qaction("lookout Settings"));
 if(settingsAction)QObject::connect(settingsAction.data(),&QAction::triggered,dock.data(),&SentinelDock::openSettings);
}
void obs_module_unload(void) {
 delete settingsAction.data();
 if(dockRegistered && dock)obs_frontend_remove_dock("obs-sentinel-status");
 dockRegistered=false;
 delete dock.data();
}
