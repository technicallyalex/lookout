// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "quick-bridge.hpp"
#include "sentinel.hpp"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QSet>
#include <util/dstr.h>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
const QMap<QString, bool> switches{
 {"showBitrate",true},{"showAlertIcon",true},
 {"showInCaptures",false},{"glowEnabled",false},
 {"independentCards",false},{"snapCards",true},{"snapScreen",true},
 {"storage",true},{"encoder",true},{"network",true},{"lifecycle",true},{"untilDismiss",false}
};
struct Range { double initial, minimum, maximum; };
const QMap<QString, Range> numbers{
 {"panelMode",{0,0,2}},{"monitor",{0,0,64}},{"corner",{1,0,3}},
 {"thumbnailPosition",{0,0,3}},{"panelScale",{1,0,4}},{"customSizePercent",{100,45,300}},
 {"offsetX",{24,0,10000}},{"offsetY",{24,0,10000}},{"panelWidth",{520,128,960}},
 {"thumbnailWidth",{320,80,800}},{"cardSpacing",{8,0,64}},{"snapDistance",{12,0,64}},
 {"backgroundOpacity",{100,0,100}},{"blurAmount",{0,0,100}},{"logoTheme",{0,0,2}},
 {"chartDetail",{0,0,1}},{"graphWindow",{60,30,900}},
 {"gib",{20,0.1,10000}},{"skip",{2,1,100}},{"networkPercent",{2,1,100}},
 {"seconds",{6,2,60}},{"glowIntensity",{100,0,200}},{"glowSpread",{24,4,64}},{"glowSpeed",{1,0.25,2}}
};
const QStringList metrics{"recording","storage","streaming","encoder","camera","network","replay","cpu","memory","disk-time"};
const QStringList designs{"obs","layered","minimal","studio","signal"};
const QStringList lineDesigns{"line","area","stepped","grid"};
const QStringList transitions{"instant","fade","flash","slide","blur","fall"};

QJsonObject paletteJson(const QPalette &palette)
{
 return {{"background",palette.color(QPalette::Window).name()},
         {"surface",palette.color(QPalette::Base).name()},
         {"text",palette.color(QPalette::WindowText).name()},
         {"muted",palette.color(QPalette::PlaceholderText).name()},
         {"edge",palette.color(QPalette::Mid).name()},
         {"accent",palette.color(QPalette::Highlight).name()}};
}
}

QString QuickPanelBridge::panelTransition() const
{
 const auto style=dock_->settings_.value("panelTransition").toString();
 return transitions.contains(style)?style:QStringLiteral("instant");
}

QString QuickPanelBridge::panelDesign() const
{
 const QString saved=dock_->settings_.value("panelDesign").toString();
 if(designs.contains(saved))return saved;
 const QStringList replacements{"obs","obs","layered","signal","studio","layered","signal","minimal","studio"};
 return replacements.value(dock_->settings_.value("graphAppearance",0).toInt(),"obs");
}

QString QuickPanelBridge::chartLines() const
{
 const QString saved=dock_->settings_.value("chartLines").toString();
 if(lineDesigns.contains(saved))return saved;
 const QStringList previousLines{"line","area","line","grid","stepped"};
 return previousLines.at(designs.indexOf(panelDesign()));
}

int QuickPanelBridge::chartDetail() const
{
 const auto &settings=dock_->settings_;
 const int fallback=panelDesign()=="minimal"?1:settings.value("obsDetail",0).toInt();
 return std::clamp(settings.value("chartDetail",fallback).toInt(),0,1);
}

QPalette QuickPanelBridge::panelPalette() const
{
 QPalette palette=QApplication::palette();
 const bool dark=obs_frontend_is_theme_dark();
 if((palette.color(QPalette::Window).lightness()<128)!=dark){
  palette.setColor(QPalette::Window,QColor(dark?"#25272b":"#f1f2f4"));
  palette.setColor(QPalette::WindowText,QColor(dark?"#f3f4f7":"#20242a"));
  palette.setColor(QPalette::PlaceholderText,QColor(dark?"#a9b0bc":"#525a67"));
  palette.setColor(QPalette::Mid,QColor(dark?"#484d57":"#c6cbd3"));
 }
 palette.setColor(QPalette::Base,palette.color(QPalette::Window));
 const QString design=panelDesign();
 if(design=="obs")return palette;
 struct Colors { const char *surface,*bottom,*text,*muted,*edge,*accent; };
 const bool day=!dock_->darkLogo();
 const QMap<QString,Colors> themes{
  {"layered",day?Colors{"#faf5ff","#eee3fa","#2f2147","#584373","#b8a1d2","#7446aa"}
                 :Colors{"#242038","#191629","#f4eaff","#bfb0d1","#5d4b74","#c6a2ff"}},
  {"minimal",day?Colors{"#f4fbff","#c6e0f1","#122a3b","#223f54","#f1fbff","#20618a"}
                 :Colors{"#202c3d","#151c2b","#f4f7ff","#b3c1d6","#4d5c72","#a9cfff"}},
  {"studio",day?Colors{"#effbfc","#dbf0f2","#143640","#365d65","#90bbc0","#126f7b"}
                :Colors{"#182931","#111e26","#edfaff","#aec7d0","#365765","#74d8e4"}},
  {"signal",day?Colors{"#fff8e6","#f7edce","#3e3014","#625025","#ccb675","#885700"}
                 :Colors{"#201f16","#15160f","#f4efda","#c6c0a1","#544c31","#f3cd67"}}
 };
 const auto colors=themes.value(design);
 palette.setColor(QPalette::Window,QColor(colors.surface));
 palette.setColor(QPalette::Base,QColor(colors.bottom));
 palette.setColor(QPalette::WindowText,QColor(colors.text));
 palette.setColor(QPalette::PlaceholderText,QColor(colors.muted));
 palette.setColor(QPalette::Mid,QColor(colors.edge));
 palette.setColor(QPalette::Highlight,QColor(colors.accent));
 return palette;
}

void QuickPanelBridge::openSettings()
{
#ifdef _WIN32
 if(process_.processId()>0)AllowSetForegroundWindow(DWORD(process_.processId()));
#endif
 settingsOpen_=true;
 lastSettings_.clear();
 publishSettings();
}

void QuickPanelBridge::publishSettings()
{
 if(!authenticated_ || !settingsOpen_)return;
 auto &dock=*dock_;
 QJsonObject values;
 for(auto it=switches.cbegin();it!=switches.cend();++it)
  values[it.key()]=dock.settings_.value(it.key(),it.value()).toBool();
 for(auto it=numbers.cbegin();it!=numbers.cend();++it)
  values[it.key()]=dock.settings_.value(it.key(),it.value().initial).toDouble();
 for(const auto &metric:metrics)values["iconVisible/"+metric]=dock.metricEnabled(metric);
 values["panelDesign"]=panelDesign();
 values["panelTransition"]=panelTransition();
 values["chartLines"]=chartLines();
 values["chartDetail"]=chartDetail();
 values["presentation"]=dock.displayMode();
 for(const auto &metric:HistoryGraphs::metrics)values[QString("graph/")+metric.id]=dock.graphMode(metric.id);
 for(const auto *id:dock.outputGraphIds_)values[QString("graph/")+id]=dock.graphMode(id);
 QJsonArray modeLabels,modeIds;
 for(const auto &mode:dock.displayModes_){modeLabels.append(QString::fromUtf8(mode.label));modeIds.append(mode.id);}
 values["iconStrip"]=dock.iconStripMode();
 values["thumbnail"]=dock.thumbnailEnabled();
 values["glowWarningColor"]=dock.warningColor(0).name();
 values["glowCriticalColor"]=dock.warningColor(1).name();
 QJsonArray monitors,presets;
 for(auto *screen:QGuiApplication::screens())monitors.append(screen->name());
 for(const auto &preset:dock.settings_.value("positionPresets").toList())
  presets.append(preset.toMap().value("name").toString());
 QMap<obs_hotkey_id,QStringList> bindings;
 if(dock.ready_)obs_enum_hotkey_bindings([](void *data,size_t,obs_hotkey_binding_t *binding){
  dstr label{};
  obs_key_combination_to_str(obs_hotkey_binding_get_key_combination(binding),&label);
  auto &items=*static_cast<QMap<obs_hotkey_id,QStringList>*>(data);
  items[obs_hotkey_binding_get_hotkey_id(binding)].append(QString::fromUtf8(label.array?label.array:""));
  dstr_free(&label);
  return true;
 },&bindings);
 QJsonArray shortcuts;
 for(const auto key:dock.keys_)shortcuts.append(bindings.value(key).join(" / "));
 QJsonArray dockOptions,dockOrder;
 QJsonObject statLabels;
 for(const auto &stat:dock.statDefinitions_)statLabels.insert(stat.id,QString::fromUtf8(stat.label.full));
 for(const auto &option:dock.dockButtonOptions_)
  dockOptions.append(QJsonObject{{"id",option.id},{"label",option.label},{"icon",option.icon}});
 for(const auto &id:dock.dockOrder_)dockOrder.append(id);
 QPalette obsPalette=QApplication::palette();
 if((obsPalette.color(QPalette::Window).lightness()<128)!=obs_frontend_is_theme_dark())
  obsPalette.setColor(QPalette::Window,obs_frontend_is_theme_dark()?QColor("#25272b"):QColor("#f1f2f4"));
 const QJsonObject state{{"values",values},{"monitors",monitors},{"presets",presets},{"shortcuts",shortcuts},
  {"displayModeLabels",modeLabels},{"displayModeIds",modeIds},{"statLabels",statLabels},
  {"dockOptions",dockOptions},{"dockOrder",dockOrder},
  {"editing",dock.editMode_},{"hasAlerts",dock.alertActive_ || dock.sessionNotice_},
  {"hasPreview",dock.glowPreviewUntil_>dock.glowClock_.elapsed()},
  {"thumbnailAlertPreview",dock.thumbnailAlertPreview_.isActive()},
  {"transitionPreview",transitionPreviewActive()},
  {"dark",obs_frontend_is_theme_dark()},{"logoDark",dock.darkLogo()},
  {"palette",paletteJson(obsPalette)},{"captureStatus",dock.captureStatus_},
  {"version",QStringLiteral(SENTINEL_VERSION)},{"build",QStringLiteral(SENTINEL_BUILD)}};
 const auto bytes=QJsonDocument(state).toJson(QJsonDocument::Compact);
 if(bytes==lastSettings_)return;
 send({{"type","settings"},{"state",state},{"open",lastSettings_.isEmpty()}});
 lastSettings_=bytes;
}

void QuickPanelBridge::settingsCommand(const QJsonObject &message)
{
 if(!settingsOpen_)return;
 auto &dock=*dock_;
 const QString action=message["action"].toString();
 const auto value=message["value"];
 const QString key=message["key"].toString();
 auto &settings=dock.settings_;
 if(action=="dockButton" && value.isString()){
  dock.editDockOrder(key,value.toString());
  publishSettings();
  return;
 }
 if(action=="close"){
  settingsOpen_=capturingShortcut_=false;
  const bool wasPreviewing=dock.thumbnailAlertPreview_.isActive();
  dock.thumbnailAlertPreview_.stop();
  dock.setEditMode(false);
  finishTransitionPreview();
  if(wasPreviewing)dock.refreshPanel();
  return;
 }
 if(action=="capture"){capturingShortcut_=value.toBool();return;}
 if(!dock.ready_)return;
 if(action=="shortcut"){
  const int index=message["index"].toInt(-1);
  const int native=value.toInt(0);
  if(index>=0 && index<SentinelDock::HotkeyCount && native>=0 && native<=255){
   const auto keyCode=obs_key_from_virtual_key(native);
   const int mask=message["modifiers"].toInt();
   uint32_t modifiers=0;
   if(mask & 1)modifiers|=INTERACT_SHIFT_KEY;
   if(mask & 2)modifiers|=INTERACT_CONTROL_KEY;
   if(mask & 4)modifiers|=INTERACT_ALT_KEY;
   if(mask & 8)modifiers|=INTERACT_COMMAND_KEY;
   if(native==0 || keyCode!=OBS_KEY_NONE)
    dock.setHotkeyBinding(index,{modifiers,native?keyCode:OBS_KEY_NONE});
  }
  publishSettings();
  return;
 }
 if(action=="previewThumbnailAlert"){
  dock.previewThumbnailAlert();
  publishSettings();
  return;
 }
 if(action=="previewTransition"){
  previewTransition();
  return;
 }
 if(action=="edit")dock.setEditMode(value.toBool());
 else if(action=="resetPosition")dock.defaultPosition();
 else if(action=="previewAlert")dock.previewAlert(value.toInt(-1));
 else if(action=="previewGlow")dock.previewGlow(value.toBool()?1:0);
 else if(action=="dismiss")dock.dismissAlert();
 else if(action=="stopPreview"){
  dock.glowPreviewUntil_=0;dock.glowPreviewMetric_=-1;
  dock.previewAlertTitle_.clear();dock.previewAlertBody_.clear();
 }
 else if(action=="clearHistory"){
  dock.graphs_->clearHistory();dock.bitrates_->clearHistory();dock.replay_->clearHistory();
 }
 else if(action=="savePreset" || action=="loadPreset" || action=="removePreset"){
  const QString name=value.toString().trimmed().left(64);
  if(name.isEmpty())return;
  if(action=="savePreset")dock.savePreset(name);
  else if(action=="loadPreset")dock.loadPreset(name);
  else dock.removePreset(name);
 }
 else if(action=="set"){
  if(key=="presentation"){
   dock.setDisplayMode(value.toInt(-1));
   return;
  }
  else if(key.startsWith("graph/") && value.isDouble()){
   const QString id=key.mid(6);
   const int style=value.toInt(-1);
   const bool known=std::any_of(std::begin(HistoryGraphs::metrics),std::end(HistoryGraphs::metrics),
    [&](const auto &metric){return id==metric.id;}) || id=="bitrate" || id=="replay";
   if(!known || style<SentinelDock::GraphOff || style>SentinelDock::GraphFull)return;
   settings.setValue(key,style);
   if(style!=SentinelDock::GraphOff)dock.metricsVisible_=true;
  }
  else if(key=="thumbnail" && value.isBool())
   settings.setValue("showThumbnail",value.toBool());
  else if(key=="panelDesign" && designs.contains(value.toString())){
   settings.setValue("chartLines",chartLines());
   settings.setValue("chartDetail",chartDetail());
   settings.setValue(key,value.toString());
   dock.updateBranding();
  }
  else if(key=="chartLines" && lineDesigns.contains(value.toString()))
   settings.setValue(key,value.toString());
  else if(key=="panelTransition" && transitions.contains(value.toString()))
   settings.setValue(key,value.toString());
  else if(key=="glowWarningColor" || key=="glowCriticalColor"){
   const QColor color(value.toString());
   if(!color.isValid())return;
   settings.setValue(key,color.name());
  }
  else if(value.isBool() && (switches.contains(key) || (key.startsWith("iconVisible/") && metrics.contains(key.mid(12))))){
   settings.setValue(key,value.toBool());
   if(value.toBool() && (key=="showBitrate" || key.startsWith("iconVisible/")))
    dock.metricsVisible_=true;
   if(key=="storage" || key=="encoder" || key=="network"){
    dock.storageAlarm_=dock.storageFull_=dock.storageUnavailable_=false;
    dock.encoderAlarm_=dock.networkAlarm_=dock.reconnectAlarm_=false;
   }
   if(key=="untilDismiss"){
    if(value.toBool())dock.dismiss_.stop();
    else if(dock.alertActive_ || dock.sessionNotice_)
     dock.dismiss_.start(settings.value("seconds",6).toInt()*1000);
   }
  }
  else if(numbers.contains(key) && value.isDouble()){
   if(key=="logoTheme" && panelDesign()=="obs")return;
   const auto range=numbers.value(key);
   const double number=std::clamp(value.toDouble(),range.minimum,range.maximum);
   if(key=="panelScale"){
    if(int(number)==4)dock.applyCustomSize();
    else dock.applyScalePreset(int(number));
   }else if(key=="customSizePercent"){
    const double scale=number/100;
    settings.setValue(key,int(number));
    settings.setValue("customScale",scale);
    settings.setValue("customPanelWidth",qRound(320*scale)+16);
    settings.setValue("customThumbnailWidth",std::max(80,qRound(320*scale)-16));
    dock.applyCustomSize();
   }else{
    settings.setValue(key,key=="gib" || key=="glowSpeed"?QVariant(number):QVariant(int(number)));
    if(key=="monitor" || key=="corner" || key=="offsetX" || key=="offsetY")
     settings.setValue("customPosition",false);
    if(key=="panelWidth" || key=="thumbnailWidth")dock.rememberCustomSize();
    if(key=="gib")dock.storageAlarm_=dock.storageFull_=false;
    if(key=="logoTheme")dock.updateBranding(true);
   }
  }else return;
 }else return;
 settings.sync();
 dock.refreshPanel();
 publish();
}
