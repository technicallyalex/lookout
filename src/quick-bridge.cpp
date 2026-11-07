// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "quick-bridge.hpp"
#include "sentinel.hpp"
#include "thumbnail-render.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>
#include <QMouseEvent>
#include <QCursor>
#include <QScreen>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <graphics/vec4.h>
#include <cerrno>
#include <cmath>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif
namespace {
QJsonObject rect(const QRectF &r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
QString color(const QColor &c){return c.name(QColor::HexArgb);}
}
QuickPanelBridge::QuickPanelBridge(SentinelDock *dock,const QString &executable):QObject(dock),dock_(dock),executable_(executable){
 transitionPreviewTimer_.setSingleShot(true);
 connect(&transitionPreviewTimer_,&QTimer::timeout,this,&QuickPanelBridge::advanceTransitionPreview);
 pointerTimer_.setInterval(16);
 pointerTimer_.setTimerType(Qt::PreciseTimer);
 connect(&pointerTimer_,&QTimer::timeout,this,&QuickPanelBridge::trackPointer);
 token_=QUuid::createUuid().toString(QUuid::WithoutBraces);endpoint_="lookout-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef _WIN32
 frameEventName_=QStringLiteral("Local\\")+endpoint_+QStringLiteral("-frame");
 frameEvent_=CreateEventW(nullptr,FALSE,FALSE,reinterpret_cast<LPCWSTR>(frameEventName_.utf16()));
 if(!frameEvent_)frameEventName_.clear();
#endif
 server_.setSocketOptions(QLocalServer::UserAccessOption);
 connect(&server_,&QLocalServer::newConnection,this,[this]{auto *candidate=server_.nextPendingConnection();if(socket_){candidate->disconnectFromServer();candidate->deleteLater();return;}socket_=candidate;input_.clear();authenticated_=false;connect(candidate,&QLocalSocket::readyRead,this,[this]{receive();});connect(candidate,&QLocalSocket::disconnected,this,[this,candidate]{if(socket_==candidate){socket_=nullptr;authenticated_=false;thumbnail_=false;dock_->dragging_=false;dock_->resizing_=false;dock_->dragCard_.clear();capturingShortcut_=false;lastSettings_.clear();freeTexture();}candidate->deleteLater();});QTimer::singleShot(3000,candidate,[this,candidate]{if(socket_==candidate && !authenticated_)candidate->abort();});});
 connect(&process_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int,QProcess::ExitStatus){authenticated_=false;thumbnail_=false;if(!stopping_ && attempts_<3)QTimer::singleShot(1000,this,[this]{launch();});else if(!stopping_)dock_->setCaptureStatus("lookout renderer stopped. Reopen OBS to retry.");});
 connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){dock_->setCaptureStatus("lookout renderer could not start. Reinstall the complete plugin package.");});
 connect(&process_,&QProcess::readyReadStandardError,this,[this]{const auto text=process_.readAllStandardError();diagnostic_+=text;diagnostic_=diagnostic_.right(65536);blog(LOG_WARNING,"[lookout-ui] %s",text.left(2000).constData());});
 if(!server_.listen(endpoint_)){dock_->setCaptureStatus("lookout could not open its local renderer connection.");return;}
 heartbeat_.start();timer_.setInterval(200);connect(&timer_,&QTimer::timeout,this,[this]{publish();});timer_.start();obs_add_main_rendered_callback(render,this);launch();
}
bool QuickPanelBridge::transitionPreviewActive() const {
 return transitionPreview_!=PreviewPhase::Idle && transitionPreview_!=PreviewPhase::Restore;
}
void QuickPanelBridge::previewTransition(){
 if(!authenticated_ || transitionPreviewActive() || dock_->dragging_ || dock_->transformTimer_.isActive())return;
 transitionPreview_=PreviewPhase::Prepare;
 dock_->refreshPanel();
 transitionPreviewTimer_.start(75);
}
void QuickPanelBridge::advanceTransitionPreview(){
 // Leave enough time for the longest show/hide animation before moving on.
 switch(transitionPreview_){
 case PreviewPhase::Prepare:
  transitionPreview_=PreviewPhase::Show;
  publish();
  transitionPreviewTimer_.start(1200);
  break;
 case PreviewPhase::Show:
  transitionPreview_=PreviewPhase::Hide;
  publish();
  transitionPreviewTimer_.start(1050);
  break;
 case PreviewPhase::Hide:
  finishTransitionPreview();
  break;
 default:break;
 }
}
void QuickPanelBridge::finishTransitionPreview(){
 if(!transitionPreviewActive())return;
 transitionPreviewTimer_.stop();
 transitionPreview_=PreviewPhase::Restore;
 dock_->refreshPanel();
 transitionPreview_=PreviewPhase::Idle;
}
void QuickPanelBridge::launch(){
 if(stopping_ || process_.state()!=QProcess::NotRunning)return;++attempts_;lastScene_.clear();
 QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();for(const auto &name:environment.keys())if(name.startsWith("QT_") || name.startsWith("QML"))environment.remove(name);environment.insert("QSG_RHI_BACKEND","d3d11");const auto windows=environment.value("SystemRoot");environment.insert("PATH",windows+"\\System32;"+windows);process_.setProcessEnvironment(environment);
 process_.setProgram(executable_);process_.setWorkingDirectory(QFileInfo(executable_).absolutePath());process_.setArguments({"--socket",endpoint_,"--token",token_,"--parent",QString::number(QCoreApplication::applicationPid())});process_.start();
}
void QuickPanelBridge::send(const QJsonObject &message){if(!socket_ || socket_->state()!=QLocalSocket::ConnectedState)return;if(socket_->bytesToWrite()>2*1024*1024){socket_->abort();return;}socket_->write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');}
void QuickPanelBridge::receive(){
 if(!socket_)return;input_+=socket_->readAll();if(input_.size()>65536){socket_->abort();return;}
 for(int end;(end=input_.indexOf('\n'))>=0;){const auto line=input_.left(end);input_.remove(0,end+1);QJsonParseError error;const auto document=QJsonDocument::fromJson(line,&error);if(error.error!=QJsonParseError::NoError || !document.isObject()){socket_->abort();return;}const auto object=document.object();if(!authenticated_){if(object["type"]!="hello" || object["version"].toInt()!=1 || object["token"].toString()!=token_){socket_->abort();return;}authenticated_=true;lastScene_.clear();publish();}else command(object);}
}
void QuickPanelBridge::command(const QJsonObject &m){
 if(m["type"]=="settings"){settingsCommand(m);return;}
 if(m["type"]=="status"){dock_->setCaptureStatus(m["text"].toString().left(200));return;}
 if(m["type"]=="dismiss"){dock_->dismissAlert();return;}
 if(m["type"]!="transform" || !dock_->editMode_ || !dock_->quickVisible_)return;
 const QString phase=m["phase"].toString();const bool ongoing=phase=="move" || phase=="end" || phase=="cancel";
 if(ongoing && !dock_->dragging_)return;
 auto *target=dock_->movableCards().value(ongoing?dock_->dragCard_:m["card"].toString());if(!target)return;
 const QPoint start(m["sx"].toInt(),m["sy"].toInt()),end(m["ex"].toInt(),m["ey"].toInt());if((end-start).manhattanLength()>50000)return;
 // Reuse the existing transform/snapping implementation and configuration keys.
 auto dispatch=[&](QEvent::Type event,const QPoint &pos){
  const QPoint local=pos-dock_->badge_->pos()-target->mapTo(dock_->badge_,QPoint());
  QMouseEvent mouse(event,local,pos,event==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
   event==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
  dock_->eventFilter(target,&mouse);
 };
 if(phase.isEmpty() || phase=="begin"){
  if(dock_->dragging_)return;
  dispatch(QEvent::MouseButtonPress,start);
  if(m["resize"].isBool())dock_->resizing_=m["resize"].toBool();
  if(dock_->dragging_ && m["trackPointer"].toBool()){
   pointerPosition_=start;
   pointerTimer_.start();
  }
 }
 if(phase.isEmpty() || phase=="move" || phase=="end")dispatch(QEvent::MouseMove,end);
 if(phase.isEmpty() || phase=="end" || phase=="cancel")dispatch(QEvent::MouseButtonRelease,end);
 publish();
}
void QuickPanelBridge::trackPointer(){
#ifdef _WIN32
 if(!authenticated_ || !dock_->editMode_ || !dock_->quickVisible_ || !dock_->dragging_){
  pointerTimer_.stop();
  return;
 }
 // Read the same screen coordinates as the OBS layout, even after the
 // overlay moves away from the pointer or crosses a DPI boundary.
 const auto position=QCursor::pos();
 const bool down=(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON)?VK_RBUTTON:VK_LBUTTON)&0x8000)!=0;
 if(down && position==pointerPosition_)return;
 if(!down)pointerTimer_.stop();
 pointerPosition_=position;
 command({{"type","transform"},{"phase",down?"move":"end"},{"card",dock_->dragCard_},
          {"sx",dock_->dragOrigin_.x()},{"sy",dock_->dragOrigin_.y()},{"ex",position.x()},{"ey",position.y()}});
#endif
}
void QuickPanelBridge::publish(){
 if(stopping_ || !authenticated_)return;
 const auto blockReason=dock_->ready_ && dock_->quickVisible_?dock_->overlayBlockReason():QString{};
 overlayBlocked_=!blockReason.isEmpty();
 publishSettings();QJsonObject scene=snapshot(blockReason);
 thumbnail_=scene["visible"].toBool() && scene["hasThumbnail"].toBool();
 {std::lock_guard<std::mutex> lock(gpuMutex_);scene["texture"]=QJsonObject{{"handle",double(handle_)},{"generation",generation_},{"width",640},{"height",360},{"luidLow",double(luidLow_)},{"luidHigh",double(luidHigh_)},{"graphicsReady",graphicsReady_},{"supported",thumbnailSupported_},{"frameEvent",frameEventName_}};}
 const auto bytes=QJsonDocument(scene).toJson(QJsonDocument::Compact);if(bytes!=lastScene_){send({{"type","scene"},{"scene",scene}});lastScene_=bytes;heartbeat_.restart();}else if(heartbeat_.elapsed()>1000){send({{"type","heartbeat"}});heartbeat_.restart();}
}
void QuickPanelBridge::stop(){if(stopping_)return;stopping_=true;timer_.stop();transitionPreviewTimer_.stop();transitionPreview_=PreviewPhase::Idle;thumbnail_=false;obs_remove_main_rendered_callback(render,this);send({{"type","quit"}});if(socket_)socket_->flush();if(process_.state()!=QProcess::NotRunning && !process_.waitForFinished(1200)){process_.terminate();if(!process_.waitForFinished(500)){process_.kill();process_.waitForFinished(500);}}freeTexture();}
QuickPanelBridge::~QuickPanelBridge(){
 stop();
#ifdef _WIN32
 if(frameEvent_)CloseHandle(frameEvent_);
#endif
}
void QuickPanelBridge::freeTexture(){
 {std::lock_guard<std::mutex> lock(gpuMutex_);if(!shared_)return;}
 obs_enter_graphics();{std::lock_guard<std::mutex> lock(gpuMutex_);if(shared_)gs_texture_destroy(shared_);shared_=nullptr;handle_=0;lastVideoFrame_=0;++generation_;}obs_leave_graphics();
}
void QuickPanelBridge::render(void *data){
 auto *self=static_cast<QuickPanelBridge*>(data);
 if(self->graphicsReady_ && !self->thumbnail_)return;
 std::lock_guard<std::mutex> lock(self->gpuMutex_);
 self->graphicsReady_=true;self->thumbnailSupported_=gs_get_device_type()==GS_DEVICE_DIRECT3D_11;if(!self->thumbnailSupported_)return;
#ifdef _WIN32
 if(!self->luidLow_ && !self->luidHigh_){auto *device=static_cast<ID3D11Device*>(gs_get_device_obj());Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;if(device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter))){DXGI_ADAPTER_DESC desc{};adapter->GetDesc(&desc);self->luidLow_=desc.AdapterLuid.LowPart;self->luidHigh_=desc.AdapterLuid.HighPart;}}
#endif
 if(!self->thumbnail_ || !obs_get_main_texture())return;
 const auto frame=obs_get_video_frame_time();
 // OBS may call the render hook for several mixes in one graphics tick.
 if(frame==self->lastVideoFrame_ && self->handle_)return;
 obs_video_info video{};
 if(!obs_get_video_info(&video) || !video.base_width || !video.base_height)return;

 constexpr uint32_t width=640,height=360;
 if(!self->shared_){
  self->shared_=gs_texture_create(width,height,GS_RGBA,1,nullptr,GS_RENDER_TARGET|GS_SHARED_KM_TEX);
  if(!self->shared_)return;
 }
 const auto resetTexture=[&]{
  gs_texture_destroy(self->shared_);
  self->shared_=nullptr;
  self->handle_=0;
  ++self->generation_;
 };
 const auto handle=gs_texture_get_shared_handle(self->shared_);
 if(handle==GS_INVALID_HANDLE || !handle){resetTexture();return;}
 // Both devices use key 0. OBS can replace an unread frame, and it never
 // waits for the desktop renderer to present the preceding frame.
 const int acquired=gs_texture_acquire_sync(self->shared_,0,0);
 if(acquired==ETIMEDOUT)return;
 if(acquired!=0){resetTexture();return;}

 auto *target=gs_get_render_target();
 auto *depth=gs_get_zstencil_target();
 const auto space=gs_get_color_space();
 const bool srgb=gs_framebuffer_srgb_enabled();
 gs_viewport_push();
 gs_matrix_push();
 gs_matrix_identity();
 gs_blend_state_push();
 gs_reset_blend_state();
 gs_set_render_target_with_color_space(self->shared_,nullptr,GS_CS_SRGB);
 vec4 background{};
 vec4_set(&background,0,0,0,1);
 gs_clear(GS_CLEAR_COLOR,&background,1,0);
 SentinelThumbnail::drawProgram(width,height,video);
 gs_set_render_target_with_color_space(target,depth,space);
 gs_enable_framebuffer_srgb(srgb);
 gs_blend_state_pop();
 gs_matrix_pop();
 gs_viewport_pop();
 gs_flush();
 if(gs_texture_release_sync(self->shared_,0)!=0){resetTexture();return;}

 self->lastVideoFrame_=frame;
 if(self->handle_!=handle){
  self->handle_=handle;
  ++self->generation_;
 }
#ifdef _WIN32
 if(self->frameEvent_)SetEvent(self->frameEvent_);
#endif
}

QJsonObject QuickPanelBridge::snapshot(const QString &blockReason) const {
 const auto &d=*dock_;
 const bool publicMode=d.settings_.value("showInCaptures",false).toBool();
 const bool edit=d.editMode_ && !transitionPreviewActive();
 const bool prepare=transitionPreview_==PreviewPhase::Prepare;
 const bool restore=transitionPreview_==PreviewPhase::Restore;
 if(!d.ready_ || !d.quickVisible_ || !blockReason.isEmpty() || prepare || transitionPreview_==PreviewPhase::Hide){
  return {{"version",1},{"visible",false},{"bounds",rect(d.badge_->geometry())},{"public",publicMode},
          {"edit",edit},{"immediate",!d.ready_ || !blockReason.isEmpty() || prepare || restore},
          {"blockReason",blockReason},{"transition",panelTransition()}};
 }
 const double scale=d.panelScale();const auto palette=d.badge_->property("appliedPanelPalette").value<QPalette>();const QPalette colors=palette==QPalette()?d.badge_->palette():palette;
 const bool preview=d.glowPreviewUntil_>d.glowClock_.elapsed();const double risk=preview?d.glowPreviewRisk_:d.glowRisk();const QRect bounds=d.badge_->geometry();
 QJsonObject scene{{"version",1},{"visible",d.ready_ && d.quickVisible_},{"bounds",rect(bounds)},{"scale",scale},{"edit",edit},{"independent",d.independentLayout_},{"public",d.settings_.value("showInCaptures",false).toBool()},{"opacity",std::clamp(d.settings_.value("backgroundOpacity",100).toDouble()/100,0.0,1.0)},{"design",panelDesign()},{"top",color(colors.color(QPalette::Window))},{"bottom",color(colors.color(QPalette::Base))},{"text",color(colors.color(QPalette::WindowText))},{"muted",color(colors.color(QPalette::PlaceholderText))},{"edge",color(colors.color(QPalette::Mid))},{"accent",color(colors.color(QPalette::Highlight))},{"dark",colors.color(QPalette::Window).lightness()<128},{"logoDark",d.darkLogo()},{"font",d.font().family()},{"fontPx",QFontMetricsF(d.statValues_[0]->font()).height()*.8},{"glow",risk>=0 && (preview || d.settings_.value("glowEnabled",false).toBool())},{"glowColor",color(d.warningColor(std::max(0.0,risk)))},{"glowPeriod",d.glowPeriod(std::max(0.0,risk))},{"glowIntensity",d.settings_.value("glowIntensity",100).toDouble()/100},{"glowSpread",d.settings_.value("glowSpread",24).toInt()}};
 QJsonArray glow;for(auto r:d.glowTargets())glow<<rect(r);scene["glowTargets"]=glow;
 QJsonArray cards;
 scene["chartDetail"]=chartDetail();
 if(restore)scene["immediate"]=true;
 scene["transition"]=panelTransition();
 auto *screen=QGuiApplication::screens().value(d.settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());
 const auto display=screen?screen->geometry():bounds;
 scene["screen"]=rect(display);
 scene["right"]=bounds.center().x()>=display.center().x();
 scene["chartLines"]=chartLines();
 scene["glass"]=panelDesign()=="minimal" && !d.darkLogo();
 scene["blurAmount"]=std::clamp(d.settings_.value("blurAmount",0).toInt(),0,100);
 auto add=[&](QWidget *widget,QString id,QString type){QJsonObject item=rect(QRectF(widget->mapTo(d.badge_,QPoint()),widget->size()));item["id"]=id;item["type"]=type;return item;};
 auto finite=[](double value)->QJsonValue{return std::isfinite(value)?QJsonValue(value):QJsonValue();};
 auto point=[&](double x,double y){return QJsonArray{x,finite(y)};};
 if(d.stats_->isVisibleTo(d.badge_)){auto c=add(d.stats_,"stats","stats");QJsonArray rows;for(int i=0;i<SentinelDock::StatCount;++i)if(!d.statIcons_[i]->isHidden()){auto box=[&](QWidget *w){return rect(QRectF(w->mapTo(d.stats_,QPoint()),w->size()));};const double r=d.metricRisk(i);const bool flash=preview && i==d.glowPreviewMetric_;QJsonObject row{{"icon",d.statDefinitions_[i].icon},{"label",d.statNames_[i]->text()},{"value",d.statValues_[i]->text()},{"iconRect",box(d.statIcons_[i])},{"nameRect",box(d.statNames_[i])},{"valueRect",box(d.statValues_[i])},{"color",color(r>=0?d.warningColor(r):colors.color(QPalette::WindowText))},{"flash",flash},{"flashColor",color(d.warningColor(d.glowPreviewRisk_))},{"names",!d.statNames_[i]->isHidden()},{"values",!d.statValues_[i]->isHidden()}};rows<<row;}c["rows"]=rows;cards<<c;}
 if(d.badgeText_->isVisibleTo(d.badge_)){auto c=add(d.badgeText_,"alert","alert");c["html"]=d.badgeText_->text();cards<<c;}
 scene["hasThumbnail"]=d.previewCard_->isVisibleTo(d.badge_);if(scene["hasThumbnail"].toBool()){auto c=add(d.previewCard_,"thumbnail","thumbnail");c["preview"]=rect(QRectF(d.preview_->mapTo(d.previewCard_,QPoint()),d.preview_->size()));c["alertIcon"]=!d.alertIcon_->isHidden();QString role=d.alertIcon_->property("alertRole").toString();if(role=="record")role="recording";else if(role=="camera")role="virtual-camera";c["alertRole"]=role;c["alertColor"]=color(d.alertIcon_->property("alertColor").value<QColor>());cards<<c;}
 if(d.activeIcons_->isVisibleTo(d.badge_)){auto c=add(d.activeIcons_,"active-icons","icons");QJsonArray icons;const auto glyphs=d.activeIcons_->glyphs();const auto ids=d.activeIcons_->roles();for(int i=0;i<glyphs.size();++i){QString role=ids.value(i);role.remove("alert:");if(role=="camera")role="virtual-camera";else if(role=="bitrate")role="graphs";icons<<QJsonObject{{"icon",role},{"color",color(glyphs[i].color)},{"label",d.activeIcons_->toolTip()},{"flash",d.activeIcons_->freshAlerts_.contains(glyphs[i].alertId)}};}c["icons"]=icons;cards<<c;}
 auto chart=[&](QJsonObject &c,double min,double max,QString label,QString value,QString icon,bool compact){c["min"]=min;c["max"]=max;c["label"]=label;c["value"]=value;c["icon"]=icon;c["compact"]=compact;c["graphs"]=true;};
 auto views=QVector<HistoryGraphs*>{d.graphs_};
 for(auto *view:d.graphViews_)views.append(view);
 for(auto *view:views)if(view->isVisibleTo(d.badge_)){
  const auto &history=view->samples();
  const qint64 now=history.empty()?0:history.back().timeMs,begin=now-view->seconds_*1000;
  for(int i=0;i<HistoryGraphs::Count;++i)if(view->selected_[i]){
   const auto &metric=HistoryGraphs::metrics[i];
   auto c=rect(view->metricRect(i).translated(view->mapTo(d.badge_,QPoint())));
   c["id"]=d.independentLayout_?metric.card:"graphs";
   c["type"]="chart";
   double min=INFINITY,max=-INFINITY;
   QJsonArray points;
   for(const auto &sample:history)if(sample.timeMs>=begin){
    const double value=sample.values[i];
    points<<point(double(sample.timeMs-begin)/(view->seconds_*1000),value);
    if(std::isfinite(value)){min=std::min(min,value);max=std::max(max,value);}
   }
   const bool haveData=std::isfinite(min);
   const double threshold=view->thresholds_[i];
   if(i==3){min=0;max=100;}
   else if(i==0 && haveData){
    const double pad=std::max(1.0,(max-min)*.15);
    min=std::max(0.0,min-pad);max+=pad;
   }else{
    min=0;max=std::max({1.0,threshold*1.25,max*1.15});
   }
   chart(c,min,max,metric.label,"",metric.id,view->compact(i));
   c["series"]=QJsonArray{QJsonObject{{"points",points},{"index",i}}};
   c["unit"]=metric.unit;
   if(threshold>=0)c["threshold"]=threshold;
   const int stat=i<3?1+2*i:-1;
   const double risk=d.metricRisk(stat);
   c["headerColor"]=color(risk>=0?d.warningColor(risk):colors.color(QPalette::WindowText));
   c["flash"]=preview && stat>=0 && d.glowPreviewMetric_==stat;
   if(!haveData)c["hint"]="Waiting for samples";
   c["seconds"]=view->seconds_;cards<<c;
  }
 }
 if(d.bitrates_->isVisibleTo(d.badge_)){auto *b=d.bitrates_;auto c=add(b,"bitrate","chart");QJsonArray series,readings,idleIcons;const bool dark=colors.color(QPalette::Window).lightness()<128;const std::array<QColor,2> lineColors={colors.color(QPalette::Highlight),QColor(dark?"#f4b273":"#96540e")};for(int i=0;i<2;++i)idleIcons<<QJsonObject{{"icon",i?"streaming":"recording"}};const auto begin=b->now_-b->seconds_*1000;for(int i=0;i<2;++i)if(b->active_[i]){QJsonArray points;for(const auto &sample:b->history_)if(sample.timeMs>=begin)points<<point(double(sample.timeMs-begin)/(b->seconds_*1000),sample.generation[i]==b->generation_[i]?sample.bps[i]:NAN);series<<QJsonObject{{"points",points},{"index",i},{"color",color(lineColors[i])}};readings<<QJsonObject{{"icon",i==0?"recording":"streaming"},{"label",i==0?LookoutStats::recording.compact:LookoutStats::streaming.compact},{"color",color(lineColors[i])}};}chart(c,0,b->rangeMaximumBps(),LookoutStats::bitrate.compact,"","graphs",b->compact_);c["readings"]=readings;c["idleIcons"]=idleIcons;c["series"]=series;c["seconds"]=b->seconds_;if(readings.isEmpty())c["hint"]="No active outputs";else{const double max=b->rangeMaximumBps();c["axisScale"]=max>=1e9?1e9:max>=1e6?1e6:max>=1e3?1e3:1;c["unit"]=max>=1e9?"Gb/s":max>=1e6?"Mb/s":max>=1e3?"kb/s":"b/s";}c["rate"]=true;cards<<c;}
 if(d.replay_->isVisibleTo(d.badge_)){auto *r=d.replay_;auto c=add(r,"replay","chart");const auto begin=r->now_-r->seconds_*1000;QJsonArray points;for(const auto &p:r->history_)if(p.time>=begin)points<<point(double(p.time-begin)/(r->seconds_*1000),p.value);chart(c,0,100,LookoutStats::replay.compact,"","replay",r->compact_);c["series"]=QJsonArray{QJsonObject{{"points",points},{"index",0}}};c["seconds"]=r->seconds_;c["footnote"]="Estimated fill";if(!r->active_)c["hint"]="Replay buffer stopped";else if(!r->fill_.known)c["hint"]="Collecting replay data";cards<<c;}
 scene["cards"]=cards;return scene;
}
