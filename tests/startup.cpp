// SPDX-License-Identifier: GPL-2.0-or-later
#include "sentinel.hpp"
#include "panel-visuals.hpp"
#include "lookout-brand.hpp"
#include "quick-bridge.hpp"
#include <QJsonArray>
#include <QApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialog>
#include <QGroupBox>
#include <QPushButton>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QKeyEvent>
#include <QScreen>
#include <QMenu>
#include <QMouseEvent>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <QRegularExpression>
#include <util/config-file.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <graphics/vec4.h>
#include <obs-source.h>
#include <util/platform.h>
namespace {
bool replayTesting=false;obs_output_t *replayTestOutput=nullptr;std::atomic<int> qaVideoColor{0};
QString obsTestPath(const QString &relative)
{
 return qEnvironmentVariable("SENTINEL_OBS_ROOT","C:/Program Files/obs-studio")+"/"+relative;
}
void qaDraw(void*,uint32_t,uint32_t)
{
 vec4 color;
 if(qaVideoColor==2){
  static unsigned step=0;
  vec4_set(&color,.08f,.10f+float(step++%12)*.06f,.9f,1);
 }else if(qaVideoColor==1)vec4_set(&color,.9f,.15f,.05f,1);
 else vec4_set(&color,.08f,.36f,.9f,1);
 gs_clear(GS_CLEAR_COLOR,&color,1,0);
}
}
struct SentinelTestAccess {
 static bool sampling(const SentinelDock &dock){return dock.timer_.isActive();}
 static void quickMigration(SentinelDock &dock){
  const QString executable=qEnvironmentVariable("SENTINEL_QUICK_QA");if(executable.isEmpty())return;
  auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
  QVariantMap saved;for(const auto &key:dock.settings_.allKeys())saved[key]=dock.settings_.value(key);
  // Exercise the normal OBS scene render. A pre-render clear can otherwise
  // populate the texture early enough to hide a broken thumbnail callback.
  obs_source_info solid{};
  solid.id="lookout_thumbnail_regression";
  solid.type=OBS_SOURCE_TYPE_INPUT;
  solid.output_flags=OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
  solid.get_name=[](void *){return "lookout test color";};
  solid.create=[](obs_data_t *,obs_source_t *source)->void *{return source;};
  solid.destroy=[](void *){};
  solid.get_width=[](void *)->uint32_t{return 1280;};
  solid.get_height=[](void *)->uint32_t{return 720;};
  solid.video_render=[](void *,gs_effect_t *){qaDraw(nullptr,1280,720);};
  obs_register_source(&solid);
  auto *program=obs_source_create(solid.id,"lookout thumbnail test",nullptr,nullptr);
  auto *programScene=obs_scene_create("lookout thumbnail regression scene");
  checkState(program && programScene,"create normal thumbnail program scene");
  auto *programItem=obs_scene_add(programScene,program);
  obs_remove_main_render_callback(qaDraw,nullptr);
  obs_set_output_source(0,obs_scene_get_source(programScene));
  QWidget backdrop;backdrop.setWindowFlags(Qt::Tool|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);backdrop.setStyleSheet("background:#123a2a;");backdrop.setGeometry(0,0,2000,1400);backdrop.show();dock.dialog_->hide();
  dock.settings_.setValue("panelMode",1);dock.metricsVisible_=true;format(dock,0);dock.settings_.setValue("showThumbnail",true);dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("panelWidth",400);dock.settings_.setValue("thumbnailWidth",320);dock.settings_.setValue("thumbnailPosition",1);dock.settings_.setValue("blurAmount",0);dock.settings_.setValue("customPosition",true);dock.settings_.setValue("positionX",100);dock.settings_.setValue("positionY",60);dock.setEditMode(false);dock.enableQuickRenderer(executable);wait(2500);
  checkState(dock.quick_->connected(),"bundled QML renderer authenticates over private local connection");checkState(!dock.badge_->isVisible(),"QML renderer replaces native overlay rather than duplicating it");
  if(!dock.quick_->diagnostic_.isEmpty())std::fprintf(stderr,"Renderer diagnostics: %s\n",dock.quick_->diagnostic_.constData());
  checkState(!dock.quick_->diagnostic_.contains("qrc:/quick/") && dock.quick_->process_.state()==QProcess::Running,"QML components load without runtime errors");
  const auto firstCards=dock.quick_->snapshot()["cards"].toArray();
  checkState(!firstCards.isEmpty(),"saved Readings mode is visible on renderer startup");
  for(const auto &card:firstCards)
   checkState(!card.toObject()["graphs"].toBool(),"saved Readings mode starts without graph plots");
  format(dock,1);dock.refreshPanel();wait(350);
  const QStringList metrics{"recording","storage","streaming","encoder","camera","network","cpu","memory","disk-time","bitrate","replay"};
  auto statsCard=[&]{
   for(const auto &entry:dock.quick_->snapshot()["cards"].toArray())
    if(entry.toObject()["id"]=="stats")return entry.toObject();
   return QJsonObject{};
  };
  auto checkReadings=[&](int count){
   const auto card=statsCard();
   const auto rows=card["rows"].toArray();
   checkState(rows.size()==count,"quick readings card contains each enabled metric");
   int previousBottom=0;
   for(const auto &row:rows){
    const auto name=row.toObject()["nameRect"].toObject();
    const int y=name["y"].toInt(),height=name["h"].toInt();
    if(y<previousBottom || height<1 || y+height>card["h"].toInt())
     std::fprintf(stderr,"Readings layout: %d rows, card height %d (hint %d, info %d, badge %d), row y=%d height=%d previous bottom=%d\n",
                  count,card["h"].toInt(),dock.statsLayout_->sizeHint().height(),dock.information_->height(),dock.badge_->height(),y,height,previousBottom);
    checkState(y>=previousBottom && height>0 && y+height<=card["h"].toInt(),
               "quick readings have separate rows inside their card after metrics are restored");
    previousBottom=y+height;
   }
  };
  checkReadings(SentinelDock::StatCount);
  const auto liveRows=statsCard()["rows"].toArray();
  checkState(liveRows[SentinelDock::CpuStat].toObject()["value"].toString().endsWith('%') &&
             liveRows[SentinelDock::MemoryStat].toObject()["value"].toString().endsWith(" MiB") &&
             liveRows[SentinelDock::DiskTimeStat].toObject()["value"]=="Idle",
             "Quick publishes the live OBS process metrics and the idle disk estimate");
  const auto savedFreeBytes=dock.freeBytes_;
  auto diskTime=[&]{
   dock.refreshPanel();
   return statsCard()["rows"].toArray()[SentinelDock::DiskTimeStat].toObject()["value"].toString();
  };
  dock.recordingActive_=true;dock.freeBytes_=3600000000;
  checkState(diskTime()=="Calculating…","disk estimate waits for measured recording throughput");
  dock.recordingSpace_.update(8000000);
  checkState(diskTime().contains("1 h"),"disk estimate publishes remaining recording time");
  dock.recordingPaused_=true;
  checkState(diskTime()=="Paused","paused recordings show no stale countdown");
  dock.freeBytes_=0;
  checkState(diskTime()=="0 min","a full recording drive reports zero remaining time");
  dock.recordingActive_=dock.recordingPaused_=false;dock.freeBytes_=savedFreeBytes;
  dock.recordingSpace_.update(std::nullopt);dock.refreshPanel();
  for(const bool independent:{false,true}){
   dock.settings_.setValue("independentCards",independent);
   for(const auto &metric:metrics){
    dock.settings_.setValue(metric=="bitrate"?"showBitrate":"iconVisible/"+metric,false);
    dock.refreshPanel();wait(30);
   }
   checkState(statsCard().isEmpty() && dock.quick_->snapshot()["hasThumbnail"].toBool(),
              "turning off all readings leaves the thumbnail by itself");
   for(int i=0;i<metrics.size();++i){
    dock.settings_.setValue(metrics[i]=="bitrate"?"showBitrate":"iconVisible/"+metrics[i],true);
    dock.refreshPanel();wait(30);
    checkReadings(qMin(i+1,int(SentinelDock::StatCount)));
   }
  }
  dock.settings_.setValue("independentCards",false);
  dock.refreshPanel();
  const QVariant scaleSetting=dock.settings_.value("panelScale",1);
  for(int preset:{0,1,2,3,4}){
   dock.settings_.setValue("panelScale",preset);
   int metricsWidth=0,thumbnailWidth=0;
   for(int placement:{0,1,2,3}){
    dock.settings_.setValue("thumbnailPosition",placement);
    dock.refreshPanel();
    const QRect stats(dock.stats_->mapTo(dock.badge_,QPoint()),dock.stats_->size());
    const QRect thumbnail(dock.previewCard_->mapTo(dock.badge_,QPoint()),dock.previewCard_->size());
    if(placement==0){metricsWidth=stats.width();thumbnailWidth=thumbnail.width();}
    checkState(stats.width()==metricsWidth && thumbnail.width()==thumbnailWidth,
               "grouped thumbnail placement preserves both widths when the display has room");
    checkState(!stats.intersects(thumbnail) && dock.badge_->rect().contains(thumbnail),
               "thumbnail and metrics are separate and inside the overlay");
    checkState(placement==0?thumbnail.bottom()<stats.top():placement==1?thumbnail.top()>stats.bottom():
               placement==2?thumbnail.right()<stats.left():thumbnail.left()>stats.right(),
               "all four requested thumbnail positions are respected");
   }
  }
  dock.settings_.setValue("panelScale",scaleSetting);dock.settings_.setValue("panelWidth",400);
  dock.settings_.setValue("thumbnailWidth",320);dock.settings_.setValue("thumbnailPosition",1);
  dock.refreshPanel();wait(350);
  const auto scene=dock.quick_->snapshot();checkState(scene["cards"].toArray().size()>=6 && scene["hasThumbnail"].toBool(),"QML receives stats, bitrate, replay, health charts and thumbnail");
  QList<QRect> publishedCards;
  for(const auto &entry:scene["cards"].toArray()){
   const auto card=entry.toObject();
   const QRect bounds(card["x"].toInt(),card["y"].toInt(),card["w"].toInt(),card["h"].toInt());
   for(const auto &earlier:publishedCards){
    if(earlier.intersects(bounds)){
     std::fprintf(stderr,"Restored card %s (%d,%d %dx%d) overlaps (%d,%d %dx%d)\n",
                  qPrintable(card["id"].toString()),bounds.x(),bounds.y(),bounds.width(),bounds.height(),
                  earlier.x(),earlier.y(),earlier.width(),earlier.height());
     const auto *layout=dock.information_->layout();
     for(int i=0;i<layout->count();++i){
      auto *widget=layout->itemAt(i)->widget();
      const auto g=widget->geometry();
      std::fprintf(stderr,"Item %d %s: %d,%d %dx%d hidden=%d\n",i,qPrintable(widget->objectName()),
                   g.x(),g.y(),g.width(),g.height(),widget->isHidden());
     }
    }
    checkState(!earlier.intersects(bounds),"regrouped cards do not overlap after restoring metrics");
   }
   publishedCards.append(bounds);
  }
#ifdef _WIN32
  struct Find {DWORD pid;HWND window=nullptr;};Find target{DWORD(dock.quick_->process_.processId())};auto find=[](HWND w,LPARAM data)->BOOL{auto *target=reinterpret_cast<Find*>(data);DWORD pid=0;GetWindowThreadProcessId(w,&pid);wchar_t title[128]{};GetWindowTextW(w,title,128);if(pid==target->pid && wcscmp(title,L"lookout panels")==0)target->window=w;return TRUE;};EnumWindows(find,reinterpret_cast<LPARAM>(&target));checkState(target.window && IsWindowVisible(target.window),"QML native panel is visible");DWORD affinity=99;checkState(GetWindowDisplayAffinity(target.window,&affinity) && affinity==WDA_NONE,"public QML panel is capturable");
  auto grab=[&](QString name){const QRect bounds=dock.badge_->geometry();auto pix=QGuiApplication::primaryScreen()->grabWindow(0,bounds.x(),bounds.y(),bounds.width(),bounds.height());checkState(pix.save(qEnvironmentVariable("SENTINEL_GPU_CAPTURE")+name+".png"),"QML desktop evidence saved");return pix.toImage();};
  const auto publicImage=grab(".qml-public");const QPointF center=dock.preview_->mapTo(dock.badge_,dock.preview_->rect().center());const QColor pixel=publicImage.pixelColor(qRound(center.x()*publicImage.devicePixelRatio()),qRound(center.y()*publicImage.devicePixelRatio()));std::fprintf(stdout,"QML live thumbnail pixel: %d,%d,%d\n",pixel.red(),pixel.green(),pixel.blue());checkState(pixel.blue()>150 && pixel.blue()>pixel.red()*2,"real OBS GPU thumbnail reaches the QML texture across processes");
  qaVideoColor=1;wait(700);const auto nextFrame=grab(".qml-live-red");const QColor red=nextFrame.pixelColor(qRound(center.x()*nextFrame.devicePixelRatio()),qRound(center.y()*nextFrame.devicePixelRatio()));checkState(red.red()>180 && red.red()>red.blue()*2,"QML thumbnail keeps updating when OBS output changes");qaVideoColor=0;wait(250);
  // Sample what Qt Quick actually presents, not the OBS callback count.
  RECT overlay{};
  GetWindowRect(target.window,&overlay);
  const double displayScale=publicImage.devicePixelRatio();
  const int sampleX=overlay.left+qRound(center.x()*displayScale);
  const int sampleY=overlay.top+qRound(center.y()*displayScale);
  const HDC desktop=GetDC(nullptr);
  checkState(desktop!=nullptr,"acquire desktop pixel sampler");
  COLORREF previous=CLR_INVALID;
  int changes=0;
  QTimer sampling;
  sampling.setTimerType(Qt::PreciseTimer);
  QObject::connect(&sampling,&QTimer::timeout,[&]{
   const auto pixel=GetPixel(desktop,sampleX,sampleY);
   if(pixel==CLR_INVALID || GetBValue(pixel)<150 || pixel==previous)return;
   previous=pixel;
   ++changes;
  });
  qaVideoColor=2;
  wait(150);
  QElapsedTimer sampleTime;
  sampleTime.start();
  sampling.start(3);
  wait(1800);
  sampling.stop();
  const double delivered=changes*1000.0/sampleTime.elapsed();
  ReleaseDC(nullptr,desktop);
  const double expected=std::min<double>(60.0,QGuiApplication::primaryScreen()->refreshRate());
  std::fprintf(stdout,"QML thumbnail: %.1f displayed frame changes/s (60 fps OBS source)\n",delivered);
  checkState(delivered>=expected*.75,"thumbnail tracks OBS cadence without the old 15 fps cap");
  qaVideoColor=0;
  dock.settings_.setValue("showThumbnail",false);
  dock.refreshPanel();
  wait(250);
  checkState(!dock.quick_->thumbnail_,"hidden thumbnail pauses transfer");
  dock.settings_.setValue("showThumbnail",true);
  dock.refreshPanel();
  wait(600);
  const auto resumed=grab(".qml-resumed");
  checkState(resumed.pixelColor(qRound(center.x()*resumed.devicePixelRatio()),qRound(center.y()*resumed.devicePixelRatio())).blue()>150,
             "thumbnail resumes after toggling");
  obs_set_output_source(0,nullptr);
  obs_sceneitem_remove(programItem);
  obs_scene_release(programScene);
  obs_source_release(program);
  obs_wait_for_destroy_queue();
  obs_add_main_render_callback(qaDraw,nullptr);
  dock.settings_.setValue("glowSpeed",0.25);dock.previewAlert(5);wait(1350);const auto alert=grab(".qml-alert");const auto targets=dock.glowTargets();checkState(!targets.isEmpty(),"threshold preview selects the offending QML cards");const auto halo=targets.first();const double dpi=alert.devicePixelRatio();bool redHalo=false;for(int dx=2;dx<=12;++dx){const auto p=alert.pixelColor(qRound((halo.left()-dx)*dpi),qRound(halo.center().y()*dpi));redHalo|=p.red()>45 && p.red()>p.green();}checkState(redHalo,"public QML capture includes a red alert glow outside the card");
  dock.settings_.setValue("showInCaptures",false);dock.refreshPanel();wait(500);checkState(IsWindowVisible(target.window) && GetWindowDisplayAffinity(target.window,&affinity) && affinity==WDA_EXCLUDEFROMCAPTURE,"private QML window remains visible on desktop but excluded from capture");const auto privateImage=grab(".qml-private");checkState(privateImage!=publicImage,"private screenshot excludes QML cards and glow");
  const QPoint hiddenCenter=dock.preview_->mapTo(dock.badge_,dock.preview_->rect().center());const QColor hidden=privateImage.pixelColor(qRound(hiddenCenter.x()*dpi),qRound(hiddenCenter.y()*dpi));const QColor hiddenHalo=privateImage.pixelColor(qRound((halo.left()-5)*dpi),qRound(halo.center().y()*dpi));auto backdropPixel=[](const QColor &p){return std::abs(p.red()-18)<4 && std::abs(p.green()-58)<4 && std::abs(p.blue()-42)<4;};checkState(backdropPixel(hidden) && backdropPixel(hiddenHalo),"private capture reveals the known backdrop instead of thumbnail and glow");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_WGC_QA")){
   SetDllDirectoryW(reinterpret_cast<LPCWSTR>(obsTestPath("bin/64bit").utf16()));obs_module_t *module=nullptr;checkState(obs_open_module(&module,obsTestPath("core/win-capture/win-capture.dll").toUtf8().constData(),obsTestPath("core/win-capture/data").toUtf8().constData())==MODULE_SUCCESS && obs_init_module(module),"load actual OBS Windows capture module");auto *settings=obs_data_create();obs_data_set_int(settings,"method",2);auto *props=obs_get_source_properties("monitor_capture");checkState(props,"display capture properties");auto *monitors=obs_properties_get(props,"monitor_id");checkState(monitors && obs_property_list_item_count(monitors)>0,"enumerate real displays");obs_data_set_string(settings,"monitor_id",obs_property_list_item_string(monitors,0));obs_properties_destroy(props);obs_data_set_bool(settings,"capture_cursor",false);obs_data_set_bool(settings,"force_sdr",true);auto *capture=obs_source_create("monitor_capture","lookout isolated WGC QA",settings,nullptr);obs_data_release(settings);checkState(capture,"create actual OBS WGC display source");obs_source_inc_showing(capture);obs_source_inc_active(capture);wait(2500);const uint32_t w=obs_source_get_width(capture),h=obs_source_get_height(capture);checkState(w && h,"actual OBS WGC receives display frames");
   auto *scene=obs_scene_create("lookout isolated WGC scene");checkState(scene,"create isolated OBS compositor scene");auto *item=obs_scene_add(scene,capture);vec2 scale;vec2_set(&scale,1280.f/w,720.f/h);obs_sceneitem_set_scale(item,&scale);obs_remove_main_render_callback(qaDraw,nullptr);obs_set_output_source(0,obs_scene_get_source(scene));
   struct Pixel {std::atomic<int> x{0},y{0};std::atomic<uint32_t> rgb{0},frames{0};} pixel;
   auto read=[](void *p,video_data *frame){auto &v=*static_cast<Pixel*>(p);const auto *b=frame->data[0]+v.y.load()*frame->linesize[0]+v.x.load()*4;v.rgb=(uint32_t(b[2])<<16)|(uint32_t(b[1])<<8)|b[0];++v.frames;};obs_add_raw_video_callback(nullptr,read,&pixel);
   auto sample=[&]{RECT native{};GetWindowRect(target.window,&native);const auto p=dock.stats_->mapTo(dock.badge_,QPoint(dock.stats_->width()*2/3,8));pixel.x=std::clamp(qRound((native.left+p.x()*dpi)*1280/w),0,1279);pixel.y=std::clamp(qRound((native.top+p.y()*dpi)*720/h),0,719);const auto count=pixel.frames.load();wait(850);checkState(pixel.frames>count,"OBS delivered new compositor frames");return QColor::fromRgb(pixel.rgb.load());};
   const auto privatePixel=sample();dock.settings_.setValue("showInCaptures",true);dock.refreshPanel();wait(250);const auto publicPixel=sample();std::fprintf(stdout,"OBS WGC public/private pixel: %s / %s\n",qPrintable(publicPixel.name()),qPrintable(privatePixel.name()));checkState(privatePixel!=publicPixel,"actual OBS compositor sees public QML cards but excludes private cards");dock.settings_.setValue("showInCaptures",false);dock.refreshPanel();wait(250);const auto privateAgain=sample();checkState(std::abs(privateAgain.red()-privatePixel.red())<4 && std::abs(privateAgain.green()-privatePixel.green())<4 && std::abs(privateAgain.blue()-privatePixel.blue())<4,"actual OBS WGC exclusion restores consistently");
   auto setCaptureMethod=[&](int method){
    auto *config=obs_source_get_settings(capture);
    obs_data_set_int(config,"method",method);
    obs_source_update(capture,config);
    obs_data_release(config);
   };
   setCaptureMethod(0);
   wait(400);
   checkState(!IsWindowVisible(target.window) && !dock.quick_->thumbnail_,
              "Automatic Display Capture immediately pauses private panels and thumbnail transfer");
   auto *activeScene=obs_get_output_source(0);
   auto *captureConfig=obs_source_get_settings(capture);
   checkState(activeScene==obs_scene_get_source(scene) && obs_sceneitem_visible(item) &&
              obs_source_active(capture) && obs_data_get_int(captureConfig,"method")==0,
              "lookout does not modify the source, capture method or active scene");
   obs_data_release(captureConfig);
   obs_source_release(activeScene);
   setCaptureMethod(2);
   wait(500);
   checkState(IsWindowVisible(target.window) && dock.quick_->thumbnail_,
              "private panels and thumbnail resume with explicitly compatible Display Capture");
   obs_set_output_source(0,nullptr);obs_remove_raw_video_callback(read,&pixel);obs_sceneitem_remove(item);obs_scene_release(scene);obs_wait_for_destroy_queue();obs_add_main_render_callback(qaDraw,nullptr);obs_source_dec_active(capture);obs_source_dec_showing(capture);obs_source_release(capture);obs_wait_for_destroy_queue();std::puts("PASS: actual OBS capture isolation and public/private compositor verification");
  }
  auto privateSurfaces=[&](int amount){
   dock.settings_.setValue("blurAmount",amount);dock.refreshPanel();wait(350);
   int count=0;QPair<DWORD,int*> privateState(target.pid,&count);
   EnumWindows([](HWND w,LPARAM data)->BOOL{
    auto *state=reinterpret_cast<QPair<DWORD,int*>*>(data);DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    if(pid!=state->first || !IsWindowVisible(w))return TRUE;
    DWORD affinity=0;checkState(GetWindowDisplayAffinity(w,&affinity) && affinity==WDA_EXCLUDEFROMCAPTURE,"every visible QML/blur surface inherits capture exclusion");
    ++*state->second;return TRUE;
   },reinterpret_cast<LPARAM>(&privateState));
   return count;
  };
  checkState(privateSurfaces(100)>1 && privateSurfaces(10)>1 && privateSurfaces(0)==1,"full and subtle blur are private, and zero disables the backdrop");
  dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("backgroundOpacity",0);
  dock.glowPreviewUntil_=0;dock.dismissAlert();dock.refreshPanel();wait(300);
  const auto stripePoint=[&](int x){
   return dock.badge_->pos()+dock.stats_->mapTo(dock.badge_,QPoint(x,dock.stats_->height()/2));
  };
  const auto finePoint=stripePoint(dock.stats_->width()*2/5), broadPoint=stripePoint(dock.stats_->width()*3/5);
  QWidget fineStripe(&backdrop),broadStripe(&backdrop);
  auto addStripe=[&](QWidget &stripe,const QPoint &point,int width){
   const auto local=backdrop.mapFromGlobal(point);
   stripe.setGeometry(local.x()-width/2,local.y()-100,width,200);
   stripe.setStyleSheet("background:#fff3d5;");stripe.show();
  };
  addStripe(fineStripe,finePoint,9);addStripe(broadStripe,broadPoint,41);
  const auto flatPoint=stripePoint(dock.stats_->width()/2);
  QMap<int,QColor> flatPixels;
  auto blurPixels=[&](int amount){
   dock.settings_.setValue("blurAmount",amount);dock.refreshPanel();wait(400);
   const auto image=grab(QString(".qml-blur-%1").arg(amount));
   auto pixel=[&](const QPoint &point){
    const auto local=point-dock.badge_->pos();
    return image.pixelColor(qRound(local.x()*image.devicePixelRatio()),qRound(local.y()*image.devicePixelRatio()));
   };
   const int fine=pixel(finePoint).red(),broad=pixel(broadPoint).red();
   const auto flat=pixel(flatPoint);
   flatPixels.insert(amount,flat);
   std::fprintf(stdout,"Backdrop at %d%%: fine stripe %d, broad stripe %d, flat %s\n",
                amount,fine,broad,qPrintable(flat.name()));
   return qMakePair(fine,broad);
  };
  const auto sharp=blurPixels(0);
  blurPixels(1);blurPixels(5);
  const auto subtle=blurPixels(10),low=blurPixels(30),
             medium=blurPixels(50),high=blurPixels(75),soft=blurPixels(100);
  for(int amount:{1,5}){
   const auto base=flatPixels[0],sample=flatPixels[amount];
   checkState(std::abs(sample.red()-base.red())<=6 && std::abs(sample.green()-base.green())<=6 &&
              std::abs(sample.blue()-base.blue())<=6,
              "low blur does not brighten or tint a flat background");
  }
  checkState(sharp.first>=subtle.first && subtle.first>medium.first+30 &&
             low.first>medium.first+10 && medium.first>high.first+10,
             "low blur preserves fine detail and the midrange progressively softens it");
  checkState(medium.second>high.second+5 && high.second>soft.second+10,
             "higher blur settings continue softening broader details");
  fineStripe.hide();broadStripe.hide();dock.settings_.setValue("backgroundOpacity",100);
  dock.activateAction(1);dock.dismissAlert();wait(350);checkState(!IsWindowVisible(target.window),"Hide removes QML cards and glow");dock.activateAction(0);wait(350);checkState(IsWindowVisible(target.window),"Show restores QML panels immediately");
  dock.glowPreviewUntil_=0;dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("blurAmount",0);dock.settings_.setValue("showThumbnail",false);dock.settings_.setValue("panelWidth",320);dock.setEditMode(true);dock.refreshPanel();wait(250);const QPoint old=dock.badge_->pos(),start=dock.stats_->mapToGlobal(dock.stats_->rect().center());auto drag=[&](QString phase){dock.quick_->command({{"type","transform"},{"phase",phase},{"card","stats"},{"sx",start.x()},{"sy",start.y()},{"ex",start.x()+30},{"ey",start.y()+20}});};drag("begin");drag("move");checkState(dock.dragging_ && dock.badge_->pos()==old+QPoint(30,20),"QML window follows the pointer before release");drag("end");checkState(!dock.dragging_ && dock.badge_->pos()==old+QPoint(30,20),"QML drag commits with existing persistent placement controls");
  wait(180);
  target.window=nullptr;
  EnumWindows(find,reinterpret_cast<LPARAM>(&target));
  checkState(target.window && IsWindowVisible(target.window),"editable QML window is visible");
  const QPoint previousPointer=QCursor::pos();
  const QPoint grip=dock.badge_->pos()+dock.stats_->mapTo(dock.badge_,dock.stats_->rect().bottomRight()-QPoint(6,6));
  QCursor::setPos(grip);
  POINT pointer{};GetCursorPos(&pointer);
  const auto hit=WindowFromPoint(pointer);
  checkState(hit==target.window,"the native resize grip is hit-testable");
  auto button=[](DWORD action){INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=action;return SendInput(1,&input,sizeof(input))==1;};
  const int originalWidth=dock.badge_->width();
  RECT nativeBefore{};GetWindowRect(target.window,&nativeBefore);
  checkState(button(MOUSEEVENTF_LEFTDOWN),"send test mouse press");wait(160);
  const bool grabbed=dock.dragging_ && dock.resizing_;
  QCursor::setPos(grip+QPoint(35,8));wait(160);
  const bool resized=dock.badge_->width()>originalWidth;
  RECT nativeAfter{};GetWindowRect(target.window,&nativeAfter);
  button(MOUSEEVENTF_LEFTUP);wait(120);
  QCursor::setPos(previousPointer);
  if(!dock.quick_->diagnostic_.isEmpty())std::fprintf(stderr,"Renderer while testing backdrop and input: %s\n",dock.quick_->diagnostic_.constData());
  checkState(grabbed,"native QML resize grip reaches the placement backend");
  checkState(resized,"QML resize grip changes size before release");
  checkState(nativeAfter.right-nativeAfter.left>nativeBefore.right-nativeBefore.left,
             "the rendered native overlay follows the resize during the gesture");
  checkState(!dock.dragging_ && dock.settings_.value("panelScale").toInt()==4,"QML resize commits the custom size");
  dock.settings_.setValue("independentCards",true);dock.refreshPanel();const QPoint cardStart=dock.stats_->mapToGlobal(dock.stats_->rect().center());dock.quick_->command({{"type","transform"},{"card","stats"},{"sx",cardStart.x()},{"sy",cardStart.y()},{"ex",cardStart.x()+50},{"ey",cardStart.y()+40}});checkState(dock.settings_.value("cardPositions").toMap().contains("stats"),"QML individual-card drag preserves existing preset keys and snapping");dock.settings_.setValue("independentCards",false);dock.setEditMode(false);
  backdrop.hide();
  dock.openSettings();
  wait(650);
  Find settingsWindow{target.pid};
  EnumWindows([](HWND window,LPARAM data)->BOOL{
   auto *found=reinterpret_cast<Find*>(data);
   DWORD pid=0; GetWindowThreadProcessId(window,&pid);
   if(pid==found->pid && GetPropW(window,L"LookoutSettings"))found->window=window;
   return TRUE;
  },reinterpret_cast<LPARAM>(&settingsWindow));
  checkState(settingsWindow.window && IsWindowVisible(settingsWindow.window),"dock opens the QML settings window");
  const auto savedAlertIcon=dock.settings_.value("showAlertIcon");
  dock.settings_.setValue("showAlertIcon",false);
  dock.settings_.setValue("showInCaptures",false);
  dock.settings_.setValue("panelMode",0);
  dock.dismissAlert();
  const QJsonObject thumbnailPreview{{"action","previewThumbnailAlert"}};
  dock.quick_->settingsCommand(thumbnailPreview);
  wait(1400);
  const auto sample=dock.quick_->snapshot();
  QJsonObject thumbnailCard;
  for(const auto &entry:sample["cards"].toArray())
   if(entry.toObject()["id"]=="thumbnail")thumbnailCard=entry.toObject();
  checkState(sample["visible"].toBool() && !sample["public"].toBool() &&
             thumbnailCard["alertIcon"].toBool() && thumbnailCard["alertRole"]=="storage",
             "thumbnail preview shows a sample alert icon in the private overlay");
  checkState(!dock.thumbnailEnabled() && !dock.settings_.value("showAlertIcon").toBool() &&
             !dock.alertActive_ && dock.glowPreviewUntil_==0,
             "thumbnail preview leaves saved preferences and real alerts alone");
  dock.quick_->settingsCommand(thumbnailPreview);
  checkState(!dock.thumbnailAlertPreview_.isActive() && !dock.quick_->snapshot()["visible"].toBool(),
             "stopping thumbnail preview restores alert-only visibility");
  dock.thumbnailAlertPreview_.setInterval(50);
  dock.quick_->settingsCommand(thumbnailPreview);
  wait(150);
  checkState(!dock.thumbnailAlertPreview_.isActive() && !dock.quick_->snapshot()["visible"].toBool(),
             "thumbnail preview expires automatically");
  dock.thumbnailAlertPreview_.setInterval(10000);
  if(savedAlertIcon.isValid())dock.settings_.setValue("showAlertIcon",savedAlertIcon);
  else dock.settings_.remove("showAlertIcon");
  const auto savedTransition=dock.settings_.value("panelTransition");
  dock.settings_.setValue("panelTransition","fade");
  const QJsonObject transitionPreview{{"action","previewTransition"}};
  dock.quick_->settingsCommand(transitionPreview);
  wait(550);
  checkState(IsWindowVisible(target.window) && GetWindowDisplayAffinity(target.window,&affinity) &&
             affinity==WDA_EXCLUDEFROMCAPTURE,
             "transition preview reveals private panels even in alert-only mode");
  wait(1150);
  checkState(!IsWindowVisible(target.window),"transition preview plays the hide animation");
  wait(900);
  checkState(!dock.quick_->transitionPreviewActive() && !IsWindowVisible(target.window) && dock.panelMode()==0,
             "transition preview restores the original visibility and leaves preferences alone");
  if(savedTransition.isValid())dock.settings_.setValue("panelTransition",savedTransition);
  else dock.settings_.remove("panelTransition");
  dock.settings_.setValue("showInCaptures",true);
  dock.settings_.setValue("panelMode",1);
  dock.refreshPanel();
  dock.quick_->settingsCommand({{"action","dockButton"},{"key","toggleThumbnail"},{"value","add"}});
  checkState(dock.dockOrder_.last()=="toggleThumbnail","Dock settings adds the chosen action");
  dock.quick_->settingsCommand({{"action","dockButton"},{"key","toggleThumbnail"},{"value","up"}});
  checkState(dock.dockOrder_.at(dock.dockOrder_.size()-2)=="toggleThumbnail","Dock settings reorders the chosen action");
  dock.quick_->settingsCommand({{"action","dockButton"},{"value","reset"}});
  checkState(dock.dockOrder_==dock.defaultDockOrder(),"Dock settings restores the default actions");
  wchar_t settingsTitle[128]{};
  GetWindowTextW(settingsWindow.window,settingsTitle,128);
  checkState(QString::fromWCharArray(settingsTitle).trimmed().isEmpty(),"settings caption contains no visible text");
  const auto captionIcon=reinterpret_cast<HICON>(SendMessageW(settingsWindow.window,WM_GETICON,ICON_SMALL,0));
  ICONINFO iconInfo{};
  checkState(captionIcon && GetIconInfo(captionIcon,&iconInfo),"settings overrides the default caption icon");
  std::array<BYTE,64> iconMask{};
  const bool transparent=!iconInfo.hbmColor && GetBitmapBits(iconInfo.hbmMask,DWORD(iconMask.size()),iconMask.data())==LONG(iconMask.size()) &&
   std::count(iconMask.begin(),iconMask.end(),BYTE(0xff))==32 &&
   std::count(iconMask.begin(),iconMask.end(),BYTE(0))==32;
  DeleteObject(iconInfo.hbmMask);
  if(iconInfo.hbmColor)DeleteObject(iconInfo.hbmColor);
  checkState(transparent,"native settings caption has no visible icon");
  const auto windowIcon=[&]{
   const auto icon=reinterpret_cast<HICON>(SendMessageW(settingsWindow.window,WM_GETICON,ICON_BIG,0));
   return icon?QImage::fromHICON(icon):QImage{};
  };
  checkState(!windowIcon().isNull(),"settings supplies a large Windows icon");
  HICON executableIcon=nullptr;
  checkState(ExtractIconExW(reinterpret_cast<LPCWSTR>(executable.utf16()),0,&executableIcon,nullptr,1)==1 &&
             executableIcon && !QImage::fromHICON(executableIcon).isNull(),
             "renderer executable supplies an icon for the Windows shell fallback");
  DestroyIcon(executableIcon);
  dock.quick_->settingsCommand({{"action","set"},{"key","chartLines"},{"value","area"}});
  dock.quick_->settingsCommand({{"action","set"},{"key","chartDetail"},{"value",1}});
  for(const auto *style:{"obs","layered","minimal","studio","signal"}){
   dock.quick_->settingsCommand({{"action","set"},{"key","panelDesign"},{"value",style}});
   const auto colors=dock.quick_->snapshot();
   checkState(colors["design"]==style && colors["chartLines"]=="area" && colors["chartDetail"]==1,
              "palette changes preserve the line design and detail");
   if(QString(style)!="obs"){
    for(int theme:{1,2}){
     dock.quick_->settingsCommand({{"action","set"},{"key","logoTheme"},{"value",theme}});
     const auto palette=dock.quick_->panelPalette();
     const bool light=theme==1;
     const int surface=palette.color(QPalette::Window).lightness();
     const int text=palette.color(QPalette::WindowText).lightness();
     checkState(light?surface>190 && text<100:surface<100 && text>190,
                "each lookout palette has distinct light and dark card colors");
    }
   }
   wait(140);
  }
  auto set=[&](const QString &key,const QJsonValue &value){
   dock.quick_->settingsCommand({{"action","set"},{"key",key},{"value",value}});
  };
  set("panelDesign","minimal");
  set("logoTheme",2);
  wait(180);
  const auto darkIcon=windowIcon();
  const auto darkPalette=dock.quick_->panelPalette();
  set("backgroundOpacity",25);
  set("blurAmount",50);
  const auto effects=dock.quick_->snapshot();
  checkState(dock.quick_->panelPalette()==darkPalette && effects["logoDark"].toBool() &&
             effects["opacity"].toDouble()==.25 && effects["blurAmount"].toInt()==50,
             "opacity and blur updates preserve the dark palette and logo");
  auto surfacePixel=[&](const QString &name){
   wait(280);
   const auto image=grab(name);
   const auto point=dock.stats_->mapTo(dock.badge_,QPoint(dock.stats_->width()/2,dock.stats_->height()-5));
   return image.pixelColor(qRound(point.x()*image.devicePixelRatio()),qRound(point.y()*image.devicePixelRatio()));
  };
  const auto darkSurface=surfacePixel(".qml-dark-effects");
  set("logoTheme",1);
  const auto lightSurface=surfacePixel(".qml-light-effects");
  const auto lightIcon=windowIcon();
  checkState(!darkIcon.isNull() && !lightIcon.isNull() && darkIcon!=lightIcon &&
             reinterpret_cast<HICON>(SendMessageW(settingsWindow.window,WM_GETICON,ICON_SMALL,0))==captionIcon,
             "Windows icon follows Light and Dark while the settings caption stays blank");
  checkState(!dock.quick_->snapshot()["logoDark"].toBool() &&
             lightSurface.lightness()>darkSurface.lightness()+10,
             "light and dark palettes render visibly different cards at 25% opacity and 50% blur");
  set("backgroundOpacity",100);
  set("blurAmount",0);
  set("panelDesign","signal");
  set("logoTheme",2);
  dock.activateAction(4);
  checkState(!dock.metricsVisible_,"the stats shortcut hides the stats card");
  set("iconVisible/recording",true);
  checkState(dock.metricsVisible_ && !dock.stats_->isHidden(),
             "enabling a stat in Panels restores the stats card after the shortcut hides it");
  dock.quick_->settingsCommand({{"action","set"},{"key","graph/cpu"},{"value",1}});
  dock.quick_->settingsCommand({{"action","set"},{"key","graph/memory"},{"value",2}});
  dock.quick_->settingsCommand({{"action","set"},{"key","graph/disk-time"},{"value",1}});
  for(int mode:{5,4,0}){
   dock.quick_->settingsCommand({{"action","set"},{"key","presentation"},{"value",mode}});
   checkState(dock.displayMode()==mode,"QML accepts the three display modes");
   if(mode==0){
    const auto snapshot=dock.quick_->snapshot();
    QMap<QString,bool> styles;
    for(const auto &entry:snapshot["cards"].toArray()){
     const auto card=entry.toObject();
     if(card["type"]=="chart")styles.insert(card["icon"].toString(),card["compact"].toBool());
    }
    checkState(styles.contains("cpu") && styles["cpu"] && styles.contains("memory") && !styles["memory"] &&
               styles.contains("disk-time") && styles["disk-time"],"QML publishes mixed full and mini graphs");
   }else checkState(dock.stats_->isHidden() && dock.graphs_->isHidden(),"icon modes suspend readings and charts");
   wait(150);
  }
  format(dock,0);
  dock.refreshPanel();
  wait(200);
  for(const QString style:{"fade","flash","slide","blur","fall"}){
   dock.quick_->settingsCommand({{"action","set"},{"key","panelTransition"},{"value",style}});
   checkState(dock.quick_->snapshot()["transition"].toString()==style,"transition choice is saved and sent to the renderer");
   dock.activateAction(1);
   wait(80);
   checkState(IsWindowVisible(target.window),"animated hide keeps the card surface alive");
   if(style=="fall"){
    dock.activateAction(0);wait(1050);
    checkState(IsWindowVisible(target.window),"reversing a fall returns to the visible layout");
    dock.activateAction(1);
   }
   wait(1100);
   checkState(!IsWindowVisible(target.window),"animated hide finishes with the native window hidden");
   dock.activateAction(0);wait(850);
   checkState(IsWindowVisible(target.window),"animated show finishes with the window visible");
   RECT placed{};GetWindowRect(target.window,&placed);
   const qreal dpi=dock.badge_->devicePixelRatioF();
   checkState(std::abs(placed.right-placed.left-qRound(dock.badge_->width()*dpi))<=2 &&
              std::abs(placed.bottom-placed.top-qRound(dock.badge_->height()*dpi))<=2,
              "the animation canvas returns to the normal panel geometry");
  }
  dock.settings_.setValue("showInCaptures",false);dock.refreshPanel();wait(300);
  dock.activateAction(1);wait(90);
  checkState(IsWindowVisible(target.window) && GetWindowDisplayAffinity(target.window,&affinity) &&
             affinity==WDA_EXCLUDEFROMCAPTURE,"the falling exit stays capture-excluded");
  dock.settings_.setValue("showInCaptures",true);dock.refreshPanel();wait(100);
  checkState(!IsWindowVisible(target.window),"a privacy change immediately clears any outgoing frame");
  dock.quick_->settingsCommand({{"action","set"},{"key","panelTransition"},{"value","instant"}});
  dock.activateAction(0);wait(250);
  const auto oldPid=dock.quick_->process_.processId();dock.quick_->process_.kill();dock.quick_->process_.waitForFinished(1000);wait(2000);checkState(dock.quick_->connected() && dock.quick_->process_.processId()!=oldPid,"renderer restarts after an isolated failure without restarting OBS");if(!dock.quick_->diagnostic_.isEmpty())std::fprintf(stderr,"Renderer diagnostics: %s\n",dock.quick_->diagnostic_.constData());checkState(dock.quick_->diagnostic_.isEmpty(),"QML settings, designs and presentation switches produce no runtime warnings");std::puts("PASS: QML settings, scene thumbnail, capture privacy, blur, transforms, five designs, formats and renderer recovery");
#endif
  delete dock.quick_;dock.quick_=nullptr;dock.quickVisible_=false;dock.badge_->setProperty("quickModel",false);dock.glowPreviewUntil_=0;dock.dismissAlert();dock.settings_.clear();for(auto it=saved.cbegin();it!=saved.cend();++it)dock.settings_.setValue(it.key(),it.value());dock.refreshPanel();
 }
 static void realReplay(SentinelDock &dock){
#ifdef _WIN32
  SetDllDirectoryW(reinterpret_cast<LPCWSTR>(obsTestPath("bin/64bit").utf16()));
#endif
  auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
  for(const char *name:{"obs-x264","obs-ffmpeg"}){obs_module_t *module=nullptr;const QByteArray root=obsTestPath(QString("core/")+name).toUtf8();checkState(obs_open_module(&module,(root+"/"+name+".dll").constData(),(root+"/data").constData())==MODULE_SUCCESS && obs_init_module(module),"load official OBS encoders for isolated replay QA");}
  obs_audio_info audioInfo{};audioInfo.samples_per_sec=48000;audioInfo.speakers=SPEAKERS_STEREO;checkState(obs_reset_audio(&audioInfo),"initialize isolated silent audio");
  obs_source_info silenceInfo{};silenceInfo.id="lookout_qa_silence";silenceInfo.type=OBS_SOURCE_TYPE_INPUT;silenceInfo.output_flags=OBS_SOURCE_AUDIO;silenceInfo.get_name=[](void*){return "lookout QA silence";};silenceInfo.create=[](obs_data_t*,obs_source_t *source)->void*{return source;};silenceInfo.destroy=[](void*){};obs_register_source(&silenceInfo);
  auto *silence=obs_source_create("lookout_qa_silence","lookout QA silence",nullptr,nullptr);checkState(silence,"silent test source");obs_set_output_source(0,silence);
  float zeros[480]{};QTimer audioTimer;audioTimer.setInterval(10);QObject::connect(&audioTimer,&QTimer::timeout,[&]{obs_source_audio audio{};audio.data[0]=audio.data[1]=reinterpret_cast<uint8_t*>(zeros);audio.frames=480;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=48000;audio.timestamp=os_gettime_ns();obs_source_output_audio(silence,&audio);});audioTimer.start();
  auto *settings=obs_data_create();obs_data_set_string(settings,"preset","ultrafast");obs_data_set_string(settings,"rate_control","CBR");obs_data_set_int(settings,"bitrate",1000);obs_data_set_int(settings,"keyint_sec",1);auto *video=obs_video_encoder_create("obs_x264","lookout QA video",settings,nullptr);obs_data_release(settings);checkState(video,"real software video encoder");obs_encoder_set_preferred_video_format(video,VIDEO_FORMAT_I420);obs_encoder_set_video(video,obs_get_video());
  settings=obs_data_create();obs_data_set_int(settings,"bitrate",128);auto *audio=obs_audio_encoder_create("ffmpeg_aac","lookout QA audio",settings,0,nullptr);obs_data_release(settings);checkState(audio,"real software audio encoder");obs_encoder_set_audio(audio,obs_get_audio());
  settings=obs_data_create();obs_data_set_int(settings,"max_time_sec",3);obs_data_set_int(settings,"max_size_mb",8);replayTestOutput=obs_output_create("replay_buffer","lookout QA replay",settings,nullptr);obs_data_release(settings);checkState(replayTestOutput,"real replay buffer output");obs_output_set_video_encoder(replayTestOutput,video);obs_output_set_audio_encoder(replayTestOutput,audio,0);
  replayTesting=true;dock.updateReplay();checkState(obs_output_start(replayTestOutput),"start isolated replay buffer without saving a clip");wait(6200);dock.tick();const auto fill=dock.replayTracker_.sample();checkState(fill.known && fill.bytes>0 && fill.seconds>0 && fill.percent>0,"live replay callback tracks actual encoded packets");checkState(dock.lastOutput_==replayTestOutput && dock.encoderSampleValid_,"replay-only session participates in encoder health sampling");std::fprintf(stdout,"Replay integration: %.2fs, %llu bytes, %.0f%% fill\n",fill.seconds,static_cast<unsigned long long>(fill.bytes),fill.percent);
  obs_output_stop(replayTestOutput);wait(1000);checkState(!obs_output_active(replayTestOutput),"isolated replay buffer stops cleanly");replayTesting=false;dock.tick();checkState(!dock.replayOutput_ && !dock.replayTracker_.sample().known,"replay callback and tracked state detach after stop");obs_output_release(replayTestOutput);replayTestOutput=nullptr;obs_encoder_release(video);obs_encoder_release(audio);audioTimer.stop();obs_set_output_source(0,nullptr);obs_source_release(silence);
 }
 static void universalMetrics(SentinelDock &dock){
  QVariantMap saved;
  for(const auto &key:dock.settings_.allKeys())saved[key]=dock.settings_.value(key);
  dock.timer_.stop();dock.dismissAlert();dock.settings_.setValue("panelMode",1);
  dock.settings_.setValue("showThumbnail",false);format(dock,0);
  auto *diskGraph=dock.dialog_->findChild<QComboBox*>("graph_storage");
  auto *cpuGraph=dock.dialog_->findChild<QComboBox*>("graph_cpu");
  auto *bitrateGraph=dock.dialog_->findChild<QComboBox*>("graph_bitrate");
  auto *replayGraph=dock.dialog_->findChild<QComboBox*>("graph_replay");
  checkState(diskGraph && cpuGraph && bitrateGraph && replayGraph,"each numeric metric has a graph control");
  checkState(!dock.statNames_[SentinelDock::BitrateStat]->isHidden() && !dock.statNames_[SentinelDock::ReplayStat]->isHidden() &&
             dock.bitrates_->isHidden() && dock.replay_->isHidden(),"bitrate and replay readings share the stats card");
  diskGraph->setCurrentIndex(SentinelDock::GraphFull);
  cpuGraph->setCurrentIndex(SentinelDock::GraphMini);
  bitrateGraph->setCurrentIndex(SentinelDock::GraphMini);
  replayGraph->setCurrentIndex(SentinelDock::GraphFull);
  checkState(!dock.graphs_->isHidden() && dock.graphs_->metricRect(0).height()>dock.graphs_->metricRect(3).height() &&
             !dock.bitrates_->isHidden() && !dock.replay_->isHidden(),"mini and full graphs have independent card sizes");
  dock.dialog_->findChild<QCheckBox*>("metric_storage")->setChecked(false);
  checkState(dock.statNames_[SentinelDock::StorageStat]->isHidden() && !dock.graphs_->metricRect(0).isEmpty(),
             "turning off a reading preserves its graph");
  dock.setDisplayMode(5);
  checkState(dock.stats_->isHidden() && dock.graphs_->isHidden() && !dock.activeIcons_->isHidden(),"Icons uses selected metric glyphs");
  dock.setDisplayMode(0);
  checkState(!dock.graphs_->isHidden() && dock.graphs_->compact(3) && dock.statNames_[SentinelDock::StorageStat]->isHidden(),
             "returning to Custom restores the individual choices");
  format(dock,0);
  for(const auto &metric:SentinelDock::statDefinitions_)
   dock.settings_.setValue(QString(metric.id)=="bitrate"?"showBitrate":QString("iconVisible/")+metric.id,false);
  dock.refreshPanel();
  checkState(dock.stats_->isHidden() && dock.bitrates_->isHidden() && dock.replay_->isHidden() && dock.information_->isHidden(),
             "all readings and graphs off leaves no empty cards");
  dock.settings_.clear();
  for(auto it=saved.cbegin();it!=saved.cend();++it)dock.settings_.setValue(it.key(),it.value());
  dock.refreshPanel();dock.timer_.start(2000);
 }
 static void format(SentinelDock &dock,int mode){
  const int graph=mode==1 || mode==2?SentinelDock::GraphFull:mode==3?SentinelDock::GraphMini:SentinelDock::GraphOff;
  for(int i=0;i<HistoryGraphs::Count;++i)
   dock.settings_.setValue(QString("graph/")+HistoryGraphs::metrics[i].id,i<3?graph:0);
  dock.settings_.setValue("graph/bitrate",graph);dock.settings_.setValue("graph/replay",graph);
  dock.settings_.setValue("statsDisplay",mode==4 || mode==5?mode:0);
  dock.refreshPanel();
 }
 static void styleMatrix(SentinelDock &dock){
  QVariantMap saved;for(const auto &key:dock.settings_.allKeys())saved[key]=dock.settings_.value(key);dock.dismissAlert();dock.settings_.setValue("showThumbnail",false);dock.settings_.setValue("panelMode",1);dock.settings_.setValue("showInCaptures",true);dock.metricsVisible_=true;dock.settings_.setValue("independentCards",false);dock.settings_.setValue("panelScale",4);dock.settings_.setValue("glowEnabled",false);dock.settings_.setValue("blurAmount",0);dock.timer_.stop();const QString nativeSheet=qApp->styleSheet();qApp->setStyleSheet("QWidget {color: #ffffff; font-size: 14px;} QLabel {background: transparent;}");
  for(int style=0;style<9;++style)for(int mode:{0,1,3,4,5})for(int width:{160,240,480})for(double scale:{.65,1.5}){
   dock.settings_.setValue("graphAppearance",style);format(dock,mode);dock.settings_.setValue("panelWidth",width);dock.settings_.setValue("customScale",scale);dock.refreshPanel();QApplication::processEvents();
   checkState(dock.statNames_[0]->palette().color(QPalette::WindowText)==dock.badge_->palette().color(QPalette::WindowText),"OBS global text styles cannot override overlay contrast");
   for(auto *widget:dock.movableCards()){const QRect bounds(widget->mapTo(dock.badge_,QPoint()),widget->size());if(!dock.badge_->rect().adjusted(-1,-1,1,1).contains(bounds))std::fprintf(stderr,"style=%d mode=%d width=%d scale=%.2f card=%s root=%dx%d card=%dx%d at %d,%d\n",style,mode,width,scale,qPrintable(widget->objectName()),dock.badge_->width(),dock.badge_->height(),widget->width(),widget->height(),bounds.x(),bounds.y());checkState(dock.badge_->rect().adjusted(-1,-1,1,1).contains(bounds),"style/mode/scale switches keep every card inside its panel");}
   for(auto *widget:QVector<QWidget*>{dock.graphs_,dock.bitrates_,dock.replay_})if(!widget->isHidden())checkState(widget->height()+1>=widget->heightForWidth(widget->width()),"each graph keeps enough height after changing styles and compact modes");
  }
  qApp->setStyleSheet(nativeSheet);dock.settings_.clear();for(auto it=saved.cbegin();it!=saved.cend();++it)dock.settings_.setValue(it.key(),it.value());dock.syncVisibilityControls();dock.refreshPanel();dock.timer_.start(2000);
 }
 static void iconMode(SentinelDock &dock){
  QVariantMap saved;for(const auto &key:dock.settings_.allKeys())saved[key]=dock.settings_.value(key);const bool rec=dock.recordingActive_,stream=dock.streamingActive_;
  dock.setEditMode(false);dock.dismissAlert();dock.settings_.setValue("animationStyle",0);dock.settings_.setValue("statsDisplay",4);dock.settings_.setValue("showThumbnail",false);dock.metricsVisible_=true;dock.settings_.setValue("panelMode",1);dock.settings_.setValue("showInCaptures",true);dock.recordingActive_=dock.streamingActive_=false;dock.refreshPanel();checkState(!dock.badge_->isVisible(),"idle icon mode has no empty panel");
  dock.recordingActive_=true;dock.refreshPanel();checkState(!dock.activeIcons_->isHidden() && dock.activeIcons_->roles()==QStringList{"recording"},"icon mode appears automatically for active recording");
  dock.streamingActive_=true;dock.refreshPanel();checkState(dock.activeIcons_->iconCount()==2 && dock.stats_->isHidden() && dock.graphs_->isHidden() && dock.bitrates_->isHidden() && dock.previewCard_->isHidden() && dock.badgeText_->isHidden(),"icon mode excludes normal cards and inactive stats");
  dock.notify("Network interruption","Connection lost",true);checkState(dock.activeIcons_->roles().contains("alert:network"),"network alert adds its relevant icon");dock.dismissAlert();checkState(dock.activeIcons_->iconCount()==2,"dismiss keeps active status icons");
  dock.recordingActive_=dock.streamingActive_=false;dock.notify("Storage full","No recording space",true);checkState(dock.activeIcons_->roles()==QStringList{"alert:storage"},"idle alert displays the relevant icon alone");dock.activateAction(2);checkState(!dock.badge_->isVisible() && dock.alerts_.isEmpty(),"Disable panel suppresses the icon overlay and alerts");
  dock.settings_.setValue("statsDisplay",5);dock.settings_.setValue("panelMode",1);dock.refreshPanel();checkState(dock.activeIcons_->iconCount()==SentinelDock::StatCount && dock.activeIcons_->roles().contains("replay") && !dock.activeIcons_->isHidden(),"all-icons mode retains every status including replay while idle");dock.notify("Storage full","No space",true);checkState(dock.activeIcons_->iconCount()==SentinelDock::StatCount && dock.activeIcons_->toolTip().contains("Storage full"),"all-icons alerts highlight the existing relevant glyph without duplicating it");if(!qEnvironmentVariableIsEmpty("SENTINEL_GPU_CAPTURE")){QApplication::processEvents();checkState(dock.badge_->grab().save(qEnvironmentVariable("SENTINEL_GPU_CAPTURE")+".all-icons.png"),"all-icons native layout screenshot");}dock.dismissAlert();dock.replayActive_=true;dock.settings_.setValue("statsDisplay",4);dock.refreshPanel();checkState(dock.activeIcons_->roles()==QStringList{"replay"},"active-icons mode includes the running replay buffer");dock.replayActive_=false;
  for(int mode:{4,5}){format(dock,mode);dock.settings_.setValue("panelMode",1);dock.recordingActive_=dock.streamingActive_=dock.cameraActive_=dock.replayActive_=true;dock.notify("Storage full","No space",true);dock.notify("Encoder issue","Skipped frames",true);dock.notify("Network issue","Disconnected",true);dock.refreshPanel();
   for(const QString id:{"recording","storage","streaming","encoder","camera","network","cpu","memory","disk-time","bitrate","replay"}){auto *toggle=dock.dialog_->findChild<QCheckBox*>("metric_"+id);checkState(toggle && toggle->isEnabled(),"each stat has a toggle in both icon-only modes");toggle->setChecked(false);checkState(!dock.activeIcons_->roles().contains(id) && !dock.activeIcons_->roles().contains("alert:"+id),"disabled stat icons stay hidden even during their alerts");toggle->setChecked(true);}
   for(const QString id:{"recording","storage","streaming","encoder","camera","network","cpu","memory","disk-time","bitrate","replay"})dock.dialog_->findChild<QCheckBox*>("metric_"+id)->setChecked(false);checkState(dock.activeIcons_->iconCount()==0 && dock.activeIcons_->isHidden(),"no empty icon panel remains when every stat is disabled");for(const QString id:{"recording","storage","streaming","encoder","camera","network","cpu","memory","disk-time","bitrate","replay"})dock.dialog_->findChild<QCheckBox*>("metric_"+id)->setChecked(true);dock.dismissAlert();
  }
  dock.cameraActive_=dock.replayActive_=false;dock.dismissAlert();dock.recordingActive_=rec;dock.streamingActive_=stream;dock.settings_.clear();for(auto it=saved.cbegin();it!=saved.cend();++it)dock.settings_.setValue(it.key(),it.value());dock.syncVisibilityControls();dock.refreshPanel();
 }
 static void nativePanels(SentinelDock &dock,const QString &path){
#ifdef _WIN32
  SetDllDirectoryW(reinterpret_cast<LPCWSTR>(obsTestPath("bin/64bit").utf16()));
#endif
  auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
  dock.dialog_->hide();dock.setEditMode(false);dock.settings_.setValue("animationStyle",0);dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("showThumbnail",true);dock.metricsVisible_=true;format(dock,0);dock.settings_.setValue("showBitrate",true);dock.settings_.setValue("panelMode",1);dock.settings_.setValue("blurAmount",0);dock.settings_.setValue("backgroundOpacity",100);dock.settings_.setValue("customPosition",true);dock.settings_.setValue("positionX",180);dock.settings_.setValue("positionY",160);dock.settings_.setValue("thumbnailPosition",1);dock.settings_.setValue("panelScale",4);dock.settings_.setValue("customScale",1);dock.settings_.setValue("panelWidth",336);dock.refreshPanel();wait(250);
  auto capture=[&](QString suffix){const QRect bounds=dock.badge_->geometry();auto image=QGuiApplication::primaryScreen()->grabWindow(0,bounds.x(),bounds.y(),bounds.width(),bounds.height());checkState(!image.isNull() && image.save(path+suffix+".png"),"native GPU panel capture");return image.toImage();};
  auto hasVideo=[&]{const QPoint center=dock.preview_->mapToGlobal(dock.preview_->rect().center());const auto image=QGuiApplication::primaryScreen()->grabWindow(0,center.x()-2,center.y()-2,4,4).toImage();const auto color=image.pixelColor(1,1);return color.blue()>150 && color.green()>50 && color.red()<80;};
  checkState(hasVideo(),"real native GPU thumbnail renders the known test output");capture(".baseline");const auto stable=dock.visibleCardRects();
  for(int i=0;i<3;++i){dock.settings_.setValue("panelMode",0);dock.hidePanel();checkState(!dock.badge_->isVisible(),"native panels hide immediately");dock.settings_.setValue("panelMode",1);dock.refreshPanel();wait(60);checkState(dock.badge_->isVisible() && dock.visibleCardRects()==stable && hasVideo(),"native panels show immediately with live video and stable geometry");}
  QWidget backdrop;backdrop.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);backdrop.setStyleSheet("background: #123726;");backdrop.setGeometry(QGuiApplication::primaryScreen()->geometry());backdrop.show();
  dock.settings_.setValue("animationStyle",0);dock.settings_.setValue("glowEnabled",true);dock.settings_.setValue("glowCriticalColor","#ff3b4d");dock.settings_.setValue("glowSpread",24);dock.glowPreviewUntil_=dock.glowClock_.elapsed()+5000;dock.glowPreviewRisk_=1;dock.glowPreviewMetric_=1;dock.refreshPanel();dock.badge_->raise();dock.glowPhase_=3.14159265;dock.updateGlow();dock.glowTimer_.stop();wait(100);checkState(!dock.glow_->isWindow() && dock.glow_->window()==dock.badge_,"glow has no independently capturable top-level window");capture(".public-glow");
  const QPoint halo=dock.stats_->mapToGlobal(QPoint(-6,dock.stats_->height()/2));auto haloPixel=[&]{return QGuiApplication::primaryScreen()->grabWindow(0,halo.x(),halo.y(),2,2).toImage().pixelColor(0,0);};checkState(haloPixel().red()>haloPixel().green(),"public capture contains the red halo outside the card");
#ifdef _WIN32
  dock.settings_.setValue("showInCaptures",false);dock.refreshPanel();wait(150);DWORD affinity=0;checkState(GetWindowDisplayAffinity(reinterpret_cast<HWND>(dock.badge_->winId()),&affinity) && affinity==WDA_EXCLUDEFROMCAPTURE,"panel and glow use the same private capture surface");capture(".private-glow");checkState(haloPixel()==QColor("#123726"),"desktop capture excludes the complete glow and shows the background beneath it");
#endif
  dock.glowPreviewUntil_=0;dock.settings_.setValue("showThumbnail",false);dock.badge_->hide();
 }
 static void qaSettings(SentinelDock &dock){
  QVariantMap saved;for(const auto &key:dock.settings_.allKeys())saved[key]=dock.settings_.value(key);const bool rec=dock.recordingActive_,stream=dock.streamingActive_;const auto recRate=dock.recordingBitrate_,streamRate=dock.streamingBitrate_;dock.settings_.setValue("animationStyle",0);dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("panelMode",1);
  auto *record=dock.dialog_->findChild<QCheckBox*>("metric_recording"),*live=dock.dialog_->findChild<QCheckBox*>("metric_streaming"),*card=dock.dialog_->findChild<QCheckBox*>("metric_bitrate");checkState(record && live && card,"bitrate panel has dedicated card/recording/stream controls");
  format(dock,1);dock.recordingActive_=dock.streamingActive_=true;dock.recordingBitrate_=12000000;dock.streamingBitrate_=6000000;record->setChecked(false);live->setChecked(false);dock.refreshPanel();checkState(dock.bitrates_->hasActiveOutputs(),"bitrate graph follows outputs independently of status readings");record->setChecked(true);live->setChecked(true);
  dock.graphs_->append({240000,{1,2,3}});dock.updateBitrateCard();dock.dialog_->findChild<QPushButton*>("clearHistory")->click();checkState(dock.graphs_->sampleCount()==0 && dock.bitrates_->sampleCount()==0,"Clear history clears metrics and bitrate together");
  dock.settings_.setValue("thumbnailPosition",2);dock.savePreset("QA layout");dock.settings_.setValue("thumbnailPosition",3);checkState(dock.loadPreset("QA layout") && dock.settings_.value("thumbnailPosition").toInt()==2 && dock.dialog_->findChild<QComboBox*>("thumbnailPosition")->currentIndex()==2,"presets restore thumbnail arrangement and its control");
  dock.settings_.setValue("untilDismiss",false);dock.settings_.setValue("seconds",2);dock.notify("QA critical","test",true);checkState(dock.dismiss_.interval()==2000,"configured short critical duration is respected");dock.settings_.setValue("untilDismiss",true);dock.notify("QA retained","test",true);dock.dialog_->findChild<QPushButton*>("dismissCurrentAlerts")->click();checkState(dock.alerts_.isEmpty() && !dock.alertActive_,"retained notifications have a reachable dismiss action");
  for(int mode:{0,4,5}){
   dock.settings_.setValue("showThumbnail",true);format(dock,mode);dock.refreshPanel();
   checkState(dock.thumbnailEnabled() && !dock.previewCard_->isHidden(),"enabled thumbnail survives every display mode");
   dock.settings_.setValue("showThumbnail",false);dock.refreshPanel();
   checkState(!dock.thumbnailEnabled() && dock.previewCard_->isHidden(),"disabled thumbnail stays hidden in every display mode");
  }
  format(dock,1);dock.refreshPanel();
  for(const char *key:{"graph_storage","graph_encoder","graph_network"})dock.dialog_->findChild<QComboBox*>(key)->setCurrentIndex(0);checkState(dock.graphs_->isHidden(),"no empty graph card is drawn with every chart off");
  dock.dialog_->findChild<QCheckBox*>("independentCards")->setChecked(true);dock.settings_.setValue("thumbnailWidth",120);dock.settings_.setValue("panelWidth",400);dock.refreshPanel();checkState(dock.previewCard_->width()==136 && !dock.dialog_->findChild<QComboBox*>("thumbnailPosition")->isEnabled(),"independent thumbnail respects width and grouped-position control is disabled");
  const double previousScale=dock.panelScale();const int previousWidth=dock.stats_->width();const int previousReplay=dock.replay_->width();QWidget *resize=dock.stats_;const QPointF corner=resize->rect().bottomRight()-QPoint(2,2),global=resize->mapToGlobal(corner.toPoint());QMouseEvent press(QEvent::MouseButtonPress,corner,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(resize,&press);checkState(dock.resizing_,"card corner exposes a resize transform while settings is open");QMouseEvent move(QEvent::MouseMove,corner+QPointF(40,20),global+QPointF(40,20),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(resize,&move);QMouseEvent release(QEvent::MouseButtonRelease,corner+QPointF(40,20),global+QPointF(40,20),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(resize,&release);checkState(!dock.resizing_ && !dock.dragging_ && dock.panelScale()==previousScale && dock.stats_->width()>previousWidth && dock.replay_->width()==previousReplay,"individual resize changes only the selected card width");
  dock.settings_.setValue("offsetX",10000);dock.settings_.setValue("offsetY",10000);dock.place(dock.badge_,"");checkState(dock.cardGeometry_["stats"].width()>=160,"large insets cannot collapse card widths");
  dock.settings_.clear();for(auto it=saved.cbegin();it!=saved.cend();++it)dock.settings_.setValue(it.key(),it.value());dock.settings_.sync();dock.recordingActive_=rec;dock.streamingActive_=stream;dock.recordingBitrate_=recRate;dock.streamingBitrate_=streamRate;for(auto *box:dock.dialog_->findChildren<QCheckBox*>())if(saved.contains(box->objectName())){QSignalBlocker guard(box);box->setChecked(saved.value(box->objectName()).toBool());}dock.syncVisibilityControls();dock.syncGeometryControls();dock.refreshPanel();
 }
 static void exercise(SentinelDock &dock){
  const char *dockButtons[]={"showPanels","hidePanels","cycleDefaultPositions","cyclePositionPresets","displayMode","lookoutSettings"};
  for(const char *name:dockButtons){auto *button=dock.findChild<QPushButton*>(name);checkState(button && button->text().isEmpty() && !button->toolTip().isEmpty() && !button->accessibleName().isEmpty(),"dock is icon-only with accessible action labels");}
  for(const QSize size:{QSize(237,36),QSize(132,68),QSize(100,68),QSize(68,100),QSize(43,196),QSize(237,36)}){
   dock.resize(size);dock.show();QApplication::processEvents();
   checkState(dock.minimumHeight()==size.height(),"dock minimum height follows its current width");
   const int columns=qBound(1,(size.width()-4)/32,6);
   const int left=(size.width()-(columns*32-4))/2;
   QList<QRect> placed;
   for(const char *name:dockButtons){
    auto *button=dock.findChild<QPushButton*>(name);
    const QRect rect(button->mapTo(&dock,QPoint()),button->size());
    checkState(dock.rect().contains(rect),"dock actions fit horizontal, wrapped and narrow vertical slots");
    checkState(rect.topLeft()==QPoint(left+(placed.size()%columns)*32,4+(placed.size()/columns)*32),
               "each dock action shares the centered grid and uniform spacing");
    for(const auto &other:placed)checkState(!other.intersects(rect),"dock action slots do not overlap");
    placed.append(rect);
   }
  }
  dock.editDockOrder("lookoutSettings","remove");
  dock.editDockOrder("toggleThumbnail","add");
  dock.editDockOrder("toggleThumbnail","add");
  dock.editDockOrder("toggleThumbnail","up");
  checkState(dock.dockOrder_.size()==6 && dock.dockOrder_[4]=="toggleThumbnail" &&
             dock.dockButtons_["lookoutSettings"]->isHidden(),"dock actions can be added once, removed, and reordered");
  QSettings persisted(dock.settings_.fileName(),QSettings::IniFormat);
  checkState(persisted.value("dockButtons").toStringList()==dock.dockOrder_,"dock order is saved immediately");
  dock.resize(100,68);QApplication::processEvents();
  checkState(dock.dockButtons_["toggleThumbnail"]->pos()==QPoint(36,36),"custom order controls visual placement");
  const bool thumbnail=dock.thumbnailEnabled();
  dock.dockButtons_["toggleThumbnail"]->click();
  checkState(dock.thumbnailEnabled()!=thumbnail &&
             dock.dockButtons_["toggleThumbnail"]->isChecked()==dock.thumbnailEnabled(),"optional toggle reflects the live setting");
  dock.dockButtons_["toggleThumbnail"]->click();
  for(const auto &id:QStringList(dock.dockOrder_))dock.editDockOrder(id,"remove");
  persisted.sync();
  checkState(dock.dockOrder_.isEmpty() && persisted.contains("dockButtons") &&
             persisted.value("dockButtons").toStringList().isEmpty(),"an intentionally empty dock is retained");
  dock.editDockOrder({},"reset");
  checkState(dock.dockOrder_==dock.defaultDockOrder(),"restoring defaults shows the original six actions");
  dock.hide();
  for(const char *theme:{"light","dark"})for(const char *name:{"show","hide","settings","recording","streaming","storage","encoder","network","virtual-camera","thumbnail","stats","graphs","layout","alerts","hotkeys","about","cpu","memory","disk-time"}){const QIcon glyph(QString(":/lookout/icons/%1/%2.svg").arg(theme,name));checkState(!glyph.isNull() && !glyph.pixmap(24,24).isNull(),"all approved light and dark icons are embedded and renderable");}
  checkState(dock.styleSheet().isEmpty() && !dock.badge_->styleSheet().isEmpty(),"dock stays native while custom overlay styles are scoped");
  dock.dialog_->resize(980,720);
  auto *tabs=dock.dialog_->findChild<QTabWidget*>();
  for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);QApplication::processEvents();checkState(dock.editMode_,"card dragging stays enabled without extra handle boxes");
   auto *area=qobject_cast<QScrollArea*>(tabs->widget(i));checkState(area->verticalScrollBar()->maximum()==0 && area->horizontalScrollBar()->maximum()==0,"all tab controls fit at desktop size without scrolling");}
  tabs->setCurrentIndex(0);
  auto *logoMode=dock.dialog_->findChild<QComboBox*>("logoTheme");checkState(logoMode && logoMode->count()==3,"light dark system logo modes");
  logoMode->setCurrentIndex(1);checkState(!dock.brandingDark_ && dock.settings_.value("logoTheme").toInt()==1 && !dock.dialog_->windowIcon().isNull(),"light logo applies and saves throughout settings");const auto lightKey=dock.dialog_->findChild<QLabel*>("lookoutBadge")->pixmap().cacheKey();
  logoMode->setCurrentIndex(2);checkState(dock.brandingDark_ && dock.dialog_->findChild<QLabel*>("lookoutBadge")->pixmap().cacheKey()!=lightKey && !dock.findChild<QPushButton*>("lookoutSettings")->icon().isNull(),"dark logo updates About and dock settings button");logoMode->setCurrentIndex(0);checkState(dock.brandingDark_==dock.darkLogo(),"system logo resolves current operating system preference");
  const auto brandLight=LookoutBrand::badge(false,256).toImage(),brandDark=LookoutBrand::badge(true,256).toImage();checkState(brandLight.pixelColor(0,0).alpha()==0 && brandDark.pixelColor(0,0).alpha()==0,"arch logo preserves transparent outside corners");checkState(brandLight.pixelColor(128,244).lightness()<100 && brandDark.pixelColor(128,244).lightness()>180,"connected foreground follows light and dark identity palettes");const QColor window=brandLight.pixelColor(137,132);checkState(window==QColor("#7b6971"),"light-mode windows match the rear mountain without sky slivers");const QColor core=brandLight.pixelColor(96,106);checkState(core.red()>core.green()*1.5 && core.red()>core.blue()*1.5,"tally retains a predominantly red recording-light identity");const auto retina=LookoutBrand::badge(true,64,2);checkState(retina.width()==128 && retina.devicePixelRatio()==2,"logo renders at physical display density");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT")){const QString path=qEnvironmentVariable("SENTINEL_SCREENSHOT");checkState(brandLight.save(path+".brand-light.png") && brandDark.save(path+".brand-dark.png"),"production logo previews");LookoutBrand::wordmark(false,QSize(600,160),2).save(path+".wordmark-light.png");LookoutBrand::wordmark(true,QSize(600,160),2).save(path+".wordmark-dark.png");for(int size:{16,24,32,64}){LookoutBrand::badge(false,size,2).save(path+QString(".brand-light-%1.png").arg(size));LookoutBrand::badge(true,size,2).save(path+QString(".brand-dark-%1.png").arg(size));}}
  for(int i=0;i<5;++i){auto *button=dock.dialog_->findChild<QPushButton*>(QString("sizePreset%1").arg(i));checkState(button && !button->menu(),"size presets are plain buttons");}auto *custom=dock.dialog_->findChild<QSpinBox*>("customSizePercent");checkState(custom!=nullptr,"one editable custom size field");const int priorWidth=dock.settings_.value("panelWidth").toInt();custom->setValue(125);const int customWidth=dock.settings_.value("panelWidth").toInt();dock.dialog_->findChild<QPushButton*>("sizePreset0")->click();dock.dialog_->findChild<QPushButton*>("sizePreset4")->click();checkState(dock.settings_.value("panelWidth").toInt()==customWidth && custom->value()==125,"custom size survives switching to a standard preset and back");dock.settings_.setValue("panelWidth",priorWidth);dock.settings_.setValue("panelScale",4);dock.settings_.setValue("customScale",1);dock.refreshPanel();auto *diskWarning=dock.dialog_->findChild<QDoubleSpinBox*>("gib");checkState(diskWarning && diskWarning->minimum()>0,"storage warning must be greater than zero");checkState(!dock.dialog_->findChild<QWidget*>("storageApproach") && !dock.dialog_->findChild<QWidget*>("qualityApproach"),"percentage based approach settings are removed");
  auto render=[](double pulse,double intensity,double spread){QImage image(300,180,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);SentinelVisuals::paintGlow(painter,QRectF(60,60,180,60),QColor("#ffd43b"),pulse,intensity,spread);painter.end();return image;};
  const auto bright=render(1,1,32),dim=render(0.15,1,32),narrow=render(1,1,8),off=render(1,0,32);
  checkState(bright.pixelColor(61,61).alpha()>0,"glow continues smoothly through rounded corner cutouts");
  checkState(bright.pixelColor(150,90).alpha()==0,"glow never paints over panel contents");
  checkState(bright.pixelColor(150,35).alpha()>0 && dim.pixelColor(150,35).alpha()==0,"glow expands with brightness");
  checkState(narrow.pixelColor(150,35).alpha()==0 && off.pixelColor(150,59).alpha()==0,"spread and intensity affect rendered glow");
  auto *spread=dock.findChild<QSpinBox*>("glowSpread");auto *intensity=dock.findChild<QSpinBox*>("glowIntensity");auto *speed=dock.findChild<QDoubleSpinBox*>("glowSpeed");
  checkState(spread && intensity && speed,"live glow tuning controls available");spread->setValue(40);intensity->setValue(150);speed->setValue(1.5);
  checkState(dock.glowPadding()==42 && dock.settings_.value("glowIntensity").toInt()==150 && std::abs(dock.glowPeriod(0)-4000/1.5)<0.01,"tuning applies immediately");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT")){
   QImage demo(300,180,QImage::Format_ARGB32_Premultiplied);demo.fill(QColor("#16131e"));QPainter painter(&demo);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor("#303030"));painter.drawRoundedRect(QRectF(60,60,180,60),12,12);SentinelVisuals::paintGlow(painter,QRectF(60,60,180,60),QColor("#ffd43b"),1,1,32);painter.end();checkState(demo.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".glow.png"),"rounded glow preview");
  }
  auto *shortcut=dock.findChild<QLineEdit*>("hotkeyEditor0");checkState(shortcut!=nullptr,"direct hotkey field available");
  QKeyEvent shortcutEvent(QEvent::KeyPress,Qt::Key_F8,Qt::ControlModifier|Qt::ShiftModifier,0,obs_key_to_virtual_key(OBS_KEY_F8),0);QApplication::sendEvent(shortcut,&shortcutEvent);
  auto *beforeRelease=obs_hotkey_save(dock.keys_[0]);checkState(obs_data_array_count(beforeRelease)==0,"binding waits until assigned key released");obs_data_array_release(beforeRelease);
  QKeyEvent shortcutRelease(QEvent::KeyRelease,Qt::Key_F8,Qt::ControlModifier|Qt::ShiftModifier,0,obs_key_to_virtual_key(OBS_KEY_F8),0);QApplication::sendEvent(shortcut,&shortcutRelease);QEventLoop keyLoop;QTimer::singleShot(150,&keyLoop,&QEventLoop::quit);keyLoop.exec();
  auto *saved=obs_hotkey_save(dock.keys_[0]);checkState(obs_data_array_count(saved)==1 && !shortcut->text().isEmpty(),"shortcut editor applies to real OBS binding");obs_data_array_release(saved);
  obs_key_combination_t external={INTERACT_ALT_KEY,OBS_KEY_F9};obs_hotkey_load_bindings(dock.keys_[0],&external,1);const QString before=shortcut->text();dock.refreshHotkeyEditors();checkState(shortcut->text()!=before,"external OBS hotkey edits reflected in settings");
  checkState(dock.dialog_->findChildren<QLineEdit*>(QRegularExpression("^hotkeyEditor[0-7]$")).size()==8,"exactly eight plugin shortcut editors");
  checkState(dock.dialog_->findChildren<QLineEdit*>(QRegularExpression("^coreHotkey")).isEmpty(),"native OBS shortcut editors absent");
  checkState(!config_get_string(obs_frontend_get_profile_config(),"Hotkeys","OBSBasic.StartRecording"),"native OBS bindings untouched");
  dock.setHotkeyBinding(1,{INTERACT_ALT_KEY,OBS_KEY_F11});dock.findChild<QPushButton*>("clearHotkey1")->click();saved=obs_hotkey_save(dock.keys_[1]);checkState(obs_data_array_count(saved)==0,"Clear removes OBS binding");obs_data_array_release(saved);
  const auto smooth=HistoryGraphs::curve({QPointF(0,5),QPointF(10,1),QPointF(20,7),QPointF(30,2)},true);
  for(int i=0;i<=1000;++i){const auto point=smooth.pointAtPercent(i/1000.0);checkState(point.y()>=1-0.001 && point.y()<=7+0.001,"smooth graph curves do not invent overshooting values");}
  auto *preset=dock.findChild<QComboBox*>("graphAppearance");checkState(preset && preset->count()==9,"nine panel design presets including lookout");preset->setCurrentIndex(2);checkState(dock.graphs_->appearance()==2 && dock.settings_.value("graphAppearance").toInt()==2,"graph preset applies and saves immediately");checkState(dock.badge_->property("panelDesign").toInt()==2 && dock.badge_->palette().color(QPalette::WindowText)==QColor("#eae8f5"),"design applies to entire panel");checkState(dock.dialog_->findChild<QTabWidget*>("appearanceSections")!=nullptr,"graphs belong to Appearance");preset->setCurrentIndex(8);auto *theme=dock.dialog_->findChild<QComboBox*>("logoTheme");theme->setCurrentIndex(2);checkState(dock.badge_->property("panelDesign").toInt()==8 && dock.badge_->palette().color(QPalette::WindowText)==QColor("#fff0cf") && dock.graphs_->appearance()==8,"lookout night theme applies warm icon colors throughout");theme->setCurrentIndex(1);checkState(dock.badge_->palette().color(QPalette::WindowText)==QColor("#171820"),"lookout day theme follows the logo theme setting");
  preset->setCurrentIndex(0);
  checkState(!theme->isEnabled() && dock.darkLogo() && dock.settings_.value("logoTheme").toInt()==1,
             "Follow OBS uses the OBS theme and preserves the saved light appearance");
  preset->setCurrentIndex(8);
  checkState(theme->isEnabled() && !dock.darkLogo(),"leaving Follow OBS restores the saved appearance");
  preset->setCurrentIndex(2);
  HistoryGraphs history;history.configure(300,{true,true,true},{20,2,2});
  for(int i=0;i<=600;++i)history.append({qint64(i)*2000,{800-i*0.05,i%9==0?NAN:double(i%5),double(i%7)}});
  checkState(history.sampleCount()<=452 && history.samples().front().timeMs>=1200000-902000,"graph history is bounded to fifteen minutes");
  const auto retained=history.sampleCount();history.append({1200000,{1,1,1}});checkState(history.sampleCount()==retained,"rapid duplicate samples are ignored");
  history.append({1202000,{799,NAN,NAN}});checkState(std::isnan(history.samples().back().values[1]),"unavailable metric remains a history gap");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT")){history.resize(600,history.heightForWidth(600));checkState(history.grab().save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".graphs.png"),"labeled graph rendering");for(int preset=0;preset<9;++preset){history.setAppearance(preset);history.resize(600,history.heightForWidth(600));checkState(history.grab().save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+QString(".preset%1.png").arg(preset)),"graph preset rendering");}}
  history.setAppearance(1);const int compactGraphHeight=history.heightForWidth(600);history.setAppearance(3);checkState(history.heightForWidth(600)==compactGraphHeight,"all designs default to the same single-column arrangement");
  history.clearHistory();checkState(history.sampleCount()==0,"graph history can be cleared");
  checkState(dock.findChild<QComboBox*>("graph_cpu") && dock.findChild<QComboBox*>("presentation"),"graph sizes are configured per metric");format(dock,1);
  checkState(!dock.stats_->isHidden() && !dock.graphs_->isHidden(),"graphs can accompany text stats");format(dock,2);
  checkState(!dock.stats_->isHidden() && !dock.graphs_->isHidden(),"graphs always accompany the selected readings");format(dock,0);
  format(dock,3);dock.refreshPanel();
  checkState(dock.graphs_->compact() && dock.statsLayout_->indexOf(dock.statIcons_[SentinelDock::DiskTimeStat])>=0 && dock.previewCard_->isHidden(),"mini graphs retain the selected readings and respect the thumbnail setting");
  checkState(!dock.statNames_[0]->isHidden(),"mini graphs can accompany the readings card");
  dock.recordingActive_=true;dock.streamingActive_=true;dock.recordingBitrate_=12000000;dock.streamingBitrate_=6000000;dock.updateBitrateCard();dock.refreshPanel();
  checkState(!dock.bitrates_->isHidden() && dock.bitrates_->compact() && dock.visibleCardRects().contains("bitrate"),"combined bitrate card participates in compact panel geometry");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT")){dock.settings_.setValue("panelWidth",240);dock.settings_.setValue("panelMode",1);dock.refreshPanel();QApplication::processEvents();QImage mini(dock.badge_->size(),QImage::Format_ARGB32_Premultiplied);mini.fill(Qt::transparent);dock.badge_->render(&mini,QPoint(),QRegion(dock.badge_->rect()),QWidget::DrawWindowBackground|QWidget::DrawChildren|QWidget::IgnoreMask);checkState(mini.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".gaming.png"),"compact gaming overlay preview");dock.transformTimer_.stop();format(dock,1);dock.settings_.setValue("panelWidth",400);dock.refreshPanel();QApplication::processEvents();QImage bitrate(dock.badge_->size(),QImage::Format_ARGB32_Premultiplied);bitrate.fill(Qt::transparent);dock.badge_->render(&bitrate,QPoint(),QRegion(dock.badge_->rect()),QWidget::DrawWindowBackground|QWidget::DrawChildren|QWidget::IgnoreMask);checkState(bitrate.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".bitrates.png"),"shared bitrate panel preview");dock.transformTimer_.stop();dock.settings_.setValue("panelWidth",480);format(dock,3);dock.refreshPanel();}
  dock.settings_.setValue("independentCards",true);dock.setIndependentLayout(true);dock.refreshPanel();checkState(dock.movableCards().contains("bitrate") && dock.cardGeometry_.contains("bitrate"),"bitrate card can be moved independently");dock.settings_.setValue("independentCards",false);dock.setIndependentLayout(false);
  dock.recordingActive_=false;dock.streamingActive_=false;dock.recordingBitrate_.reset();dock.streamingBitrate_.reset();dock.updateBitrateCard();checkState(!dock.bitrates_->isHidden() && !dock.bitrates_->hasActiveOutputs(),"enabled bitrate panel stays discoverable while idle without active lines");
  format(dock,0);dock.refreshPanel();checkState(!dock.graphs_->compact() && !dock.statNames_[0]->isHidden(),"text stats restore after leaving gaming mode");
  auto *range=dock.findChild<QComboBox*>("graphWindow");range->setCurrentIndex(range->findData(300));checkState(dock.settings_.value("graphWindow").toInt()==300,"history range saves immediately");
  auto *position=dock.findChild<QComboBox*>("thumbnailPosition");checkState(position!=nullptr,"thumbnail placement available");
  const auto originalPanelWidth=dock.settings_.value("panelWidth");
  dock.settings_.setValue("panelMode",1);format(dock,0);dock.metricsVisible_=true;
  dock.settings_.setValue("showThumbnail",true);dock.settings_.setValue("panelWidth",300);dock.settings_.setValue("thumbnailWidth",180);dock.refreshPanel();
  checkState(!dock.previewCard_->isHidden() && !dock.information_->isHidden(),"placement test shows both columns");
  const bool fitsSide=QGuiApplication::primaryScreen()->availableGeometry().width()>=300+dock.panelLayout_->spacing()+16+80;
  position->setCurrentIndex(2);dock.place(dock.badge_,"");
  checkState(dock.panelLayout_->direction()==(fitsSide?QBoxLayout::RightToLeft:QBoxLayout::BottomToTop),
             "left thumbnail stacks above the metrics only when the display is too narrow");
  position->setCurrentIndex(0);dock.place(dock.badge_,"");
  checkState(dock.panelLayout_->direction()==QBoxLayout::BottomToTop,"above thumbnail layout");
  position->setCurrentIndex(3);dock.place(dock.badge_,"");
  checkState(dock.panelLayout_->direction()==(fitsSide?QBoxLayout::LeftToRight:QBoxLayout::TopToBottom),
             "right thumbnail stacks below the metrics only when the display is too narrow");
  checkState(!dock.badge_->mask().contains(QPoint(0,0)) && dock.badge_->mask().contains(dock.stats_->mapTo(dock.badge_,dock.stats_->rect().center())),"card mask excludes the global background and includes visible stats");
  SentinelVisuals::roundWidget(dock.preview_,SentinelVisuals::PreviewRadius);checkState(!dock.preview_->mask().contains(QPoint(0,0)),"native thumbnail has rounded clipping");
  dock.settings_.setValue("panelWidth",originalPanelWidth);
  dock.settings_.setValue("thumbnailWidth",180);
  const qint64 previousFree=dock.freeBytes_;
  dock.settings_.setValue("gib",20);dock.freeBytes_=10LL*1024*1024*1024;
  checkState(std::abs(dock.metricRisk(1)-0.5)<0.001 && dock.glowRisk()>0,"approaching threshold requests glow");
  dock.settings_.setValue("glowWarningColor","#22aaee");dock.settings_.setValue("glowCriticalColor","#ff3355");
  checkState(dock.warningColor(0).name()=="#22aaee" && dock.warningColor(1).name()=="#ff3355","custom glow endpoints");
  dock.freeBytes_=20LL*1024*1024*1024;dock.updateStats();checkState(dock.metricRisk(1)==0 && dock.statValues_[1]->styleSheet().contains("#22aaee"),"storage warning begins at the configured positive value");dock.freeBytes_=1;dock.updateStats();checkState(dock.statValues_[1]->styleSheet().contains("#22aaee") && dock.statValues_[1]->text()=="<0.1 GiB","positive remaining space stays warning colored and never rounds to zero");dock.freeBytes_=0;dock.updateStats();checkState(dock.metricRisk(1)==1 && dock.statValues_[1]->styleSheet().contains("#ff3355"),"storage becomes critical only when no bytes remain");
  dock.freeBytes_=50LL*1024*1024*1024;checkState(dock.glowRisk()<0,"healthy metrics do not request persistent glow");dock.updateStats();checkState(dock.statValues_[1]->styleSheet().isEmpty(),"healthy stat inherits OBS theme");
  dock.reconnectAlarm_=true;checkState(dock.metricRisk(5)==1,"stream interruption is urgent");dock.reconnectAlarm_=false;
  dock.freeBytes_=previousFree;dock.updateStats();checkState(dock.statNames_[1]->text()=="Disk space" && dock.statValues_[0]->text()=="Inactive","compact stats labels and OBS output status");
  checkState(!dock.settings_.value("showInCaptures",false).toBool(),"capture visibility is private by default");
  obs_source_info sourceInfo{};sourceInfo.id="monitor_capture";sourceInfo.type=OBS_SOURCE_TYPE_INPUT;
  sourceInfo.get_name=[](void *){return "Test Display Capture";};sourceInfo.create=[](obs_data_t *,obs_source_t *source)->void *{return source;};sourceInfo.destroy=[](void *){};
  obs_register_source(&sourceInfo);
  auto *source=obs_source_create("monitor_capture","Test display",nullptr,nullptr);checkState(source!=nullptr,"capture fixture");obs_source_inc_showing(source);
  checkState(!dock.showProtected(dock.badge_) && dock.badge_->isHidden(),
             "private panels stay hidden while Automatic Display Capture is showing");
  auto *captureSettings=obs_source_get_settings(source);
  checkState(obs_data_get_int(captureSettings,"method")==0 && obs_source_showing(source),
             "pausing the overlay preserves Display Capture settings and visibility");
  obs_data_release(captureSettings);
  obs_source_dec_showing(source);
  checkState(dock.overlayBlockReason().isEmpty(),"inactive Display Capture does not block private panels");
  obs_source_release(source);
  dock.settings_.setValue("customPosition",true);dock.settings_.setValue("positionX",111);dock.settings_.setValue("positionY",222);dock.savePreset("Test position");
  dock.defaultPosition();checkState(!dock.settings_.value("customPosition").toBool(),"Default restores chosen corner");
  checkState(dock.loadPreset("Test position") && dock.settings_.value("positionX").toInt()==111,"preset restores custom position");
  dock.setEditMode(false);checkState(dock.badge_->windowFlags().testFlag(Qt::WindowTransparentForInput),"closed appearance returns click-through");
  dock.activateAction(0);checkState(dock.panelMode()==1,"Show Panel is persistent");
  dock.notify("Network issue","Connection interrupted",true);dock.activateAction(1);checkState(dock.panelMode()==0 && dock.alertActive_,"alerts override Hide Panel");
  dock.activateAction(2);checkState(dock.panelMode()==2 && !dock.alertActive_ && dock.badge_->isHidden(),"Disable Panel clears and hides alerts");
  dock.notify("Disabled alert","Must not show");checkState(dock.alerts_.isEmpty(),"disabled panel suppresses alerts");
  dock.settings_.setValue("showThumbnail",false);
  dock.activateAction(3);checkState(dock.settings_.value("showThumbnail").toBool(),"thumbnail hotkey toggles preference");dock.activateAction(3);
  dock.activateAction(4);checkState(!dock.metricsVisible_,"stats hotkey temporarily hides metrics");dock.activateAction(4);
  dock.activateAction(5);checkState(dock.settings_.value("showInCaptures").toBool(),"capture visibility hotkey toggles preference");dock.activateAction(5);
  auto *corners=dock.findChild<QPushButton*>("cycleDefaultPositions");
  const int oldCorner=dock.settings_.value("corner",1).toInt();
  for(int step=1;step<=4;++step){
   corners->click();
   checkState(dock.settings_.value("corner").toInt()==(oldCorner+step)%4 && !dock.settings_.value("customPosition").toBool(),"dock cycles all default positions and wraps");
  }
  dock.settings_.setValue("customPosition",true);dock.settings_.setValue("positionX",333);dock.savePreset("Second position");
  auto *presets=dock.findChild<QPushButton*>("cyclePositionPresets");
  checkState(presets->isEnabled(),"saving a preset enables dock cycling");
  presets->click();
  checkState(dock.settings_.value("positionX").toInt()==333,"dock cycles to the next saved position");
  presets->click();
  checkState(dock.settings_.value("positionX").toInt()==111,"dock wraps to the first saved position");
  dock.removePreset("Test position");presets->click();
  checkState(dock.settings_.value("positionX").toInt()==333,"dock cycles remaining presets after a deletion");
  dock.removePreset("Second position");
  checkState(!presets->isEnabled(),"dock preset button disables when no saved positions remain");
  auto *display=dock.findChild<QPushButton*>("displayMode")->menu();
  const int previousMode=dock.displayMode();
  display->aboutToShow();
  checkState(display->actions().size()==3,"dock lists Icons, Active icons and Custom");
  for(auto *item:display->actions())checkState(item->isChecked()==(item->data().toInt()==previousMode),"dock marks the current display mode");
  for(auto *item:display->actions()){
   item->trigger();
   checkState(dock.displayMode()==item->data().toInt() && dock.panelMode()==2 && !dock.thumbnailEnabled(),"dock display changes preserve panel visibility and thumbnail settings");
  }
  dock.setDisplayMode(previousMode);
  dock.settings_.setValue("showThumbnail",true);dock.metricsVisible_=false;dock.settings_.setValue("showAlertIcon",true);dock.information_->hide();dock.preview_->setVisible(true);dock.previewCard_->show();dock.alertActive_=true;dock.lastAlertTitle_="Storage running low";dock.place(dock.badge_,"");
  checkState(dock.badge_->width()==dock.preview_->width()+32 && dock.badge_->height()==dock.preview_->height()+32,"thumbnail card has uniform inner and outer padding");
  checkState(!dock.alertIcon_->isHidden() && dock.alertIcon_->property("alertRole").toString()=="storage","offending alert icon overlays thumbnail");
  dock.settings_.setValue("showAlertIcon",false);dock.updateAlertIcon();checkState(dock.alertIcon_->isHidden(),"alert icon option can be disabled");
  dock.settings_.setValue("showThumbnail",false);dock.metricsVisible_=true;dock.settings_.setValue("showAlertIcon",true);dock.preview_->hide();dock.previewCard_->hide();dock.alertActive_=false;dock.activateAction(1);
  checkState(!dock.findChild<QCheckBox*>("enabled"),"ambiguous Alerts panel toggle removed");
  auto *releases=dock.findChild<QLabel*>("githubReleases");
  checkState(releases && releases->openExternalLinks() &&
             releases->text().contains("https://github.com/technicallyalex/lookout/releases"),
             "About links to the official release page");
  dock.findChild<QCheckBox*>("untilDismiss")->setChecked(true);
  dock.notify("Storage running low","1 GiB available",true);
  checkState(dock.alertActive_ && !dock.dismiss_.isActive(),"sticky alert has no expiry timer");
  dock.sampleNetwork(100,0,false,false,true);
  dock.sampleNetwork(200,10,false,true,true);
  checkState(dock.alerts_.contains("Stream quality degraded"),"network loss notification");
  dock.sampleNetwork(200,10,true,true,true);
  checkState(dock.alerts_.contains("Stream disconnected"),"reconnect notification");
  checkState(dock.alerts_.contains("Storage running low"),"simultaneous alerts retained");
  dock.sampleNetwork(300,10,false,true,true);
  checkState(dock.alerts_.contains("Stream reconnected"),"recovery notification");
  dock.activateAction(0);
  dock.dismissAlert();checkState(!dock.alertActive_ && dock.alerts_.isEmpty(),"dismiss clears alerts");
  checkState(dock.stats_->isVisibleTo(dock.information_) && dock.badgeText_->isHidden(),"persistent mode returns to compact status rows");
  dock.activateAction(1);
  dock.findChild<QCheckBox*>("untilDismiss")->setChecked(false);
  dock.notify("Test","Timed notification");checkState(dock.dismiss_.isActive(),"timed alerts expire");dock.dismissAlert();
  checkState(dock.badge_->isHidden(),"nonpersistent panel hides after dismiss");
  checkState(dock.findChild<QPushButton*>("previewWarningGlow") && dock.findChild<QPushButton*>("previewCriticalGlow"),"separate color preview controls");
  const int alertsBefore=dock.alerts_.size();QStringList styles;for(int i=0;i<6;++i)styles<<dock.statValues_[i]->styleSheet();
  dock.previewGlow(0);checkState(dock.glowPreviewRisk_==0 && dock.glowPreviewMetric_>=0 && dock.glowPreviewMetric_<6,"warning preview picks a random stat");
  dock.applyMetricColors(1);checkState(dock.statValues_[dock.glowPreviewMetric_]->styleSheet().contains(dock.warningColor(0).name()),"stat flashes with warning glow color");
  dock.previewGlow(1);dock.applyMetricColors(1);checkState(dock.statValues_[dock.glowPreviewMetric_]->styleSheet().contains(dock.warningColor(1).name()),"stat flashes with critical glow color");
  checkState(dock.glowPreviewUntil_>dock.glowClock_.elapsed() && dock.alerts_.size()==alertsBefore,"preview does not generate alerts");
  dock.glowPreviewUntil_=dock.glowClock_.elapsed()-1;dock.updateGlow();for(int i=0;i<6;++i)checkState(dock.statValues_[i]->styleSheet()==styles[i],"preview restores real stat colors");
  checkState(dock.glowPreviewMetric_==-1 && !dock.glowTimer_.isActive(),"preview cleanup stops animation");
  dock.previewGlow(0);dock.activateAction(1);checkState(dock.glowPreviewUntil_==0 && dock.panelMode()==0,"Hide Panel cancels an explicit preview");
  dock.dismissAlert();dock.notify("Session update","Recording started");checkState(!dock.alertActive_ && dock.badgeText_->isHidden(),"lifecycle events show stats without redundant callouts");
  for(int i=0;i<8;++i){auto *button=dock.findChild<QPushButton*>(QString("previewAlert%1").arg(i));checkState(button!=nullptr,"specific alert preview available");button->click();checkState(dock.alerts_.isEmpty() && !dock.previewAlertTitle_.isEmpty() && dock.badgeText_->text().contains("Preview"),"simulations are visually identified and leave real alerts untouched");checkState(dock.glowPreviewMetric_==(i==6?-1:i<2 || i==5?1:i==2?3:5),"preview highlights the offending stat");}
  dock.findChild<QPushButton*>("stopAlertPreview")->click();checkState(dock.previewAlertTitle_.isEmpty() && dock.badgeText_->isHidden(),"stop preview restores real panel");
  checkState(dock.findChild<QComboBox*>("graphDisplay")==nullptr,"duplicate stats display selector removed");
  dock.place(dock.badge_,"");QApplication::processEvents();
  const auto originalPalette=dock.badge_->palette();QPalette transparent=originalPalette;transparent.setColor(QPalette::Window,QColor(15,15,15,0));dock.badge_->setPalette(transparent);
  QImage surface(dock.badge_->size(),QImage::Format_ARGB32_Premultiplied);surface.fill(Qt::transparent);dock.badge_->render(&surface,QPoint(),QRegion(dock.badge_->rect()),QWidget::DrawWindowBackground|QWidget::DrawChildren|QWidget::IgnoreMask);const QPoint surfacePoint=dock.stats_->mapTo(dock.badge_,QPoint(dock.stats_->width()/2,dock.stats_->height()-4));checkState(surface.pixelColor(surfacePoint).alpha()==255,"card background remains opaque during repaint");checkState(surface.pixelColor(2,2).alpha()==0,"there is no global panel background");dock.badge_->setPalette(originalPalette);
  dock.place(dock.badge_,"");
  auto *opacity=dock.findChild<QSpinBox*>("backgroundOpacity");checkState(opacity!=nullptr,"panel opacity control available");opacity->setValue(40);dock.applyPanelAppearance();
  QImage translucent(dock.badge_->size(),QImage::Format_ARGB32_Premultiplied);translucent.fill(Qt::transparent);dock.badge_->render(&translucent,QPoint(),QRegion(dock.badge_->rect()),QWidget::DrawWindowBackground|QWidget::DrawChildren|QWidget::IgnoreMask);
  const QPoint opacityPoint=dock.stats_->mapTo(dock.badge_,QPoint(dock.stats_->width()/2,dock.stats_->height()-4));const int alpha=translucent.pixelColor(opacityPoint).alpha();checkState(alpha>=100 && alpha<=104 && dock.badge_->windowOpacity()==1.0,"opacity affects background without fading foreground");opacity->setValue(100);dock.applyPanelAppearance();
  auto *blur=dock.findChild<QSpinBox*>("blurAmount");
  checkState(blur && blur->minimum()==0 && blur->maximum()==100 &&
             !dock.findChild<QCheckBox*>("backgroundBlur"),"one 0–100 control sets backdrop blur");
  blur->setValue(42);checkState(dock.settings_.value("blurAmount").toInt()==42,"blur amount persists");
  blur->setValue(0);checkState(!dock.badge_->property("clipBlur").toBool(),"zero disables backdrop blur");
  bool antialiased=false;for(int y=0;y<8;++y)for(int x=0;x<8;++x){const auto alpha=surface.pixelColor(dock.stats_->mapTo(dock.badge_,QPoint(x,y))).alpha();if(alpha>0 && alpha<255)antialiased=true;}checkState(antialiased,"rounded card edges retain smooth fractional alpha");
  format(dock,1);dock.settings_.setValue("iconVisible/storage",true);dock.settings_.setValue("iconVisible/encoder",true);dock.settings_.setValue("iconVisible/network",true);dock.refreshPanel();dock.previewAlert(2);const auto targetCards=dock.glowTargets();const QRectF encoderCard=dock.graphs_->metricRect(1).translated(dock.graphs_->mapTo(dock.badge_,QPoint()));checkState(targetCards.size()==2 && targetCards.contains(encoderCard),"encoder preview glows only stats and encoder graph cards");checkState(!targetCards.contains(dock.graphs_->metricRect(0).translated(dock.graphs_->mapTo(dock.badge_,QPoint()))),"unrelated disk graph never glows for encoder preview");
  dock.setEditMode(true);
  auto *independent=dock.findChild<QCheckBox*>("independentCards");checkState(independent!=nullptr,"independent card editing option");independent->setChecked(true);checkState(dock.independentLayout_ && dock.stats_->parentWidget()==dock.information_ && !dock.information_->layout()->isEnabled() && dock.graphViews_[1]->samples().size()==dock.graphs_->samples().size(),"independent cards keep their parent and graph views share history");
  QWidget *dragTarget=dock.statNames_[1];const QPointF local=dragTarget->rect().center(),global=dragTarget->mapToGlobal(local.toPoint());QMouseEvent dragPress(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(dragTarget,&dragPress);QMouseEvent dragMove(QEvent::MouseMove,local+QPointF(18,24),global+QPointF(18,24),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(dragTarget,&dragMove);QMouseEvent dragRelease(QEvent::MouseButtonRelease,local+QPointF(18,24),global+QPointF(18,24),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(dragTarget,&dragRelease);checkState(dock.settings_.value("cardPositions").toMap().contains("stats") && !dock.dragging_,"dragging a stat label persists its independent card position");
  auto positions=dock.settings_.value("cardPositions").toMap();positions["stats"]=QPointF(.2,.3);dock.settings_.setValue("cardPositions",positions);dock.place(dock.badge_,"");checkState(dock.cardGeometry_.contains("stats") && dock.cardGeometry_.contains("encoder"),"independent card geometry stays addressable");independent->setChecked(false);checkState(dock.information_->layout()->isEnabled() && dock.graphViews_[1]->isHidden(),"grouped layout resumes automatic placement");dock.findChild<QPushButton*>("stopAlertPreview")->click();format(dock,0);
  checkState(dock.dockOrder_.size()==6 &&
   dock.findChild<QPushButton*>("displayMode")->menu(),
   "dock contains only the six panel controls and the display mode menu");
  checkState(!dock.dialog_->findChild<QComboBox*>("statsDisplay") && dock.dialog_->findChild<QGroupBox*>("metrics"),"Metrics and Graphs replace the old display-specific controls");dock.settings_.setValue("panelMode",0);format(dock,5);checkState(dock.panelMode()==0,"presentation does not override panel visibility");dock.settings_.setValue("panelMode",1);format(dock,0);
  checkState(!dock.dialog_->findChild<QComboBox*>("animationStyle") && !dock.dialog_->findChild<QPushButton*>("previewMotion"),"transition settings removed");
  dock.settings_.setValue("showBitrate",false);dock.refreshPanel();checkState(dock.statNames_[SentinelDock::BitrateStat]->isHidden(),"bitrate reading has its own toggle");dock.settings_.setValue("showBitrate",true);dock.refreshPanel();
  dock.settings_.setValue("showInCaptures",true);dock.dismissAlert();dock.settings_.setValue("panelMode",1);dock.refreshPanel();dock.badge_->show();dock.transformTimer_.stop();dock.activateAction(1);checkState(!dock.badge_->isVisible(),"Hide is immediate while settings is open");dock.activateAction(0);checkState(!dock.editHidden_ && dock.panelMode()==1,"Show clears edit hiding");
  qaSettings(dock);universalMetrics(dock);styleMatrix(dock);
  const auto geometry=dock.badge_->geometry();const auto mask=dock.badge_->mask();for(int i=0;i<5;++i)dock.place(dock.badge_,"");checkState(dock.badge_->geometry()==geometry && dock.badge_->mask()==mask,"unchanged refresh preserves geometry and rounding");
 }
 static void capturePanel(SentinelDock &dock,const QString &path){
  dock.setEditMode(false);dock.settings_.setValue("panelWidth",520);dock.settings_.setValue("thumbnailPosition",1);dock.settings_.setValue("showThumbnail",false);dock.metricsVisible_=true;dock.settings_.setValue("panelMode",1);dock.dismissAlert();
  format(dock,0);dock.refreshPanel();QApplication::processEvents();checkState(dock.badge_->grab().save(path),"notification layout screenshot");
  format(dock,1);for(int preset=0;preset<9;++preset){dock.settings_.setValue("graphAppearance",preset);dock.refreshPanel();QApplication::processEvents();dock.place(dock.badge_,"");checkState(dock.graphs_->mapTo(dock.badge_,dock.graphs_->rect().bottomRight()).y()<dock.badge_->height(),"all graph cards fit inside the panel");checkState(dock.badge_->grab().save(path+QString(".design%1.png").arg(preset)),"whole panel design screenshot");if(preset==8){for(int mode:{1,2}){dock.settings_.setValue("logoTheme",mode);dock.refreshPanel();QApplication::processEvents();checkState(dock.badge_->grab().save(path+QString(".lookout-%1.png").arg(mode==1?"day":"night")),"lookout day and night previews");}}}dock.badge_->hide();dock.settings_.setValue("panelWidth",480);dock.settings_.setValue("thumbnailPosition",3);
 }
 static void captureBlur(SentinelDock &dock,const QString &path){
  class Checker final:public QWidget{void paintEvent(QPaintEvent*)override{QPainter p(this);for(int y=0;y<height();y+=8)for(int x=0;x<width();x+=8)p.fillRect(x,y,8,8,((x+y)/8)%2?QColor("#ffffff"):QColor("#000000"));}};
  Checker checker;checker.setWindowFlags(Qt::Tool|Qt::WindowStaysOnTopHint);checker.setGeometry(100,100,900,600);checker.show();
  dock.dialog_->hide();dock.settings_.setValue("showInCaptures",true);dock.settings_.setValue("showThumbnail",false);dock.metricsVisible_=true;format(dock,1);dock.settings_.setValue("panelMode",1);dock.settings_.setValue("backgroundOpacity",30);dock.settings_.setValue("customPosition",true);dock.settings_.setValue("positionX",140);dock.settings_.setValue("positionY",160);dock.dismissAlert();
  for(int mode=0;mode<2;++mode){dock.settings_.setValue("blurAmount",mode*100);dock.refreshPanel();dock.badge_->raise();QEventLoop loop;QTimer::singleShot(600,&loop,&QEventLoop::quit);loop.exec();const QRect r=dock.badge_->geometry();checkState(QGuiApplication::primaryScreen()->grabWindow(0,r.x(),r.y(),r.width(),r.height()).save(path+QString(".blur%1.png").arg(mode)),"Windows compositor blur visual capture");}
  dock.settings_.setValue("showInCaptures",false);dock.refreshPanel();
#ifdef _WIN32
  int privateCards=0;EnumWindows([](HWND hwnd,LPARAM data)->BOOL{wchar_t name[64]{};GetClassNameW(hwnd,name,64);if(wcscmp(name,L"SentinelCardBackdrop")==0){DWORD affinity=0;checkState(GetWindowDisplayAffinity(hwnd,&affinity) && affinity==WDA_EXCLUDEFROMCAPTURE,"blur card inherits private capture exclusion");++*reinterpret_cast<int*>(data);}return TRUE;},reinterpret_cast<LPARAM>(&privateCards));checkState(privateCards>=3,"native blur card surfaces tested");
#endif
  dock.badge_->hide();checker.hide();
 }
 static void checkState(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
};
namespace {
obs_frontend_event_cb cb=nullptr; void *context=nullptr;
bool ready=false, streaming=false;
int reads=0, outputReads=0;
config_t *profile=nullptr;
void check(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void read(){check(ready,"frontend read outside initialized lifetime");++reads;}
}
extern "C" {
config_t *obs_frontend_get_profile_config(){read();return profile;}
bool obs_frontend_is_theme_dark(){return true;}
bool obs_frontend_recording_active(){read();return false;}
bool obs_frontend_streaming_active(){read();return streaming;}
bool obs_frontend_replay_buffer_active(){read();return replayTesting;}
obs_output_t *obs_frontend_get_replay_buffer_output(){read();return obs_output_get_ref(replayTestOutput);}
bool obs_frontend_virtualcam_active(){read();return false;}
obs_output_t *obs_frontend_get_recording_output(){read();++outputReads;return nullptr;}
obs_output_t *obs_frontend_get_streaming_output(){read();check(streaming,"idle streaming output must not be requested");++outputReads;return nullptr;}
void obs_frontend_recording_start(){} void obs_frontend_recording_stop(){}
void obs_frontend_streaming_start(){} void obs_frontend_streaming_stop(){}
void obs_frontend_start_virtualcam(){} void obs_frontend_stop_virtualcam(){}
void obs_frontend_add_event_callback(obs_frontend_event_cb f,void *p){cb=f;context=p;}
void obs_frontend_remove_event_callback(obs_frontend_event_cb f,void *p){check(cb==f&&p==context,"callback removed");cb=nullptr;context=nullptr;}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"temporary folder");
 check(obs_startup("en-US",temp.path().toUtf8().constData(),nullptr),"libobs startup");
 const bool gpu=!qEnvironmentVariableIsEmpty("SENTINEL_GPU_CAPTURE");
 if(gpu){obs_add_data_path(obsTestPath("data").toUtf8().constData());const auto graphics=obsTestPath("bin/64bit/libobs-d3d11").toUtf8();obs_video_info video{};video.graphics_module=graphics.constData();video.fps_num=qEnvironmentVariableIsEmpty("SENTINEL_QUICK_QA")?30:60;video.fps_den=1;video.base_width=video.output_width=1280;video.base_height=video.output_height=720;video.output_format=VIDEO_FORMAT_BGRA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;check(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"initialize real D3D11 test output");obs_add_main_render_callback(qaDraw,nullptr);}
 check(config_open(&profile,temp.filePath("profile.ini").toUtf8().constData(),CONFIG_OPEN_ALWAYS)==CONFIG_SUCCESS,"profile");
 config_set_string(profile,"SimpleOutput","FilePath",temp.path().toUtf8().constData());
 const auto coreKey=obs_hotkey_register_frontend("OBSBasic.StartRecording","Test native recording",[](void *,obs_hotkey_id,obs_hotkey_t *,bool){},nullptr);
 {
  SentinelDock dock(temp.filePath("sentinel.ini"));check(cb!=nullptr,"callback registered");
  check(dock.findChildren<QLabel*>().isEmpty(),"dock has no repeated heading or status rows");
  check(!dock.isEnabled() && !SentinelTestAccess::sampling(dock),"controls and sampler disabled during startup");
  QApplication::processEvents();check(reads==0,"no premature timer reads");
  ready=true;cb(OBS_FRONTEND_EVENT_FINISHED_LOADING,context);
  check(dock.isEnabled() && SentinelTestAccess::sampling(dock) && reads>0,"sampling starts after OBS is ready");check(outputReads==0,"idle output not accessed");
  check(dock.findChildren<QSpinBox*>().isEmpty(),"dock has no inline settings");
  dock.openSettings();
  auto *corner=dock.findChild<QComboBox*>("Corner");
  auto *x=dock.findChild<QSpinBox*>("offsetX");
  auto *width=dock.findChild<QSpinBox*>("panelWidth");
  check(corner && x && width,"placement controls in dock");
  corner->setCurrentIndex(2);x->setValue(48);width->setValue(480);
  QSettings saved(temp.filePath("sentinel.ini"),QSettings::IniFormat);
  check(saved.value("corner").toInt()==2,"combined panel position saved");
  check(saved.value("offsetX").toInt()==48 && saved.value("panelWidth").toInt()==480,"inset and size saved immediately");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT")){
   dock.resize(300,330);dock.show();QApplication::processEvents();
   check(dock.grab().save(qEnvironmentVariable("SENTINEL_SCREENSHOT")),"dock screenshot");
   auto *dialog=dock.findChild<QDialog*>();check(dialog!=nullptr,"dedicated settings window");dialog->resize(980,720);QApplication::processEvents();
   check(dialog->findChild<QTabWidget*>()->count()==4,"settings consolidate graphs under Appearance");
   check(dialog->grab().save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".settings.png"),"settings screenshot");dock.hide();
  }
  if(qEnvironmentVariableIsEmpty("SENTINEL_BLUR_CAPTURE")){SentinelTestAccess::exercise(dock);}
  SentinelTestAccess::iconMode(dock);
  if(gpu && !qEnvironmentVariableIsEmpty("SENTINEL_REPLAY_QA"))SentinelTestAccess::realReplay(dock);
  if(gpu && qEnvironmentVariableIsEmpty("SENTINEL_QUICK_QA"))SentinelTestAccess::nativePanels(dock,qEnvironmentVariable("SENTINEL_GPU_CAPTURE"));
  if(gpu)SentinelTestAccess::quickMigration(dock);
  if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT"))SentinelTestAccess::capturePanel(dock,qEnvironmentVariable("SENTINEL_SCREENSHOT")+".panel.png");
  if(!qEnvironmentVariableIsEmpty("SENTINEL_BLUR_CAPTURE")){SentinelTestAccess::captureBlur(dock,qEnvironmentVariable("SENTINEL_BLUR_CAPTURE"));}
  streaming=true;cb(OBS_FRONTEND_EVENT_STREAMING_STARTED,context);QApplication::processEvents();check(outputReads>0,"active output sampled");
  ready=false;cb(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN,context);const int before=reads;
  check(!dock.isEnabled() && !SentinelTestAccess::sampling(dock),"controls and sampler disabled during shutdown");
  QApplication::processEvents();check(reads==before,"no timer access during teardown");
  cb(OBS_FRONTEND_EVENT_EXIT,context);
 }
 check(cb==nullptr,"unregistered callback");
 {
  QSettings legacy(temp.filePath("legacy-metrics.ini"),QSettings::IniFormat);
  legacy.setValue("statsDisplay",6);
  legacy.setValue("iconVisible/network",false);
  legacy.sync();
  SentinelDock migrated(legacy.fileName());
  legacy.sync();
  check(legacy.value("statsDisplay").toInt()==0 && legacy.value("graph/storage").toInt()==1 &&
        legacy.value("graph/bitrate").toInt()==1 && legacy.value("graph/replay").toInt()==1 &&
        legacy.value("graph/network",0).toInt()==0,"existing mini chart preferences migrate to Custom");
 }
 for(const bool enabled:{false,true}){
  QSettings legacy(temp.filePath(enabled?"blur-on.ini":"blur-off.ini"),QSettings::IniFormat);
  legacy.setValue("backgroundBlur",enabled);legacy.setValue("blurAmount",30);legacy.sync();
  SentinelDock migrated(legacy.fileName());
  legacy.sync();
  check(!legacy.contains("backgroundBlur") && legacy.value("blurAmount").toInt()==(enabled?30:0),
        "legacy blur preferences migrate to the single amount control");
 }
 if(qEnvironmentVariableIsEmpty("SENTINEL_BLUR_CAPTURE")){
  SentinelDock reopened(temp.filePath("sentinel.ini"));reopened.openSettings();
  check(reopened.findChild<QComboBox*>("thumbnailPosition")->currentIndex()==3,"thumbnail placement restored");
  check(reopened.findChild<QComboBox*>("Corner")->currentIndex()==2,"notification placement restored");
  check(reopened.findChild<QSpinBox*>("panelWidth")->value()==480,"thumbnail width restored");
 }
 if(gpu)obs_remove_main_render_callback(qaDraw,nullptr);obs_hotkey_unregister(coreKey);config_close(profile);obs_shutdown();std::puts("PASS: startup, idle, active output, shutdown and cleanup");
}
