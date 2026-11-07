// SPDX-License-Identifier: GPL-2.0-or-later
#include "lookout.hpp"
#include "settings-migration.hpp"
#include "panel-visuals.hpp"
#include "card-layout.hpp"
#include "lookout-brand.hpp"
#include "quick-bridge.hpp"
#include "thumbnail-render.hpp"
#include <QApplication>
#include <QFontInfo>
#include <QDialog>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QMenu>
#include <QActionGroup>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QScreen>
#include <QStorageInfo>
#include <QFileInfo>
#include <QDir>
#include <QRunnable>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QCloseEvent>
#include <QScrollArea>
#include <QStyle>
#include <QFile>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QMouseEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QPainter>
#include <QFontMetricsF>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <functional>
#include <util/dstr.h>
#include <cmath>
#include <util/config-file.h>
#include <util/platform.h>
#include <graphics/graphics.h>
#include <media-io/video-io.h>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#endif
namespace {
constexpr int DockButtonSize=28, DockSpacing=4, DockMargin=4;
const char *names[] = {"Lookout.ShowPanel","Lookout.HidePanel","Lookout.DisablePanel","Lookout.Preview","Lookout.ToggleStats","Lookout.ToggleCapture","Lookout.CycleDefaults","Lookout.CyclePresets"};
const char *descriptions[] = {"lookout: Show Panel (persistent)","lookout: Hide Panel (alerts override)","lookout: Disable Panel (also disables alerts)","lookout: Toggle Thumbnail","lookout: Toggle Stats","lookout: Toggle Show in Recordings/Stream","lookout: Cycle Position (Defaults)","lookout: Cycle Position (Presets)"};
QIcon uiIcon(const QString &role,std::optional<bool> darkMode={}){
 const QMap<QString,QString> names={{"status","show"},{"dismiss","hide"},{"record","recording"},{"stream","streaming"},{"camera","virtual-camera"}};const QString name=names.value(role,role);const bool dark=darkMode.value_or(obs_frontend_is_theme_dark());const QString path=":/lookout/icons/"+QString(dark?"dark/":"light/")+name+".svg";
 if(QFile::exists(path))return QIcon(path);return QApplication::style()->standardIcon(role=="dismiss"?QStyle::SP_DialogCloseButton:QStyle::SP_FileDialogDetailedView);
}
// Focused shortcut entry only; OBS continues to own global hotkey dispatch.
class HotkeyEditor final : public QLineEdit {
 std::function<void(obs_key_combination_t)> commit_;
 obs_key_combination_t pending_{};bool hasPending_=false;
 bool event(QEvent *event) override {
  if(event->type()==QEvent::ShortcutOverride){event->accept();return true;}
  return QLineEdit::event(event);
 }
 void keyPressEvent(QKeyEvent *event) override {
  if(event->isAutoRepeat())return;
  if(event->key()==Qt::Key_Escape){hasPending_=false;clearFocus();return;}
  if(event->key()==Qt::Key_Shift || event->key()==Qt::Key_Control || event->key()==Qt::Key_Alt || event->key()==Qt::Key_Meta)return;
  obs_key_combination_t combination{};combination.key=obs_key_from_virtual_key(int(event->nativeVirtualKey()));
  if(combination.key==OBS_KEY_NONE)return;
  const auto modifiers=event->modifiers();
  if(modifiers.testFlag(Qt::ControlModifier))combination.modifiers|=INTERACT_CONTROL_KEY;
  if(modifiers.testFlag(Qt::ShiftModifier))combination.modifiers|=INTERACT_SHIFT_KEY;
  if(modifiers.testFlag(Qt::AltModifier))combination.modifiers|=INTERACT_ALT_KEY;
  if(modifiers.testFlag(Qt::MetaModifier))combination.modifiers|=INTERACT_COMMAND_KEY;
  pending_=combination;hasPending_=true;
  struct dstr text={};obs_key_combination_to_str(combination,&text);setText(QString::fromUtf8(text.array?text.array:""));dstr_free(&text);event->accept();
 }
 void keyReleaseEvent(QKeyEvent *event) override {
  if(!hasPending_ || event->isAutoRepeat() || obs_key_from_virtual_key(int(event->nativeVirtualKey()))!=pending_.key)return;
  const auto combination=pending_;hasPending_=false;
  // Wait for OBS's native key-state poll to observe key-up before installing
  // the binding, so assignment itself cannot invoke the newly chosen action.
  QTimer::singleShot(75,this,[this,combination]{commit_(combination);});event->accept();
 }
public:
 explicit HotkeyEditor(std::function<void(obs_key_combination_t)> commit):commit_(std::move(commit)){
  setReadOnly(true);setProperty("lookoutHotkeyEditor",true);setPlaceholderText("Click, then press shortcut");setAttribute(Qt::WA_InputMethodEnabled,false);
 }
};
#ifdef _WIN32
bool setNativeBlur(HWND hwnd,bool enabled){
 struct AccentPolicy{int state,flags;DWORD tint;int animation;};struct CompositionData{int attribute;void *data;SIZE_T size;};
 using SetComposition=BOOL(WINAPI*)(HWND,CompositionData*);
 static const auto setComposition=reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
 AccentPolicy accent{enabled?3:0,0,0,0};CompositionData data{19,&accent,sizeof(accent)};return setComposition && setComposition(hwnd,&data);
}
LRESULT CALLBACK backdropProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 if(message==WM_NCCALCSIZE && wparam)return 0;
 if(message==WM_NCHITTEST)return HTTRANSPARENT;
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_PAINT){PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);return 0;}
 return DefWindowProcW(window,message,wparam,lparam);
}
#endif
// One capture-excluded window, with independent rounded cards and transparent gaps.
class NotificationPanel final : public QWidget {
 QRegion lastCards_;
 bool lastBlur_=false,lastPublic_=false;
#ifdef _WIN32
 QVector<HWND> backdrops_;
 QVector<QRect> backdropClips_;
 QVector<QSize> backdropSizes_;
#endif
 void syncBackdrops(){
#ifdef _WIN32
  if(property("quickModel").toBool())return;
  const bool enabled=property("clipBlur").toBool();const auto regions=cards();
  if(!enabled){for(auto hwnd:backdrops_)DestroyWindow(hwnd);backdrops_.clear();backdropClips_.clear();backdropSizes_.clear();return;}
  WNDCLASSW cls{};const HINSTANCE instance=GetModuleHandleW(nullptr);bool available=GetClassInfoW(instance,L"LookoutCardBackdrop",&cls)!=0;if(!available){cls.lpfnWndProc=backdropProc;cls.hInstance=instance;cls.lpszClassName=L"LookoutCardBackdrop";available=RegisterClassW(&cls)!=0;}
  while(backdrops_.size()>regions.size()){DestroyWindow(backdrops_.takeLast());backdropClips_.removeLast();backdropSizes_.removeLast();}
  while(available && backdrops_.size()<regions.size()){
   HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOPMOST,L"LookoutCardBackdrop",L"",WS_POPUP|WS_THICKFRAME|WS_CAPTION,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
   if(!hwnd){available=false;break;}
   const int corner=2;DwmSetWindowAttribute(hwnd,33,&corner,sizeof(corner));const DWORD noBorder=0xfffffffe;DwmSetWindowAttribute(hwnd,34,&noBorder,sizeof(noBorder));
   if(!setNativeBlur(hwnd,true)){DestroyWindow(hwnd);available=false;break;}backdrops_<<hwnd;backdropClips_<<QRect();backdropSizes_<<QSize();
  }
  const double scale=devicePixelRatioF();const HWND panel=reinterpret_cast<HWND>(winId());RECT panelRect{};GetWindowRect(panel,&panelRect);
  for(int i=0;i<backdrops_.size();++i){HWND hwnd=backdrops_[i];
   const DWORD affinity=property("publicCards").toBool()?WDA_NONE:WDA_EXCLUDEFROMCAPTURE;DWORD previous=0;
   if(!GetWindowDisplayAffinity(hwnd,&previous) || previous!=affinity){if(!SetWindowDisplayAffinity(hwnd,affinity)){ShowWindow(hwnd,SW_HIDE);available=false;continue;}}
   const auto card=regions[i];const RECT target{panelRect.left+LONG(std::round(card.left()*scale)),panelRect.top+LONG(std::round(card.top()*scale)),panelRect.left+LONG(std::round(card.right()*scale)),panelRect.top+LONG(std::round(card.bottom()*scale))};
   const QRect full(0,0,target.right-target.left,target.bottom-target.top);const QRect viewport(panelRect.left,panelRect.top,qRound(width()*scale),qRound(height()*scale));const QRect positioned(target.left,target.top,full.width(),full.height());const QRect visible=positioned.intersected(viewport);if(visible.isEmpty()){ShowWindow(hwnd,SW_HIDE);continue;}
   const QRect clip=visible==positioned?QRect():visible.translated(-positioned.topLeft());if(backdropClips_[i]!=clip || backdropSizes_[i]!=full.size()){
    if(clip.isNull())SetWindowRgn(hwnd,nullptr,FALSE);else{const int diameter=std::max(1,qRound(LookoutVisuals::PanelRadius*2*scale));HRGN rounded=CreateRoundRectRgn(0,0,full.width()+1,full.height()+1,diameter,diameter),limit=CreateRectRgn(clip.left(),clip.top(),clip.right()+1,clip.bottom()+1);CombineRgn(rounded,rounded,limit,RGN_AND);DeleteObject(limit);if(!SetWindowRgn(hwnd,rounded,FALSE))DeleteObject(rounded);}backdropClips_[i]=clip;backdropSizes_[i]=full.size();
   }
   RECT current{};GetWindowRect(hwnd,&current);const bool changed=current.left!=target.left || current.top!=target.top || current.right!=target.right || current.bottom!=target.bottom;
   const int corner=2;DwmSetWindowAttribute(hwnd,33,&corner,sizeof(corner));const DWORD noBorder=0xfffffffe;DwmSetWindowAttribute(hwnd,34,&noBorder,sizeof(noBorder));
   if(isVisible()){SetWindowPos(hwnd,panel,target.left,target.top,target.right-target.left,target.bottom-target.top,SWP_NOACTIVATE|SWP_SHOWWINDOW|(changed?0:SWP_NOMOVE|SWP_NOSIZE));}else ShowWindow(hwnd,SW_HIDE);
  }
  setProperty("blurAvailable",available);
#endif
 }
 void showEvent(QShowEvent *e) override {QWidget::showEvent(e);syncBackdrops();}
 void hideEvent(QHideEvent *e) override {QWidget::hideEvent(e);
#ifdef _WIN32
  for(auto hwnd:backdrops_)ShowWindow(hwnd,SW_HIDE);
#endif
 }
 void moveEvent(QMoveEvent *e) override {QWidget::moveEvent(e);syncBackdrops();}
 void paintEvent(QPaintEvent *) override {
  QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(rect(),Qt::transparent);
  if(property("glowActive").toBool()){
   QPainterPath outside,interiors;outside.addRect(rect());for(const auto &card:cards())interiors.addRoundedRect(card,LookoutVisuals::PanelRadius,LookoutVisuals::PanelRadius);painter.save();painter.setClipPath(outside.subtracted(interiors));painter.setCompositionMode(QPainter::CompositionMode_SourceOver);for(const auto &entry:property("glowRegions").toList())LookoutVisuals::paintGlow(painter,entry.toRectF(),property("glowColor").value<QColor>(),property("glowStrength").toDouble(),property("glowIntensity").toDouble(),property("glowSpread").toDouble());painter.restore();
  }
  const int design=property("panelDesign").toInt();const double opacity=property("backgroundOpacity").isValid()?property("backgroundOpacity").toDouble():1;
  for(const auto &card:cards()){LookoutVisuals::paintCardSurface(painter,card,palette(),design,opacity);if(property("editTransforms").toBool()){painter.setPen(QPen(palette().color(QPalette::Mid),1));for(int n=0;n<2;++n){const int d=5+n*4;painter.drawLine(card.bottomRight()-QPointF(d,3),card.bottomRight()-QPointF(3,d));}}}
 }

public:
 int radius() const {return LookoutVisuals::PanelRadius;}
 QVector<QRectF> cards() const {
  QVector<QRectF> result;
  for(const auto &name:{"panelStats","panelText","panelThumbnail","panelBitrates","panelReplay","panelActiveIcons"})if(auto *item=findChild<QWidget*>(name);item && item->isVisibleTo(this))result<<QRectF(item->mapTo(this,QPoint()),item->size());
  for(auto *item:findChildren<QWidget*>(QRegularExpression("^historyGraph(s|[0-9]+)$")))if(auto *graphs=static_cast<HistoryGraphs*>(item);graphs->isVisibleTo(this)){const QPoint origin=graphs->mapTo(this,QPoint());for(auto card:graphs->cardRects())result<<card.translated(origin);}
  return result;
 }
 void updateCards(){
  QRegion region;for(const auto &card:cards())region+=card.toAlignedRect();if(auto *halo=findChild<QWidget*>("panelGlow");halo && !halo->isHidden())region+=halo->geometry();const bool changed=region!=lastCards_,blur=property("clipBlur").toBool(),publicCards=property("publicCards").toBool();if(mask()!=region)setMask(region);if(changed || lastBlur_!=blur || lastPublic_!=publicCards)syncBackdrops();if(changed)update(region+lastCards_);lastCards_=region;lastBlur_=blur;lastPublic_=publicCards;
 }
 ~NotificationPanel() override {
#ifdef _WIN32
  for(auto hwnd:backdrops_)DestroyWindow(hwnd);UnregisterClassW(L"LookoutCardBackdrop",GetModuleHandleW(nullptr));
#endif
 }
 NotificationPanel(QWidget *parent,Qt::WindowFlags flags):QWidget(parent,flags){setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_NoSystemBackground);}
};
// Alien child layer: glow shares the protected panel HWND and capture policy.
class GlowOutline final : public QWidget {
 void paintEvent(QPaintEvent *) override {} // Root paints the effect in its own backing store.
 void hideEvent(QHideEvent *event) override {QWidget::hideEvent(event);parentWidget()->setProperty("glowActive",false);parentWidget()->update();}
public:
 explicit GlowOutline(QWidget *owner):QWidget(owner){setObjectName("panelGlow");setAutoFillBackground(false);setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);hide();}
 void frame(const QColor &color,double strength,double intensity,int spread,const QVector<QRectF> &targets){
  auto *root=parentWidget();root->setProperty("glowColor",color);root->setProperty("glowStrength",strength);root->setProperty("glowIntensity",intensity);root->setProperty("glowSpread",spread);QVariantList regions;for(const auto &target:targets)regions<<target.translated(pos()+QPoint(spread+2,spread+2));root->setProperty("glowRegions",regions);root->update(geometry());
 }
};
// Native GPU surface: no frame download or CPU thumbnail conversion.
class Preview final : public QWidget {
 obs_display_t *display_ = nullptr;
 void roundPreview(){
#ifdef _WIN32
  const HWND hwnd=reinterpret_cast<HWND>(winId());if(IsWindow(hwnd)){const double scale=devicePixelRatioF();HRGN region=CreateRoundRectRgn(0,0,int(std::round(width()*scale))+1,int(std::round(height()*scale))+1,int(16*scale),int(16*scale));if(!SetWindowRgn(hwnd,region,TRUE))DeleteObject(region);return;}
#endif
  LookoutVisuals::roundWidget(this,LookoutVisuals::PreviewRadius);
 }
 static void draw(void *, uint32_t width, uint32_t height) {
  obs_video_info info{}; if (!obs_get_video_info(&info) || !info.base_width || !info.base_height) return;
  LookoutThumbnail::drawProgram(width,height,info);
 }
 void showEvent(QShowEvent *e) override {
  QWidget::showEvent(e);roundPreview();
#ifdef _WIN32
  if (!display_) {
   gs_init_data data{}; data.window.hwnd = reinterpret_cast<HWND>(winId());
   data.cx = uint32_t(width()*devicePixelRatioF()); data.cy = uint32_t(height()*devicePixelRatioF());
   data.num_backbuffers = 1; data.format = GS_BGRA; data.zsformat = GS_ZS_NONE;
   display_ = obs_display_create(&data, 0x171820);
   if(display_) obs_display_add_draw_callback(display_, draw, this);
  }
#endif
  if(display_) obs_display_set_enabled(display_, true);
 }
 void hideEvent(QHideEvent *e) override { if(display_) obs_display_set_enabled(display_, false); QWidget::hideEvent(e); }
 void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e);roundPreview(); if(display_) obs_display_resize(display_, uint32_t(width()*devicePixelRatioF()), uint32_t(height()*devicePixelRatioF())); }
public:
 explicit Preview(QWidget *parent):QWidget(parent) {setAttribute(Qt::WA_NativeWindow); setAttribute(Qt::WA_PaintOnScreen); setMinimumSize(1,1);}
 QPaintEngine *paintEngine() const override {return nullptr;}
 ~Preview() override {if(display_) {obs_display_remove_draw_callback(display_, draw, this); obs_display_destroy(display_);}}
};
}
LookoutDock::LookoutDock(const QString &file): settings_(file,QSettings::IniFormat) {
 setObjectName("lookout"); setMinimumSize(36,36);
 if(settings_.contains("backgroundBlur")){
  const int amount=settings_.value("backgroundBlur").toBool()?settings_.value("blurAmount",100).toInt():0;
  settings_.setValue("blurAmount",std::clamp(amount,0,100));
  settings_.remove("backgroundBlur");
 }
 if(!settings_.contains("panelScale")){if(settings_.contains("panelWidth")){settings_.setValue("panelScale",4);settings_.setValue("customScale",1);}else{auto *screen=QGuiApplication::primaryScreen();const int width=LookoutLayout::presetWidth(1,screen?screen->availableGeometry().size():QSize(1920,1080));settings_.setValue("panelScale",1);settings_.setValue("panelWidth",width+16);settings_.setValue("thumbnailWidth",std::max(80,width-16));}}

 settings_.remove("storageApproach");settings_.remove("qualityApproach");settings_.setValue("gib",std::clamp(settings_.value("gib",20).toDouble(),.1,10000.0));
 if(!settings_.contains("customSizePercent")){auto *screen=QGuiApplication::primaryScreen();const double normal=LookoutLayout::presetScale(1,screen?screen->availableGeometry().size():QSize(1920,1080));const double custom=settings_.value("customScale",normal).toDouble();settings_.setValue("customSizePercent",std::clamp(int(std::round(custom*100)),50,300));settings_.setValue("customPanelWidth",settings_.value("panelScale",1).toInt()==4?settings_.value("panelWidth",336):QVariant(int(320*custom)+16));settings_.setValue("customThumbnailWidth",settings_.value("panelScale",1).toInt()==4?settings_.value("thumbnailWidth",304):QVariant(std::max(80,int(320*custom)-16)));}
 // Remove obsolete prototype preferences; appearance now follows OBS entirely.
 for(const auto &key:QStringList{"customAccent","accentColor","thumbnail/monitor","thumbnail/corner","thumbnail/offsetX","thumbnail/offsetY","thumbnail/width"})settings_.remove(key);
 if(!settings_.contains("panelMode"))settings_.setValue("panelMode",!settings_.value("enabled",true).toBool()?2:settings_.value("persistent",false).toBool()?1:0);
 LookoutMigration::migrateShortcuts(settings_,names,HotkeyCount);
 settings_.remove("enabled");settings_.remove("persistent");
 for(const auto &option:dockButtonOptions_){
  auto *button=new QPushButton(this);
  button->setObjectName(option.id);
  button->setToolTip(option.label);
  button->setAccessibleName(option.label);
  button->setProperty("iconRole",option.icon);
  button->setIconSize(QSize(18,18));
  button->setFixedSize(DockButtonSize,DockButtonSize);
  button->setCheckable(option.action==DockThumbnail || option.action==DockMetrics || option.action==DockCapture);
  dockButtons_.insert(option.id,button);
  if(option.action==DockDisplayMode){
   auto *menu=new QMenu(button);
   auto *modes=new QActionGroup(menu);
   for(const auto &mode:displayModes_){
    auto *item=menu->addAction(QString::fromUtf8(mode.label));
    item->setData(mode.id);item->setCheckable(true);modes->addAction(item);
   }
   connect(menu,&QMenu::aboutToShow,this,[this,modes]{
    for(auto *item:modes->actions())item->setChecked(item->data().toInt()==displayMode());
   });
   connect(modes,&QActionGroup::triggered,this,[this](QAction *item){setDisplayMode(item->data().toInt());});
   button->setStyleSheet("QPushButton::menu-indicator { image: none; width: 0px; }");
   button->setMenu(menu);
  }else connect(button,&QPushButton::clicked,this,[this,action=option.action]{
   switch(action){
    case DockDismissAlerts:dismissAlert();break;
    case DockResetPosition:defaultPosition();break;
    case DockSettings:openSettings();break;
    default:activateAction(action);break;
   }
  });
 }
 dockButtons_["cyclePositionPresets"]->setEnabled(!settings_.value("positionPresets").toList().isEmpty());
 dockButtons_["dismissAlerts"]->setEnabled(false);
 applyDockOrder(settings_.value("dockButtons",defaultDockOrder()).toStringList());
 if(!settings_.value("universalMetricsVersion",0).toInt()){
  for(const auto &pair:{qMakePair(QString("storage"),QString("graphDisk")),qMakePair(QString("encoder"),QString("graphEncoder")),qMakePair(QString("network"),QString("graphNetwork")),qMakePair(QString("replay"),QString("showReplay"))})if(!settings_.value(pair.second,true).toBool())settings_.setValue("iconVisible/"+pair.first,false);
  for(const char *key:{"graphDisk","graphEncoder","graphNetwork","showReplay","bitrateRecording","bitrateStreaming"})settings_.remove(key);settings_.setValue("universalMetricsVersion",1);
 }
 const int previousDisplay=settings_.value("statsDisplay",0).toInt();
 if(previousDisplay==1 || previousDisplay==2 || previousDisplay==3 || previousDisplay==6 || previousDisplay==7){
  const int style=previousDisplay==1 || previousDisplay==2?GraphFull:GraphMini;
  for(int i=0;i<3;++i){
   const QString id=HistoryGraphs::metrics[i].id;
   if(metricEnabled(id))settings_.setValue("graph/"+id,style);
  }
  if(metricEnabled("bitrate") && (metricEnabled("recording") || metricEnabled("streaming")))
   settings_.setValue("graph/bitrate",style);
  if(metricEnabled("replay"))settings_.setValue("graph/replay",style);
  settings_.setValue("statsDisplay",0);
 }
 settings_.remove("activeIconsOnly");
 if(!settings_.contains("showThumbnail") && settings_.contains("compactThumbnail"))
  settings_.setValue("showThumbnail",settings_.value("compactThumbnail"));
 for(const auto &key:{"compactThumbnail","showStatus","displayFormatVersion"})settings_.remove(key);
 for(const char *key:{"animationStyle","animationDuration","animationFallDuration"})settings_.remove(key);
 captureStatus_="Capture exclusion: not checked";
 badge_=new NotificationPanel(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
 badge_->setObjectName("lookout");badge_->setWindowTitle("lookout panels");badge_->setAccessibleName("lookout status panels"); badge_->setAutoFillBackground(false); badge_->setAttribute(Qt::WA_ShowWithoutActivating); badge_->setFixedWidth(360);
 glow_=new GlowOutline(badge_);
 auto *panelRoot=new QVBoxLayout(badge_);panelRoot->setContentsMargins(8,8,8,8);panelRoot->setSpacing(8);

 panelLayout_=new QBoxLayout(QBoxLayout::TopToBottom);panelLayout_->setContentsMargins(0,0,0,0);panelLayout_->setSpacing(8);panelRoot->addLayout(panelLayout_);
 information_=new QWidget(badge_);auto *infoLayout=new QVBoxLayout(information_);infoLayout->setContentsMargins(0,0,0,0);infoLayout->setSpacing(8);
 badgeText_=new QLabel;badgeText_->setMargin(8);badgeText_->setWordWrap(true);badgeText_->setTextFormat(Qt::RichText);infoLayout->addWidget(badgeText_);
 stats_=new QWidget;stats_->setObjectName("panelStats");statsLayout_=new QGridLayout(stats_);statsLayout_->setContentsMargins(8,8,8,8);statsLayout_->setHorizontalSpacing(6);statsLayout_->setVerticalSpacing(2);infoLayout->addWidget(stats_);
 for(int i=0;i<StatCount;++i){statIcons_[i]=new QLabel(stats_);statIcons_[i]->setFixedSize(14,14);statNames_[i]=new QLabel(statDefinitions_[i].label.compact,stats_);statNames_[i]->setAccessibleName(statDefinitions_[i].label.full);statNames_[i]->setWordWrap(true);statValues_[i]=new QLabel(stats_);statValues_[i]->setAlignment(Qt::AlignRight|Qt::AlignVCenter);statValues_[i]->setWordWrap(true);}
 activeIcons_=new StatusIconsCard(information_);activeIcons_->setObjectName("panelActiveIcons");infoLayout->addWidget(activeIcons_);activeIcons_->hide();iconPop_=new QVariantAnimation(this);iconPop_->setDuration(240);iconPop_->setStartValue(0.0);iconPop_->setEndValue(1.0);connect(iconPop_,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){activeIcons_->setPulse(value.toDouble());});
 bitrates_=new BitrateCard(information_);bitrates_->setObjectName("panelBitrates");bitrates_->setProperty("readingsInStats",true);infoLayout->addWidget(bitrates_);bitrates_->hide();
 replay_=new ReplayCard(information_);replay_->setObjectName("panelReplay");replay_->setProperty("readingsInStats",true);infoLayout->addWidget(replay_);replay_->hide();
 graphs_=new HistoryGraphs(information_);graphs_->setObjectName("historyGraphs");infoLayout->addWidget(graphs_);graphs_->hide();historyClock_.start();
 for(int metric=0;metric<HistoryGraphs::Count;++metric){graphViews_[metric]=new HistoryGraphs(badge_);graphViews_[metric]->setObjectName(QString("historyGraph%1").arg(metric));graphViews_[metric]->setHistorySource(graphs_);graphViews_[metric]->hide();}
 configureGraphs();
 badge_->setObjectName("lookoutPanel");badge_->installEventFilter(this);
badgeText_->setObjectName("panelText");
 previewCard_=new QWidget(badge_);previewCard_->setObjectName("panelThumbnail");auto *previewLayout=new QVBoxLayout(previewCard_);previewLayout->setContentsMargins(8,8,8,8);previewLayout->setSpacing(0);preview_=new Preview(previewCard_);previewLayout->addWidget(preview_);preview_->setObjectName("panelPreview");
 alertIcon_=new QLabel(preview_);alertIcon_->setObjectName("thumbnailAlertIcon");alertIcon_->setAttribute(Qt::WA_NativeWindow);alertIcon_->setFixedSize(32,32);alertIcon_->setAttribute(Qt::WA_TransparentForMouseEvents);alertIcon_->hide();
 panelLayout_->addWidget(information_);panelLayout_->addWidget(previewCard_);preview_->hide();previewCard_->hide();

 for(auto *widget:badge_->findChildren<QWidget*>())widget->installEventFilter(this);
 cardLayoutTimer_.setSingleShot(true);cardLayoutTimer_.setInterval(0);connect(&cardLayoutTimer_,&QTimer::timeout,this,[this]{if(ready_){static_cast<NotificationPanel*>(badge_)->updateCards();updateGlow();}});
 transformTimer_.setSingleShot(true);connect(&transformTimer_,&QTimer::timeout,this,&LookoutDock::saveTransform);
 glowClock_.start();glowTimer_.setInterval(33);connect(&glowTimer_,&QTimer::timeout,this,&LookoutDock::updateGlow);
 updateIcons();
 dismiss_.setSingleShot(true); connect(&dismiss_,&QTimer::timeout,this,[this]{alerts_.clear();alertActive_=false;sessionNotice_=false;refreshPanel();});
 thumbnailAlertPreview_.setSingleShot(true);
 thumbnailAlertPreview_.setInterval(10000);
 connect(&thumbnailAlertPreview_,&QTimer::timeout,this,&LookoutDock::refreshPanel);
 for(int i=0;i<HotkeyCount;i++) {
  keyData_[i]={this,i}; keys_[i]=obs_hotkey_register_frontend(names[i],descriptions[i],hotkey,&keyData_[i]);
  auto bytes=settings_.value(names[i]).toByteArray();
  obs_data_t *data=bytes.isEmpty()?nullptr:obs_data_create_from_json(bytes.constData());
  if(data) {obs_data_array_t *a=obs_data_get_array(data,"bindings"); if(a){obs_hotkey_load(keys_[i],a); obs_data_array_release(a);} obs_data_release(data);}
 }
 pool_.setMaxThreadCount(1);
 obs_frontend_add_event_callback(event,this);
 // post_load runs before OBS creates its output handler. Wait for its explicit
 // readiness event; neither an arbitrary delay nor a non-null main window suffices.
 setEnabled(false);
 connect(&timer_,&QTimer::timeout,this,&LookoutDock::tick);
}
LookoutDock::~LookoutDock() {
 delete quick_;quick_=nullptr;
 os_cpu_usage_info_destroy(cpuSampler_);
 detachReplay();iconPop_->stop();timer_.stop();glowTimer_.stop();transformTimer_.stop();cardLayoutTimer_.stop();badge_->removeEventFilter(this);if(dialog_)dialog_->removeEventFilter(this); obs_frontend_remove_event_callback(event,this); saveKeys();
 for(auto key:keys_) obs_hotkey_unregister(key);
 pool_.waitForDone(); if(lastOutput_) obs_output_release(lastOutput_);
 delete dialog_.data(); if(lastStream_)obs_output_release(lastStream_); delete glow_;delete badge_;
}
void LookoutDock::resizeEvent(QResizeEvent *event){
 QWidget::resizeEvent(event);layoutDockActions();
}
QStringList LookoutDock::defaultDockOrder(){
 QStringList order;
 for(const auto &option:dockButtonOptions_)if(option.defaultVisible)order.append(option.id);
 return order;
}
QSize LookoutDock::sizeHint() const {
 const int rows=std::max(1,(int(dockOrder_.size())+2)/3);
 return QSize(100,2*DockMargin+rows*DockButtonSize+(rows-1)*DockSpacing);
}
void LookoutDock::applyDockOrder(const QStringList &order){
 dockOrder_.clear();
 for(const auto &id:order)if(dockButtons_.contains(id) && !dockOrder_.contains(id))dockOrder_.append(id);
 for(auto it=dockButtons_.cbegin();it!=dockButtons_.cend();++it)
  it.value()->setVisible(dockOrder_.contains(it.key()));
 for(int i=1;i<dockOrder_.size();++i)setTabOrder(dockButtons_[dockOrder_[i-1]],dockButtons_[dockOrder_[i]]);
 layoutDockActions();updateGeometry();
}
void LookoutDock::editDockOrder(const QString &id,const QString &operation){
 QStringList order=dockOrder_;
 if(operation=="reset")order=defaultDockOrder();
 else if(dockButtons_.contains(id)){
  const int index=order.indexOf(id);
  if(operation=="add" && index<0)order.append(id);
  else if(operation=="remove" && index>=0)order.removeAt(index);
  else if(operation=="up" && index>0)order.swapItemsAt(index,index-1);
  else if(operation=="down" && index>=0 && index+1<order.size())order.swapItemsAt(index,index+1);
 }
 if(order==dockOrder_)return;
 settings_.setValue("dockButtons",order);settings_.sync();
 applyDockOrder(order);
}
void LookoutDock::layoutDockActions(){
 const int count=int(dockOrder_.size());
 if(count==0){setMinimumHeight(2*DockMargin);return;}
 const int pitch=DockButtonSize+DockSpacing;
 const int columns=std::clamp((width()-2*DockMargin+DockSpacing)/pitch,1,count);
 const int left=(width()-(columns*pitch-DockSpacing))/2;
 for(int i=0;i<count;++i)dockButtons_[dockOrder_[i]]->move(left+(i%columns)*pitch,DockMargin+(i/columns)*pitch);
 const int rows=(count+columns-1)/columns;
 setMinimumHeight(2*DockMargin+rows*pitch-DockSpacing);
}
void LookoutDock::updateIcons(){
 updateBranding();
 const bool dark=obs_frontend_is_theme_dark();
 if(iconsInitialized_ && iconsDark_==dark)return;
 iconsInitialized_=true;iconsDark_=dark;
 for(auto *button:findChildren<QAbstractButton*>())if(button->property("iconRole").isValid())button->setIcon(uiIcon(button->property("iconRole").toString()));
 for(int i=0;i<StatCount;++i)statIcons_[i]->setPixmap(uiIcon(statDefinitions_[i].icon).pixmap(14,14));
 if(dialog_){if(auto *tabs=dialog_->findChild<QTabWidget*>()){tabs->setTabIcon(0,uiIcon("thumbnail"));tabs->setTabIcon(1,uiIcon("alerts"));tabs->setTabIcon(2,uiIcon("hotkeys"));tabs->setTabIcon(3,uiIcon("about"));}if(auto *sections=dialog_->findChild<QTabWidget*>("appearanceSections")){sections->setTabIcon(0,uiIcon("layout"));sections->setTabIcon(1,uiIcon("settings"));sections->setTabIcon(2,uiIcon("graphs"));}}
}
bool LookoutDock::darkLogo() const {
 const bool followsObs=quick_?quick_->panelDesign()=="obs":settings_.value("graphAppearance",1).toInt()==0;
 if(followsObs)return obs_frontend_is_theme_dark();
 const int mode=std::clamp(settings_.value("logoTheme",0).toInt(),0,2);if(mode)return mode==2;
#ifdef _WIN32
 QSettings system("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",QSettings::NativeFormat);return !system.value("AppsUseLightTheme",1).toBool();
#else
 return QApplication::palette().color(QPalette::Window).lightness()<128;
#endif
}
void LookoutDock::updateBranding(bool force){
 const bool dark=darkLogo();const qreal dpr=devicePixelRatioF(),dialogDpr=dialog_?dialog_->devicePixelRatioF():0;if(!force && brandingInitialized_ && brandingDark_==dark && qFuzzyCompare(brandingDpr_,dpr) && qFuzzyCompare(brandingDialogDpr_,dialogDpr)){if(auto *dock=qobject_cast<QDockWidget*>(parentWidget()))if(dock->windowIcon().cacheKey()!=windowIcon().cacheKey())dock->setWindowIcon(windowIcon());return;}brandingInitialized_=true;brandingDark_=dark;brandingDpr_=dpr;brandingDialogDpr_=dialogDpr;
 const QIcon icon=LookoutBrand::icon(dark,64,dpr);setWindowIcon(icon);if(auto *dock=qobject_cast<QDockWidget*>(parentWidget()))dock->setWindowIcon(icon);

 if(badge_)badge_->setWindowIcon(icon);
 if(dialog_){dialog_->setWindowIcon(icon);if(auto *pixels=dialog_->findChild<QDialog*>("pixelControls"))pixels->setWindowIcon(icon);if(auto *image=dialog_->findChild<QLabel*>("lookoutBadge"))image->setPixmap(LookoutBrand::badge(dark,80,dialogDpr));if(auto *about=dialog_->findChild<QTabWidget*>())about->setTabIcon(3,icon);}
 setProperty("logoDark",dark);if(dialog_)dialog_->setProperty("logoDark",dark);
}
void LookoutDock::saveKeys() {
 for(int i=0;i<HotkeyCount;i++){auto *a=obs_hotkey_save(keys_[i]); auto *d=obs_data_create(); obs_data_set_array(d,"bindings",a); settings_.setValue(names[i],QByteArray(obs_data_get_json(d))); obs_data_release(d); obs_data_array_release(a);} settings_.sync();
}
void LookoutDock::hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
 if(!pressed)return; auto key=*static_cast<Key*>(data);
 QMetaObject::invokeMethod(key.self,[key]{if(!key.self->ready_ || (key.self->quick_ && key.self->quick_->capturingShortcut()))return;if(auto *focus=QApplication::focusWidget();focus && focus->property("lookoutHotkeyEditor").toBool())return;key.self->activateAction(key.action);},Qt::QueuedConnection);
}
void LookoutDock::event(obs_frontend_event e, void *data) {
 auto *self=static_cast<LookoutDock*>(data);
 if(e==OBS_FRONTEND_EVENT_EXIT || e==OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
  self->detachReplay();self->ready_=false;  self->timer_.stop(); self->dismiss_.stop();
  self->hidePanel();if(self->quick_)self->quick_->stop(); self->setEnabled(false); self->saveKeys(); return;
 }
 if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING) {
  self->ready_=true; self->setEnabled(true); self->timer_.start(2000); self->tick(); return;
 }
 if(!self->ready_) return;
 if(e==OBS_FRONTEND_EVENT_THEME_CHANGED){
  QMetaObject::invokeMethod(self,[self]{
   if(!self->ready_)return;
   self->updateIcons();
   self->refreshPanel();
  },Qt::QueuedConnection);
  return;
 }
 QString text;
 switch(e){case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:text="Replay buffer started";break;case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:text="Replay buffer stopped";break;case OBS_FRONTEND_EVENT_RECORDING_STARTED:text="Recording started";break;case OBS_FRONTEND_EVENT_RECORDING_STOPPED:text="Recording stopped";break;case OBS_FRONTEND_EVENT_STREAMING_STARTED:text="Stream started";break;case OBS_FRONTEND_EVENT_STREAMING_STOPPED:text="Stream stopped";break;case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:text="Virtual camera started";break;case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:text="Virtual camera stopped";break;default:return;}
 QMetaObject::invokeMethod(self,[self,text,e]{
  if(!self->ready_)return;
  if(e==OBS_FRONTEND_EVENT_RECORDING_STARTED || e==OBS_FRONTEND_EVENT_RECORDING_STOPPED){self->recordingSampler_.reset();self->recordingBitrate_.reset();self->recordingSpace_.update(std::nullopt);}
  if(e==OBS_FRONTEND_EVENT_STREAMING_STARTED || e==OBS_FRONTEND_EVENT_STREAMING_STOPPED){self->streamingSampler_.reset();self->streamingBitrate_.reset();}
  bool failure=false;
  if(e==OBS_FRONTEND_EVENT_STREAMING_STOPPED && self->settings_.value("network",true).toBool()){
   auto *out=obs_frontend_get_streaming_output();
   if(out){QString error=QString::fromUtf8(obs_output_get_last_error(out));obs_output_release(out);
    if(!error.isEmpty()){self->notify("Stream stopped with an error",error,true);failure=true;}}
  }
  if(!failure && self->settings_.value("lifecycle",true).toBool()) self->notify("Session update",text);
  self->tick();},Qt::QueuedConnection);
}
void LookoutDock::place(QWidget *window,const QString &prefix){
 if(independentLayout_){placeIndependent();return;}
 auto screens=QGuiApplication::screens();
 auto *screen=screens.value(settings_.value(prefix+"monitor",0).toInt(),QGuiApplication::primaryScreen());
 if(!screen)return;
 const QRect r=screen->availableGeometry();
 const int halo=overlayPadding();window->layout()->setContentsMargins(8+halo,8+halo,8+halo,8+halo);
 placing_=true;
 const bool showMetrics=!information_->isHidden();
 const bool showThumbnail=!previewCard_->isHidden();
 const int position=std::clamp(settings_.value("thumbnailPosition",0).toInt(),0,3);
 const int panelWidth=iconStripMode()?std::max(32,activeIcons_->sizeHint().width()+16):
  std::clamp(settings_.value("panelWidth",520).toInt(),128,960);
 const int spacing=panelLayout_->spacing();
 const auto widths=LookoutLayout::groupedWidths(r.width(),panelWidth,
  std::clamp(settings_.value("thumbnailWidth",320).toInt(),80,800),spacing,
  position>=2,showMetrics,showThumbnail);
 const int actualWidth=widths.window+2*halo;
 if(window->minimumWidth()!=actualWidth || window->maximumWidth()!=actualWidth)window->setFixedWidth(actualWidth);
 if(widths.alongside)panelLayout_->setDirection(position==2?QBoxLayout::RightToLeft:QBoxLayout::LeftToRight);
 else panelLayout_->setDirection(position==0 || position==2?QBoxLayout::BottomToTop:QBoxLayout::TopToBottom);
 panelLayout_->setAlignment(previewCard_,Qt::AlignTop);
 panelLayout_->setAlignment(information_,Qt::AlignTop);
 if(showMetrics && information_->minimumWidth()!=widths.metrics)information_->setFixedWidth(widths.metrics);
 layoutStats(widths.metrics);
 // The hidden widget tree still needs to report the new size of restored rows.
 statsLayout_->invalidate();
 stats_->updateGeometry();
 bitrates_->updateGeometry();replay_->updateGeometry();activeIcons_->updateGeometry();
 auto *informationLayout=information_->layout();
 informationLayout->invalidate();
 if(showThumbnail){
  const int infoHeight=!showMetrics || widths.alongside?0:spacing+
   (informationLayout->hasHeightForWidth()?informationLayout->totalHeightForWidth(widths.metrics):informationLayout->totalSizeHint().height());
  const int maxVideoHeight=std::max(1,r.height()-32-infoHeight);
  const int width=std::min(widths.thumbnail,maxVideoHeight*16/9);
  preview_->setFixedSize(width,std::max(1,width*9/16));
  previewCard_->setFixedSize(width+16,preview_->height()+16);
  updateAlertIcon();
 }
 information_->updateGeometry();
 panelLayout_->invalidate();
 window->layout()->invalidate();
 const int height=window->layout()->hasHeightForWidth()?window->layout()->totalHeightForWidth(actualWidth):window->layout()->totalSizeHint().height();
 const QSize target(actualWidth,height);
 if(window->size()!=target)window->resize(target);
 window->layout()->setGeometry(window->rect());
 informationLayout->setGeometry(information_->rect());
 statsLayout_->setGeometry(stats_->rect());
 const int corner=std::clamp(settings_.value(prefix+"corner",prefix.isEmpty()?1:3).toInt(),0,3);
 const int dx=std::max(0,settings_.value(prefix+"offsetX",24).toInt());
 const int dy=std::max(0,settings_.value(prefix+"offsetY",24).toInt());
 const int right=std::max(r.left(),r.right()-widths.window+1);
 const int bottom=std::max(r.top(),r.bottom()-(window->height()-2*halo)+1);
 const int x=corner%2?right-dx:r.left()+dx;
 const int y=corner<2?r.top()+dy:bottom-dy;
 const bool custom=settings_.value("customPosition",false).toBool();
 static_cast<NotificationPanel*>(badge_)->updateCards();
 window->move(std::clamp(custom?settings_.value("positionX",x).toInt():x,r.left(),right)-halo,std::clamp(custom?settings_.value("positionY",y).toInt():y,r.top(),bottom)-halo);
 placing_=false;
}
QString LookoutDock::overlayBlockReason() const {
#ifdef _WIN32
 QUERY_USER_NOTIFICATION_STATE state{};
 if(SUCCEEDED(SHQueryUserNotificationState(&state)) && state==QUNS_RUNNING_D3D_FULL_SCREEN)
  return "Panels paused during exclusive fullscreen";
#endif
 if(settings_.value("showInCaptures",false).toBool())return {};
 bool unverifiedDisplayCapture=false;
 obs_enum_sources([](void *data,obs_source_t *source){
  if(!obs_source_showing(source) || qstrcmp(obs_source_get_unversioned_id(source),"monitor_capture")!=0)
   return true;
  // OBS Display Capture's Automatic mode can use DXGI. Require explicit WGC
  // before showing private panels alongside a visible desktop capture.
  constexpr long long windowsGraphicsCapture=2;
  auto *settings=obs_source_get_settings(source);
  const bool verified=settings && obs_data_get_int(settings,"method")==windowsGraphicsCapture;
  if(settings)obs_data_release(settings);
  if(!verified)*static_cast<bool*>(data)=true;
  return verified;
 },&unverifiedDisplayCapture);
 return unverifiedDisplayCapture
  ? QStringLiteral("Private panels paused: Display Capture uses Automatic or Legacy (DXGI)")
  : QString{};
}
bool LookoutDock::showProtected(QWidget *window){
 if(!ready_)return false;
 const bool publicPanel=settings_.value("showInCaptures",false).toBool();
 const auto blockReason=overlayBlockReason();
 if(!blockReason.isEmpty()){
  window->hide();setCaptureStatus(blockReason);return false;
 }
 window->winId();
#ifdef _WIN32
 HWND hwnd=reinterpret_cast<HWND>(window->winId());DWORD affinity=0;
 const DWORD requested=publicPanel?0:0x11;
 const bool alreadySet=GetWindowDisplayAffinity(hwnd,&affinity) && affinity==requested;
 if(!alreadySet && (!SetWindowDisplayAffinity(hwnd,requested) || !GetWindowDisplayAffinity(hwnd,&affinity) || affinity!=requested)){
  window->hide();setCaptureStatus("Panel hidden: capture visibility could not be verified");return false;
 }
 setCaptureStatus(publicPanel?"Panels can appear in desktop capture":"Windows capture exclusion is on");
 if(!window->isVisible()){placing_=true;window->show();placing_=false;}return true;
#else
 if(!publicPanel){window->hide();setCaptureStatus("Private panel requires Windows");return false;}
 if(!window->isVisible()){placing_=true;window->show();placing_=false;}return true;
#endif
}
bool LookoutDock::metricEnabled(const QString &id) const {return settings_.value(id=="bitrate"?"showBitrate":"iconVisible/"+id,true).toBool();}
bool LookoutDock::hasReading(int stat) const {return metricEnabled(statDefinitions_[stat].id);}
int LookoutDock::displayMode() const {
 const int mode=settings_.value("statsDisplay",0).toInt();
 return mode==4 || mode==5?mode:0;
}
LookoutDock::GraphMode LookoutDock::graphMode(const QString &id) const {
 return GraphMode(std::clamp(settings_.value("graph/"+id,0).toInt(),0,2));
}
void LookoutDock::setDisplayMode(int mode){
 for(const auto &option:displayModes_)if(option.id==mode){
  settings_.setValue("statsDisplay",mode);
  metricsVisible_=true;
  settings_.sync();
  refreshPanel();
  return;
 }
}
bool LookoutDock::iconStripMode() const {return displayMode()==4 || displayMode()==5;}
bool LookoutDock::graphsEnabled() const {
 return std::any_of(std::begin(HistoryGraphs::metrics),std::end(HistoryGraphs::metrics),
  [this](const auto &metric){return graphMode(metric.id)!=GraphOff;});
}
void LookoutDock::layoutStats(int){
 int position=0;
 for(int i=0;i<StatCount;++i){
  for(auto *widget:{statIcons_[i],statNames_[i],statValues_[i]})statsLayout_->removeWidget(widget);
  const bool selected=hasReading(i);
  statIcons_[i]->setVisible(selected);statNames_[i]->setVisible(selected);statValues_[i]->setVisible(selected);
  if(!selected)continue;
  statsLayout_->addWidget(statIcons_[i],position,0);
  statsLayout_->addWidget(statNames_[i],position,1);
  statsLayout_->addWidget(statValues_[i],position,2);
  ++position;
 }
}
void LookoutDock::updateStatusIcons(){
 const bool enabled=iconStripMode() && metricsVisible_;QVector<StatusIconsCard::AlertIcon> icons;QStringList fingerprint;auto visible=[this](const QString &id){return metricEnabled(id);};
 auto append=[&](const QString &title,const QString &body,bool preview){QString role=title.contains("encoder",Qt::CaseInsensitive)?"encoder":title.contains("storage",Qt::CaseInsensitive) || title.contains("disk",Qt::CaseInsensitive)?"storage":title.contains("stream",Qt::CaseInsensitive) || title.contains("network",Qt::CaseInsensitive)?"network":"alerts";if(!preview && !visible(role))return;const int stat=role=="storage"?1:role=="encoder"?3:5;const double risk=preview?glowPreviewRisk_:metricRisk(stat);QColor color=title=="Stream reconnected"?badge_->palette().color(QPalette::WindowText):warningColor(risk>=0?risk:title.contains("running low",Qt::CaseInsensitive)?0:1);icons<<StatusIconsCard::AlertIcon{role,uiIcon(role,badge_->palette().color(QPalette::Window).lightness()<128),color,(preview?"Preview: ":QString())+title+": "+body};fingerprint<<role+color.name()+title;};
 if(enabled){for(auto it=alerts_.cbegin();it!=alerts_.cend();++it)append(it.key(),it.value(),false);if(glowPreviewUntil_>glowClock_.elapsed() && !previewAlertTitle_.isEmpty())append(previewAlertTitle_,previewAlertBody_,true);}
 QVector<StatusIconsCard::AlertIcon> statuses;const bool all=settings_.value("statsDisplay",0).toInt()==5;const bool dark=badge_->palette().color(QPalette::Window).lightness()<128;const QColor inactive=badge_->palette().color(QPalette::PlaceholderText),normal=badge_->palette().color(QPalette::WindowText);
 auto status=[&](QString id,QString icon,bool active,QColor color,QString label,bool always){if(enabled && visible(id) && (always || active))statuses<<StatusIconsCard::AlertIcon{id,uiIcon(icon,dark),active?color:inactive,label};};
 status("recording","record",recordingActive_,QColor("#ff4b55"),recordingActive_?"Recording active":"Recording stopped",all);
 if(all && enabled && visible("storage"))statuses<<StatusIconsCard::AlertIcon{"storage",uiIcon("storage",dark),normal,storageText_};
 status("streaming","stream",streamingActive_,QColor("#40baf0"),streamingActive_?"Streaming active":"Streaming offline",all);
 if(all && enabled){if(visible("encoder"))statuses<<StatusIconsCard::AlertIcon{"encoder",uiIcon("encoder",dark),normal,"Encoder"};if(visible("network"))statuses<<StatusIconsCard::AlertIcon{"network",uiIcon("network",dark),normal,networkText_};}
 status("camera","camera",cameraActive_,QColor("#b38bff"),cameraActive_?"Virtual camera active":"Virtual camera off",all);
 status("replay","replay",replayActive_,QColor("#65d8dc"),replayActive_?"Replay buffer active":"Replay buffer stopped",all);
 QStringList order;
 for(int i=0;i<StatCount;++i){
  const auto &metric=statDefinitions_[i];order.append(metric.id);
  if(i>=CpuStat && i<ReplayStat && all && enabled && visible(metric.id))
   statuses<<StatusIconsCard::AlertIcon{metric.id,uiIcon(metric.icon,dark),normal,
     QString::fromUtf8(metric.label.full)+": "+statValues_[i]->text()};
 }
 std::stable_sort(statuses.begin(),statuses.end(),[&](const auto &a,const auto &b){return order.indexOf(a.id)<order.indexOf(b.id);});
 activeIcons_->setPersistentIcons(statuses);activeIcons_->setScale(panelScale());activeIcons_->setState(false,false,icons);const QString next=fingerprint.join("|");if(next!=iconFingerprint_ && !icons.isEmpty()){iconPop_->stop();activeIcons_->setPulse(0);iconPop_->start();}iconFingerprint_=next;
}
void LookoutDock::updateStats(){
 auto percent=[](const QString &text){const int split=text.indexOf(QString::fromUtf8(" · "));const QString value=split>=0?text.mid(split+3):text;return value.contains('%')?value.section('%',0,0)+"%":value;};
 const double diskGiB=double(freeBytes_)/(1024*1024*1024);const QString disk=freeBytes_<0?"Unavailable":freeBytes_>0 && diskGiB<.1?"<0.1 GiB":QString("%1 GiB").arg(diskGiB,0,'f',1);
 QString diskTime="Idle";
 if(recordingActive_){
  if(freeBytes_==0)diskTime="0 min";
  else if(recordingPaused_)diskTime="Paused";
  else if(freeBytes_<0)diskTime="Unavailable";
  else if(const auto seconds=recordingSpace_.secondsRemaining(freeBytes_)){
   if(*seconds<60)diskTime="<1 min";
   else if(*seconds>=365*86400.0)diskTime="≥365 d";
   else {
    const int minutes=int(*seconds/60);
    if(minutes>=1440)diskTime=QString("≈%1 d %2 h").arg(minutes/1440).arg(minutes/60%24);
    else if(minutes>=60)diskTime=QString("≈%1 h %2 min").arg(minutes/60).arg(minutes%60);
    else diskTime=QString("≈%1 min").arg(minutes);
   }
  }else diskTime="Calculating…";
 }
 const QString cpu=cpuUsage_?QString("%1%").arg(*cpuUsage_,0,'f',1):"—";
 const QString memory=residentBytes_?QString("%1 MiB").arg(double(residentBytes_)/1048576,0,'f',1):"—";
 const auto fill=replay_->fill();
 const QString replay=!replayActive_?"Inactive":fill.known?QString("≈%1%").arg(fill.percent,0,'f',0):"Collecting…";
 auto rate=[](std::optional<double> bps){
  if(!bps)return QString("Measuring…");
  if(*bps<1e6)return QString("%1 kb/s").arg(*bps/1000,0,'f',0);
  return QString("%1 Mb/s").arg(*bps/1e6,0,'f',1);
 };
 QString bitrate="No active outputs";
 if(recordingActive_ && streamingActive_)
  bitrate=QString("R %1 · S %2").arg(rate(recordingBitrate_),rate(streamingBitrate_));
 else if(recordingActive_ || streamingActive_)
  bitrate=rate(recordingActive_?recordingBitrate_:streamingBitrate_);
 const QStringList values={recordingActive_?"Active":"Inactive",disk,streamingActive_?"LIVE":"Inactive",
   percent(encoderText_),cameraActive_?"Active":"Inactive",percent(networkText_),cpu,memory,diskTime,bitrate,replay};
 for(int i=0;i<StatCount;++i)statValues_[i]->setText(values[i]);
 applyMetricColors();

}
void LookoutDock::refreshPanel(){
 if(dragging_ || transformTimer_.isActive())return;
 const bool iconsOnly=iconStripMode();
 setIndependentLayout(settings_.value("independentCards",false).toBool() && !iconsOnly);configureGraphs();applyPanelAppearance();syncVisibilityControls();
 const bool previewing=glowPreviewUntil_>glowClock_.elapsed();
 const bool thumbnailPreview=thumbnailAlertPreview_.isActive();
 const bool transitionPreview=quick_ && quick_->transitionPreviewActive();
 if(!ready_ || (panelMode()==2 && !previewing && !thumbnailPreview && !transitionPreview && !editMode_)){if(panelMode()==2){dismiss_.stop();alerts_.clear();alertActive_=false;sessionNotice_=false;glowPreviewUntil_=0;}syncVisibilityControls();hidePanel();return;}
 const bool alertPreview=previewing && !previewAlertTitle_.isEmpty(),showMetrics=metricsVisible_;
 const bool anyStatus=std::any_of(std::begin(statDefinitions_),std::end(statDefinitions_),
   [this](const auto &metric){return metricEnabled(metric.id);});
 const bool thumbnail=thumbnailDisplayed();preview_->setVisible(thumbnail);previewCard_->setVisible(thumbnail);updateAlertIcon();
 const bool thumbnailAlert=!thumbnailPreview && (alertActive_ || alertPreview) && thumbnail && (!showMetrics || !anyStatus || iconsOnly) && !alertIcon_->isHidden() && settings_.value("showAlertIcon",true).toBool();
 updateStats();updateStatusIcons();activeIcons_->setVisible(iconsOnly && showMetrics && activeIcons_->iconCount()>0);
 stats_->setVisible(!iconsOnly && anyStatus && (showMetrics || (previewing && !alertPreview)));
 replay_->setVisible(!iconsOnly && showMetrics && graphMode("replay")!=GraphOff);
 graphs_->setVisible(!iconsOnly && showMetrics && graphsEnabled() && !independentLayout_);
 for(int metric=0;metric<HistoryGraphs::Count;++metric)
  graphViews_[metric]->setVisible(!iconsOnly && independentLayout_ && showMetrics && graphMode(HistoryGraphs::metrics[metric].id)!=GraphOff);
 updateBitrateCard();
 QStringList callouts;for(auto it=alerts_.cbegin();it!=alerts_.cend();++it){QString detail=it.value().toHtmlEscaped();detail.replace("\n","<br/>");callouts<<"<b>"+it.key().toHtmlEscaped()+"</b><br/>"+detail;}
 if(alertPreview)callouts<<"<b>Preview · "+previewAlertTitle_.toHtmlEscaped()+"</b><br/>"+previewAlertBody_.toHtmlEscaped();
 badgeText_->setText(callouts.join("<br/><br/>"));badgeText_->setVisible(!iconsOnly && !callouts.isEmpty() && !thumbnailAlert);
 bool infoVisible=false;for(auto *card:QVector<QWidget*>{stats_,replay_,bitrates_,graphs_,badgeText_,activeIcons_})infoVisible|=!card->isHidden();information_->setVisible(infoVisible);
 bool contentVisible=thumbnail || infoVisible;for(auto *view:graphViews_)contentVisible|=!view->isHidden();
 if(transitionPreview && !contentVisible){
  badgeText_->setText("Transition preview");
  badgeText_->show();
  information_->show();
  contentVisible=true;
 }
 const bool idleHidden=(!editMode_ || editHidden_) && !previewing && !thumbnailPreview && !transitionPreview && !alertActive_ && !sessionNotice_ && panelMode()!=1 && !dismiss_.isActive();
 if(!contentVisible || idleHidden){hidePanel();return;}place(badge_,"");presentPanel();updateGlow();
}

void LookoutDock::dismissAlert(){dismiss_.stop();alerts_.clear();alertActive_=false;sessionNotice_=false;refreshPanel();}
void LookoutDock::notify(const QString &title,const QString &text,bool critical){
 if(!ready_ || panelMode()==2)return;
 if(title=="Session update"){sessionNotice_=true;if(settings_.value("untilDismiss",false).toBool())dismiss_.stop();else if(!alertActive_)dismiss_.start(settings_.value("seconds",6).toInt()*1000);refreshPanel();return;}
 if(!settings_.value("untilDismiss",false).toBool())alerts_.clear();
 alerts_.insert(title,text);lastAlertTitle_=title;lastAlertBody_=text;alertActive_=true;
 if(settings_.value("untilDismiss",false).toBool())dismiss_.stop();
 else dismiss_.start(settings_.value("seconds",6).toInt()*1000);
 refreshPanel();
}
void LookoutDock::tick(){
 if(!ready_)return;
 updateIcons();refreshHotkeyEditors();
 if(cpuSampler_){
  const double usage=os_cpu_usage_info_query(cpuSampler_);
  if(std::isfinite(usage))cpuUsage_=std::clamp(usage,0.0,100.0);
 }else cpuSampler_=os_cpu_usage_info_start();
 residentBytes_=os_get_proc_resident_size();
 updateReplay();recordingActive_=obs_frontend_recording_active();streamingActive_=obs_frontend_streaming_active();cameraActive_=obs_frontend_virtualcam_active();
 auto *config=obs_frontend_get_profile_config();
 if(!config) return;
 const char *mode=config_get_string(config,"Output","Mode");
 const bool advanced=mode && strcmp(mode,"Advanced")==0;
 const char *type=config_get_string(config,"AdvOut","RecType");
 const bool ffmpeg=advanced && type && strcmp(type,"FFmpeg")==0;
 QString currentPath=QString::fromUtf8(config_get_string(config,advanced?"AdvOut":"SimpleOutput",advanced?(ffmpeg?"FFFilePath":"RecFilePath"):"FilePath"));
 if(ffmpeg && !config_get_bool(config,"AdvOut","FFOutputToFile")) currentPath.clear();
 if(currentPath!=storagePath_){storagePath_=currentPath;freeBytes_=-1;storageAlarm_=false;storageFull_=false;probeTicks_=0;storageUnavailable_=false;recordingSpace_.update(std::nullopt);graphs_->clearHistory();}
 bool unavailableNotice=false;{
  std::lock_guard<std::mutex> lock(mutex_);
  if(resultReady_){
   if(resultPath_==storagePath_){freeBytes_=resultBytes_;
    unavailableNotice=freeBytes_<0 && !storageUnavailable_ && !storagePath_.isEmpty() && settings_.value("storage",true).toBool();
    storageUnavailable_=freeBytes_<0;
   }
   resultReady_=false;checking_=false;
  }
 }
 if(unavailableNotice)notify("Recording storage unavailable","The recording destination could not be checked. Verify the drive is connected and accessible.",true);
 if(!checking_ && probeTicks_++%5==0){
  QString path=storagePath_;
  checking_=true;
  pool_.start(QRunnable::create([this,path]{qint64 bytes=-1; if(!path.isEmpty()){QStorageInfo volume(path);if(volume.isValid()&&volume.isReady())bytes=volume.bytesAvailable();} std::lock_guard<std::mutex> lock(mutex_); resultBytes_=bytes;resultPath_=path;resultReady_=true;}));
 }
 storageText_=freeBytes_<0?"Storage · destination unavailable":QString("Storage · %1 GiB available").arg(double(freeBytes_)/(1024*1024*1024),0,'f',1);
 const bool low=freeBytes_>=0 && double(freeBytes_)<=std::clamp(settings_.value("gib",20).toDouble(),.1,10000.0)*1024*1024*1024,full=freeBytes_==0;
 if(settings_.value("storage",true).toBool()){if(full && !storageFull_){alerts_.remove("Storage running low");notify("Recording disk full","No recording space remains.",true);}else if(low && !storageAlarm_)notify("Storage running low",storageText_,false);}
 storageAlarm_=low;storageFull_=full;
 auto *output=recordingActive_?obs_frontend_get_recording_output():
  (streamingActive_?obs_frontend_get_streaming_output():(replayOutput_?obs_output_get_ref(replayOutput_):nullptr));
 recordingBitrate_=recordingSampler_.sample(recordingActive_?reinterpret_cast<quintptr>(output):0,recordingActive_,recordingActive_ && output?obs_output_get_total_bytes(output):0,historyClock_.elapsed());
 const bool paused=recordingActive_ && output && obs_output_paused(output);
 recordingSpace_.update(paused || recordingPaused_ || storagePath_.isEmpty()?std::nullopt:recordingBitrate_);
 recordingPaused_=paused;
 auto *video=output?obs_get_video():nullptr;int total=output && video?int(video_output_get_total_frames(video)):0;
 int skipped=output && video?int(video_output_get_skipped_frames(video)):0;
 double percent=0;encoderSampleValid_=output && output==lastOutput_ && total>lastTotal_ && skipped>=lastSkipped_;if(encoderSampleValid_)percent=100.0*(skipped-lastSkipped_)/(total-lastTotal_);
 encoderPercent_=percent;
 const bool issue=percent>=settings_.value("skip",2).toInt();
 encoderText_=output?QString("Encoder · %1% skipped in last sample").arg(percent,0,'f',1):"Encoder · idle";
 if(issue&&!encoderAlarm_&&settings_.value("encoder",true).toBool())notify("Encoder overload",encoderText_,true);
 encoderAlarm_=issue; lastTotal_=total;lastSkipped_=skipped;
 if(lastOutput_)obs_output_release(lastOutput_);lastOutput_=output;
 auto *stream=streamingActive_?obs_frontend_get_streaming_output():nullptr;
 streamingBitrate_=streamingSampler_.sample(reinterpret_cast<quintptr>(stream),streamingActive_,stream?obs_output_get_total_bytes(stream):0,historyClock_.elapsed());
 const int totalFrames=stream?obs_output_get_total_frames(stream):0;
 const int dropped=stream?obs_output_get_frames_dropped(stream):0;
 const bool reconnecting=stream && obs_output_reconnecting(stream);
 sampleNetwork(totalFrames,dropped,reconnecting,stream && stream==lastStream_,stream!=nullptr);
 if(lastStream_)obs_output_release(lastStream_);lastStream_=stream;
 graphs_->append({historyClock_.elapsed(),{freeBytes_>=0?double(freeBytes_)/(1024*1024*1024):NAN,
  encoderSampleValid_?encoderPercent_:NAN,networkSampleValid_?networkPercent_:NAN,
  cpuUsage_.value_or(NAN),residentBytes_?double(residentBytes_)/1048576:NAN,
  diskMinutesRemaining().value_or(NAN)}});
 for(auto *view:graphViews_)if(view->isVisible())view->update();
 updateStats();updateBitrateCard();refreshPanel();
}
void LookoutDock::sampleNetwork(int totalFrames,int dropped,bool reconnecting,bool sameOutput,bool active){
 double loss=0;networkSampleValid_=active && sameOutput && totalFrames>networkTotal_ && dropped>=networkDropped_;
 if(networkSampleValid_){
  const int sent=totalFrames-networkTotal_, lost=dropped-networkDropped_;
  // OBS total frames includes attempted output frames (including drops).
  if(sent>0)loss=100.0*lost/sent;
 }
 networkPercent_=loss;
 const bool bad=loss>=settings_.value("networkPercent",2).toInt();
 networkText_=!active?"Network · idle":reconnecting?"Network · reconnecting":QString("Network · %1% dropped frames").arg(loss,0,'f',1);
 if(settings_.value("network",true).toBool()){
  if(reconnecting && !reconnectAlarm_)notify("Stream disconnected","OBS is attempting to reconnect to the streaming server.",true);
  else if(bad && !networkAlarm_)notify("Stream quality degraded",QString("%1% of frames dropped in the last sample. Check the connection or lower the streaming bitrate.").arg(loss,0,'f',1),true);
  else if(!reconnecting && reconnectAlarm_ && active)notify("Stream reconnected","Connection to the streaming server restored.");
 }
 networkAlarm_=bad;reconnectAlarm_=reconnecting;networkTotal_=totalFrames;networkDropped_=dropped;
}
void LookoutDock::openSettings(){
 if(quick_){quick_->openSettings();return;}
#ifdef LOOKOUT_WIDGET_QA
 if(dialog_){dialog_->show();dialog_->raise();return;}
 auto *d=new QDialog(this);dialog_=d;d->setAttribute(Qt::WA_DeleteOnClose);d->setObjectName("lookout");d->setWindowTitle("lookout settings");
 auto *root=new QVBoxLayout(d);auto *tabs=new QTabWidget;root->addWidget(tabs);
 auto tab=[&](const QString &title){auto *area=new QScrollArea;area->setWidgetResizable(true);area->setFrameShape(QFrame::NoFrame);auto *page=new QWidget;page->setObjectName("lookout");area->setWidget(page);tabs->addTab(area,title);auto *box=new QVBoxLayout(page);box->setSpacing(8);return box;};
 auto *appearanceLayout=tab("Appearance");auto *alertsLayout=tab("Alerts");auto *hotkeysLayout=tab("Hotkeys");auto *aboutLayout=tab("About");
 auto *appearanceTabs=new QTabWidget;appearanceTabs->setObjectName("appearanceSections");appearanceLayout->addWidget(appearanceTabs);
 auto section=[&](const QString &name){auto *page=new QWidget;appearanceTabs->addTab(page,name);return new QVBoxLayout(page);};
 auto *panelLayout=section("Layout");auto *styleLayout=section("Style");auto *graphsLayout=section("History");
 tabs->setTabIcon(0,uiIcon("thumbnail"));tabs->setTabIcon(1,uiIcon("alerts"));tabs->setTabIcon(2,uiIcon("hotkeys"));
 auto *columns=new QHBoxLayout;panelLayout->addLayout(columns);
 auto *displayGroup=new QGroupBox("Display"),*positionGroup=new QGroupBox("Position and size");columns->addWidget(displayGroup,1);columns->addWidget(positionGroup,1);columns->setAlignment(Qt::AlignTop);
 auto *displayLayout=new QVBoxLayout(displayGroup),*positionLayout=new QVBoxLayout(positionGroup);
 auto *profileForm=new QFormLayout;displayLayout->addLayout(profileForm);
 auto *appearance=new QComboBox;appearance->setObjectName("graphAppearance");appearance->addItems({"OBS theme","Gradient lines","Layered areas","Neon traces","Embossed violet","Glass ribbon","Terminal","Studio slate","lookout"});appearance->setCurrentIndex(std::clamp(settings_.value("graphAppearance",1).toInt(),0,8));profileForm->addRow("Panel design",appearance);
 auto *logoTheme=new QComboBox;logoTheme->setObjectName("logoTheme");logoTheme->addItems({"System","Light","Dark"});logoTheme->setCurrentIndex(std::clamp(settings_.value("logoTheme",0).toInt(),0,2));logoTheme->setEnabled(appearance->currentIndex()!=0);logoTheme->setToolTip("Controls the logo and the lookout panel design.");profileForm->addRow("Appearance",logoTheme);connect(logoTheme,&QComboBox::currentIndexChanged,this,[this](int mode){settings_.setValue("logoTheme",mode);settings_.sync();updateBranding(true);refreshPanel();});

 connect(appearance,&QComboBox::currentIndexChanged,this,[this,logoTheme](int value){settings_.setValue("graphAppearance",value);logoTheme->setEnabled(value!=0);settings_.sync();updateBranding();refreshPanel();});
 auto *layout=displayLayout;
 auto option=[&](const char *key,const QString &label,bool initial){
  auto *check=new QCheckBox(label);check->setObjectName(key);check->setChecked(QString(key)=="showThumbnail"?thumbnailEnabled():settings_.value(key,initial).toBool());layout->addWidget(check);
  connect(check,&QCheckBox::toggled,this,[=](bool value){settings_.setValue(key,value);settings_.sync();
   if(QString(key)=="independentCards")syncGeometryControls();
   if(QString(key)=="untilDismiss" && value)dismiss_.stop();
   if(QString(key)=="untilDismiss" && !value && (alertActive_ || sessionNotice_))dismiss_.start(settings_.value("seconds",6).toInt()*1000);
   refreshPanel();});
 };
 auto *behaviorForm=new QFormLayout;layout->addLayout(behaviorForm);auto *behavior=new QComboBox;behavior->setObjectName("panelMode");behavior->addItems({"Alerts only","Always visible","Disabled (including alerts)"});behavior->setCurrentIndex(panelMode());behaviorForm->addRow("Panel visibility",behavior);
 connect(behavior,&QComboBox::currentIndexChanged,this,[this](int value){settings_.setValue("panelMode",value);settings_.sync();refreshPanel();});
 option("showThumbnail","Show thumbnail",false);
 auto *presentation=new QComboBox;
 presentation->setObjectName("presentation");
 for(const auto &mode:displayModes_)presentation->addItem(mode.label,mode.id);
 presentation->setCurrentIndex(presentation->findData(displayMode()));
 auto *modeForm=new QFormLayout;
 modeForm->addRow("Display mode",presentation);layout->addLayout(modeForm);
 connect(presentation,&QComboBox::currentIndexChanged,this,[this,presentation](int){
  setDisplayMode(presentation->currentData().toInt());
 });
 auto *metricControls=new QHBoxLayout;layout->addLayout(metricControls);
 auto *iconOptions=new QGroupBox("Stats");iconOptions->setObjectName("metrics");iconOptions->setToolTip("Controls which stats and icons are displayed. Configure notifications under Alerts.");metricControls->addWidget(iconOptions,1,Qt::AlignTop);auto *iconGrid=new QGridLayout(iconOptions);
 const Stat iconStats[]={RecordingStat,StorageStat,StreamingStat,EncoderStat,CameraStat,NetworkStat,ReplayStat,CpuStat,MemoryStat,DiskTimeStat};
 for(int i=0;i<int(std::size(iconStats));++i){const auto &stat=statDefinitions_[iconStats[i]];const QString id=stat.id;auto *box=new QCheckBox(stat.label.compact);box->setAccessibleName(stat.label.full);box->setObjectName("metric_"+id);box->setProperty("iconRole",stat.icon);box->setChecked(settings_.value("iconVisible/"+id,true).toBool());iconGrid->addWidget(box,i/2,i%2);connect(box,&QCheckBox::toggled,this,[this,id](bool value){settings_.setValue("iconVisible/"+id,value);if(value)metricsVisible_=true;settings_.sync();refreshPanel();});}
 auto *bitrateToggle=new QCheckBox(LookoutStats::bitrate.full);bitrateToggle->setObjectName("metric_bitrate");bitrateToggle->setProperty("iconRole","graphs");bitrateToggle->setChecked(settings_.value("showBitrate",true).toBool());iconGrid->addWidget(bitrateToggle,std::size(iconStats)/2,std::size(iconStats)%2);connect(bitrateToggle,&QCheckBox::toggled,this,[this](bool value){settings_.setValue("showBitrate",value);settings_.sync();refreshPanel();});
 auto *graphOptions=new QGroupBox("Graphs");metricControls->addWidget(graphOptions,1,Qt::AlignTop);
 auto *individualGraphForm=new QFormLayout(graphOptions);
 auto addGraph=[this,individualGraphForm](const QString &id,const QString &label){
  auto *control=new QComboBox;
  control->setObjectName("graph_"+id);
  control->addItems({"Off","Mini","Full"});
  control->setCurrentIndex(graphMode(id));individualGraphForm->addRow(label,control);
  connect(control,&QComboBox::currentIndexChanged,this,[this,id](int style){
   settings_.setValue("graph/"+id,style);settings_.sync();
   if(style!=GraphOff)metricsVisible_=true;
   refreshPanel();
  });
 };
 for(const auto &metric:HistoryGraphs::metrics)addGraph(metric.id,metric.label);
 addGraph("bitrate",LookoutStats::bitrate.full);addGraph("replay",LookoutStats::replay.full);
 option("showInCaptures","Show in recordings / stream",false);
 option("showAlertIcon","Show alert icon over thumbnail",true);
 layout->addStretch();layout=styleLayout;
 auto *backgroundForm=new QFormLayout;layout->addLayout(backgroundForm);auto *opacity=new QSpinBox;opacity->setObjectName("backgroundOpacity");opacity->setRange(0,100);opacity->setSuffix(" %");opacity->setValue(settings_.value("backgroundOpacity",100).toInt());backgroundForm->addRow("Card opacity",opacity);
 connect(opacity,&QSpinBox::valueChanged,this,[this](int value){settings_.setValue("backgroundOpacity",value);settings_.sync();applyPanelAppearance();});
 auto *blur=new QSpinBox;
 blur->setObjectName("blurAmount");blur->setRange(0,100);blur->setSuffix(" %");
 blur->setValue(settings_.value("blurAmount",0).toInt());backgroundForm->addRow("Blur amount",blur);
 connect(blur,&QSpinBox::valueChanged,this,[this](int value){settings_.setValue("blurAmount",value);settings_.sync();refreshPanel();});
 auto *blurStatus=new QLabel;blurStatus->setObjectName("blurStatus");blurStatus->setWordWrap(true);blurStatus->hide();layout->addWidget(blurStatus);

 option("glowEnabled","Pulsing alert glow",false);
 auto *glowColors=new QFormLayout;layout->addLayout(glowColors);
 auto colorControl=[&](const char *key,const QString &label,const QString &initial,double risk,const QString &previewName){
  auto *row=new QHBoxLayout;
  auto *button=new QPushButton(settings_.value(key,initial).toString());button->setObjectName(key);row->addWidget(button,1);
  auto *preview=new QPushButton("Preview");preview->setObjectName(previewName);row->addWidget(preview);glowColors->addRow(label,row);
  connect(preview,&QPushButton::clicked,this,[this,risk]{previewGlow(risk);});
  connect(button,&QPushButton::clicked,this,[=]{const QColor selected=QColorDialog::getColor(QColor(settings_.value(key,initial).toString()),d,label);if(!selected.isValid())return;settings_.setValue(key,selected.name());settings_.sync();button->setText(selected.name());updateStats();updateGlow();});
 };
 colorControl("glowWarningColor","Storage warning","#ffd43b",0,"previewWarningGlow");colorControl("glowCriticalColor","Critical alert","#ff3b4d",1,"previewCriticalGlow");
 auto *glowTuning=new QFormLayout;layout->addLayout(glowTuning);
 auto tuning=[&](const char *key,const QString &label,int initial,int minimum,int maximum,const QString &suffix){
  auto *spin=new QSpinBox;spin->setObjectName(key);spin->setRange(minimum,maximum);spin->setSuffix(suffix);spin->setValue(settings_.value(key,initial).toInt());glowTuning->addRow(label,spin);
  connect(spin,&QSpinBox::valueChanged,this,[=](int value){settings_.setValue(key,value);settings_.sync();updateGlow();});
 };
 tuning("glowIntensity","Glow intensity",100,0,200," %");tuning("glowSpread","Glow spread",24,4,64," px");
 auto *speed=new QDoubleSpinBox;speed->setObjectName("glowSpeed");speed->setRange(0.25,2);speed->setSingleStep(0.1);speed->setDecimals(2);speed->setSuffix(" x");speed->setValue(settings_.value("glowSpeed",1).toDouble());glowTuning->addRow("Pulse speed",speed);
 connect(speed,&QDoubleSpinBox::valueChanged,this,[this](double value){settings_.setValue("glowSpeed",value);settings_.sync();updateGlow();});
 layout->addStretch();layout=positionLayout;
 auto *scaleRow=new QHBoxLayout;layout->addLayout(scaleRow);const QStringList scales={"Small","Normal","Large","Extra large"};for(int preset=0;preset<4;++preset){auto *button=new QPushButton(scales[preset]);button->setObjectName(QString("sizePreset%1").arg(preset));button->setCheckable(true);button->setChecked(settings_.value("panelScale",1).toInt()==preset);scaleRow->addWidget(button,1);connect(button,&QPushButton::clicked,this,[this,preset]{applyScalePreset(preset);});}
 auto *customRow=new QHBoxLayout;layout->addLayout(customRow);auto *customButton=new QPushButton("Custom");customButton->setObjectName("sizePreset4");customButton->setCheckable(true);customButton->setChecked(settings_.value("panelScale",1).toInt()==4);customRow->addWidget(customButton);auto *customSize=new QSpinBox;customSize->setObjectName("customSizePercent");customSize->setRange(50,300);customSize->setSuffix(" %");customSize->setKeyboardTracking(false);customSize->setValue(settings_.value("customSizePercent",100).toInt());customSize->setToolTip("Manual panel scale, saved as your single Custom option.");customRow->addWidget(customSize,1);connect(customButton,&QPushButton::clicked,this,&LookoutDock::applyCustomSize);
 connect(customSize,&QSpinBox::valueChanged,this,[this](int percent){const double scale=percent/100.0;settings_.setValue("customSizePercent",percent);settings_.setValue("customScale",scale);settings_.setValue("customPanelWidth",int(std::round(320*scale))+16);settings_.setValue("customThumbnailWidth",std::max(80,int(std::round(320*scale))-16));applyCustomSize();});
 auto *pixelDialog=new QDialog(d);pixelDialog->setObjectName("pixelControls");pixelDialog->setWindowTitle("Pixel controls");auto *pixelRoot=new QVBoxLayout(pixelDialog);auto *pixelForm=new QFormLayout;pixelRoot->addLayout(pixelForm);auto *pixelClose=new QDialogButtonBox(QDialogButtonBox::Close);pixelRoot->addWidget(pixelClose);connect(pixelClose,&QDialogButtonBox::rejected,pixelDialog,&QDialog::hide);auto *pixelButton=new QPushButton("Pixel controls...");pixelButton->setObjectName("pixelControlsButton");layout->addWidget(pixelButton);connect(pixelButton,&QPushButton::clicked,this,[pixelDialog]{pixelDialog->adjustSize();pixelDialog->show();pixelDialog->raise();});
 // Placement changes are saved immediately and also update visible overlays.
 auto placement=[&](const QString &title,const QString &prefix,int defaultCorner){
  layout->addWidget(new QLabel(title));
  auto *form=new QFormLayout; layout->addLayout(form);
  auto *monitor=new QComboBox; monitor->setObjectName(prefix+"Monitor");
  for(auto *screen:QGuiApplication::screens())monitor->addItem(screen->name());
  monitor->setCurrentIndex(std::clamp(settings_.value(prefix+"monitor",0).toInt(),0,std::max(0,monitor->count()-1)));
  form->addRow("Monitor",monitor);
  auto *corner=new QComboBox; corner->setObjectName(prefix+"Corner");
  corner->addItems({"Top left","Top right","Bottom left","Bottom right"});
  corner->setCurrentIndex(std::clamp(settings_.value(prefix+"corner",defaultCorner).toInt(),0,3));
  form->addRow("Corner",corner);
  auto reposition=[this]{settings_.setValue("customPosition",false);refreshPanel();};
  connect(monitor,&QComboBox::currentIndexChanged,this,[=](int value){settings_.setValue(prefix+"monitor",value);settings_.sync();reposition();});
  connect(corner,&QComboBox::currentIndexChanged,this,[=](int value){settings_.setValue(prefix+"corner",value);settings_.sync();reposition();});
  auto number=[&](const QString &key,const QString &label,int initial,int minimum,int maximum){
   auto *spin=new QSpinBox;spin->setObjectName(prefix+key);spin->setRange(minimum,maximum);spin->setSuffix(" px");
   spin->setValue(settings_.value(prefix+key,initial).toInt());pixelForm->addRow(label,spin);
   connect(spin,&QSpinBox::valueChanged,this,[=](int value){settings_.setValue(prefix+key,value);if(key=="panelWidth")rememberCustomSize();settings_.sync();syncGeometryControls();reposition();});
  };
  number("offsetX","Horizontal inset",24,0,10000);
  number("offsetY","Vertical inset",24,0,10000);
  number("panelWidth","Panel width",520,128,960);
 };
 placement("Panel position","",1);
 auto *thumbnailForm=new QFormLayout;layout->addLayout(thumbnailForm);
 auto *thumbnailPosition=new QComboBox;thumbnailPosition->setObjectName("thumbnailPosition");thumbnailPosition->addItems({"Above information","Below information","Left of information","Right of information"});
 thumbnailPosition->setToolTip("Stacks vertically when the chosen width cannot fit both cards.");thumbnailPosition->setCurrentIndex(std::clamp(settings_.value("thumbnailPosition",0).toInt(),0,3));thumbnailForm->addRow("Thumbnail position",thumbnailPosition);
 connect(thumbnailPosition,&QComboBox::currentIndexChanged,this,[=](int value){settings_.setValue("thumbnailPosition",value);settings_.sync();refreshPanel();});
 auto *thumbnailWidth=new QSpinBox;thumbnailWidth->setObjectName("thumbnailWidth");thumbnailWidth->setRange(80,800);thumbnailWidth->setSuffix(" px");thumbnailWidth->setValue(settings_.value("thumbnailWidth",320).toInt());pixelForm->addRow("Thumbnail width",thumbnailWidth);
 thumbnailWidth->setToolTip("Fits within the panel width. Side-by-side layouts reserve room for information.");
 connect(thumbnailWidth,&QSpinBox::valueChanged,this,[=](int value){settings_.setValue("thumbnailWidth",value);rememberCustomSize();settings_.sync();syncGeometryControls();refreshPanel();});
 option("independentCards","Move cards individually",false);option("snapCards","Snap cards to each other",true);option("snapScreen","Snap to screen boundaries",true);
 for(const auto &entry:{qMakePair(QString("cardSpacing"),QString("Card spacing")),qMakePair(QString("snapDistance"),QString("Snap distance"))}){auto *value=new QSpinBox;value->setObjectName(entry.first);value->setRange(0,64);value->setSuffix(" px");value->setValue(settings_.value(entry.first,entry.first=="cardSpacing"?8:12).toInt());pixelForm->addRow(entry.second,value);connect(value,&QSpinBox::valueChanged,this,[this,key=entry.first](int n){settings_.setValue(key,n);settings_.sync();refreshPanel();});}
 auto *defaults=new QPushButton("Default");defaults->setObjectName("defaultPosition");layout->addWidget(defaults);connect(defaults,&QPushButton::clicked,this,&LookoutDock::defaultPosition);
 auto *presetHelp=new QLabel("Drag cards while settings are open. Use a size preset or Custom, and Pixel controls for exact dimensions, then Save position.");presetHelp->setWordWrap(true);layout->addWidget(presetHelp);
 auto *presets=new QComboBox;presets->setObjectName("positionPresets");layout->addWidget(presets);
 auto refill=[this,presets]{presets->clear();for(const auto &item:settings_.value("positionPresets").toList())presets->addItem(item.toMap().value("name").toString());};refill();
 auto *presetButtons=new QHBoxLayout;layout->addLayout(presetButtons);auto *save=new QPushButton("Save position..."),*load=new QPushButton("Load"),*remove=new QPushButton("Remove");presetButtons->addWidget(save);presetButtons->addWidget(load);presetButtons->addWidget(remove);
 connect(save,&QPushButton::clicked,this,[this,d,refill]{bool ok=false;QString name=QInputDialog::getText(d,"Save position","Preset name",QLineEdit::Normal,{},&ok).trimmed();if(ok && !name.isEmpty()){savePreset(name.left(64));refill();}});
 connect(load,&QPushButton::clicked,this,[this,presets]{loadPreset(presets->currentText());});
 connect(remove,&QPushButton::clicked,this,[this,presets,refill]{removePreset(presets->currentText());refill();});
 layout->addStretch();
 auto *form=new QFormLayout;alertsLayout->addLayout(form);
 auto check=[&](const char *key,const QString &label){auto *c=new QCheckBox(label);c->setObjectName(key);c->setChecked(settings_.value(key,true).toBool());form->addRow(c);
  connect(c,&QCheckBox::toggled,this,[=](bool value){settings_.setValue(key,value);settings_.sync();storageAlarm_=false;storageFull_=false;storageUnavailable_=false;encoderAlarm_=false;networkAlarm_=false;reconnectAlarm_=false;refreshPanel();});return c;};
 check("storage","Low or unavailable storage");check("encoder","Encoding lag");check("network","Stream network issues");check("lifecycle","Output start/stop notifications");
 auto spin=[&](const char *key,const QString &label,int initial,int min,int max){auto *control=new QSpinBox;control->setObjectName(key);control->setRange(min,max);control->setValue(settings_.value(key,initial).toInt());form->addRow(label,control);
  connect(control,&QSpinBox::valueChanged,this,[=](int value){settings_.setValue(key,value);settings_.sync();});};
 auto *storageWarning=new QDoubleSpinBox;storageWarning->setObjectName("gib");storageWarning->setRange(.1,10000);storageWarning->setDecimals(1);storageWarning->setSuffix(" GiB");storageWarning->setKeyboardTracking(false);storageWarning->setValue(std::clamp(settings_.value("gib",20).toDouble(),.1,10000.0));form->addRow("Storage approaching warning",storageWarning);connect(storageWarning,&QDoubleSpinBox::valueChanged,this,[this](double value){settings_.setValue("gib",std::max(.1,value));settings_.sync();storageAlarm_=false;storageFull_=false;refreshPanel();});spin("skip","Skipped frames (%)",2,1,100);spin("networkPercent","Dropped frames (%)",2,1,100);spin("seconds","Notification duration (seconds)",6,2,60);
 auto *approachHelp=new QLabel("Storage uses the warning color below this value and the critical color at zero. Encoder and network alerts activate only at their configured limits.");approachHelp->setObjectName("approachDefinition");approachHelp->setWordWrap(true);alertsLayout->addWidget(approachHelp);
 layout=alertsLayout;option("untilDismiss","Keep notifications until dismissed",false);auto *dismissCurrent=new QPushButton("Dismiss current alerts");dismissCurrent->setObjectName("dismissCurrentAlerts");alertsLayout->addWidget(dismissCurrent);connect(dismissCurrent,&QPushButton::clicked,this,&LookoutDock::dismissAlert);
 auto *alertPreviews=new QGroupBox("Preview alerts");alertsLayout->addWidget(alertPreviews);auto *previewButtons=new QGridLayout(alertPreviews);
 const QStringList previewLabels={"Low storage","Storage unavailable","Encoder overload","Dropped frames","Disconnected stream","Storage full","Stream reconnected","Stream stopped with an error"};
 for(int i=0;i<previewLabels.size();++i){auto *button=new QPushButton(previewLabels[i]);button->setObjectName(QString("previewAlert%1").arg(i));previewButtons->addWidget(button,i/2,i%2);connect(button,&QPushButton::clicked,this,[this,i]{previewAlert(i);});}
 auto *stopPreview=new QPushButton("Stop preview");stopPreview->setObjectName("stopAlertPreview");previewButtons->addWidget(stopPreview,(previewLabels.size()+1)/2,0,1,2);connect(stopPreview,&QPushButton::clicked,this,[this]{glowPreviewUntil_=0;glowPreviewMetric_=-1;previewAlertTitle_.clear();previewAlertBody_.clear();applyMetricColors();refreshPanel();});alertsLayout->addStretch();
 auto *graphForm=new QFormLayout;graphsLayout->addLayout(graphForm);
 auto *historyRange=new QComboBox;historyRange->setObjectName("graphWindow");for(const auto &entry:{qMakePair(QString("30 seconds"),30),qMakePair(QString("1 minute"),60),qMakePair(QString("5 minutes"),300),qMakePair(QString("15 minutes"),900)})historyRange->addItem(entry.first,entry.second);historyRange->setCurrentIndex(std::max(0,historyRange->findData(settings_.value("graphWindow",60).toInt())));graphForm->addRow("History shown",historyRange);
 connect(historyRange,&QComboBox::currentIndexChanged,this,[this,historyRange](int index){settings_.setValue("graphWindow",historyRange->itemData(index));settings_.sync();configureGraphs();refreshPanel();});
 auto *clearHistory=new QPushButton("Clear history");clearHistory->setObjectName("clearHistory");graphsLayout->addWidget(clearHistory);connect(clearHistory,&QPushButton::clicked,this,[this]{graphs_->clearHistory();bitrates_->clearHistory();replay_->clearHistory();for(auto *view:graphViews_)view->update();});graphsLayout->addStretch();
 auto *keyHelp=new QLabel("Click a field, then press the shortcut. OBS handles the global hotkeys; changes apply immediately.");keyHelp->setWordWrap(true);hotkeysLayout->addWidget(keyHelp);
 auto *keyForm=new QFormLayout;hotkeysLayout->addLayout(keyForm);
 const QStringList actions={"Show Panel (persistent)","Hide Panel (alerts override)","Disable Panel (also disables alerts)","Toggle Thumbnail","Toggle Stats","Toggle Show in Recordings/Stream","Cycle Position (Defaults)","Cycle Position (Presets)"};
 for(int i=0;i<HotkeyCount;++i){auto *row=new QHBoxLayout;
  auto *edit=new HotkeyEditor([this,i](obs_key_combination_t combination){setHotkeyBinding(i,combination);});edit->setObjectName(QString("hotkeyEditor%1").arg(i));row->addWidget(edit,1);
  auto *clear=new QPushButton("Clear");clear->setObjectName(QString("clearHotkey%1").arg(i));row->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this,i]{setHotkeyBinding(i,{0,OBS_KEY_NONE});});
  keyForm->addRow(actions[i],row);
 }
 refreshHotkeyEditors();hotkeysLayout->addStretch();
 auto *identity=new QHBoxLayout;aboutLayout->addLayout(identity);auto *logo=new QLabel;logo->setObjectName("lookoutBadge");logo->setFixedSize(80,80);identity->addWidget(logo);auto *brandName=new QLabel("lookout");QFont brandFont("Georgia");brandFont.setPointSize(26);brandName->setFont(brandFont);identity->addWidget(brandName);identity->addStretch();
 auto *about=new QFormLayout;aboutLayout->addLayout(about);about->addRow("Version",new QLabel(QStringLiteral(LOOKOUT_VERSION)));about->addRow("Build",new QLabel(QStringLiteral(LOOKOUT_BUILD)));
 auto *releases=new QLabel("<a href=\"https://github.com/technicallyalex/lookout/releases\">GitHub Releases</a>");
 releases->setObjectName("githubReleases");releases->setOpenExternalLinks(true);
 about->addRow("Release Candidate 2",releases);aboutLayout->addStretch();
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,d,&QDialog::close);
 d->installEventFilter(this);
 // Size to the largest tab's actual content; retain scrolling only as a fallback
 // for screens physically smaller than the settings contents.
 QSize pages;
 for(int i=0;i<tabs->count();++i){auto *area=qobject_cast<QScrollArea*>(tabs->widget(i));area->widget()->layout()->activate();pages=pages.expandedTo(area->widget()->sizeHint());}
 auto *screen=QGuiApplication::screenAt(mapToGlobal(rect().center()));if(!screen)screen=QGuiApplication::primaryScreen();
 const QSize available=screen?screen->availableGeometry().size()-QSize(48,80):QSize(1000,800);
 d->resize(std::min(std::max(880,pages.width()+48),available.width()),std::min(pages.height()+105,available.height()));
 updateBranding(true);iconsInitialized_=false;updateIcons();syncVisibilityControls();d->show();setEditMode(true);
#endif
}

void LookoutDock::setEditMode(bool enabled){
 if(editMode_==enabled)return;
 editHidden_=false;editMode_=enabled;resizing_=false;badge_->setProperty("editTransforms",enabled);badge_->setCursor(enabled?Qt::SizeAllCursor:Qt::ArrowCursor);dragging_=false;transformTimer_.stop();
 if(!enabled){glowPreviewUntil_=0;glowPreviewMetric_=-1;previewAlertTitle_.clear();previewAlertBody_.clear();applyMetricColors();}
 placing_=true;badge_->hide();badge_->setWindowFlag(Qt::WindowTransparentForInput,!enabled);placing_=false;
 refreshPanel();
}
void LookoutDock::saveTransform(){
 if(!editMode_)return;
 if(independentLayout_){settings_.sync();refreshPanel();return;}
 if(auto *screen=QGuiApplication::screenAt(badge_->frameGeometry().center()))settings_.setValue("monitor",QGuiApplication::screens().indexOf(screen));
 settings_.setValue("customPosition",true);const int halo=overlayPadding();settings_.setValue("positionX",badge_->x()+halo);settings_.setValue("positionY",badge_->y()+halo);settings_.sync();syncGeometryControls();refreshPanel();
}
void LookoutDock::defaultPosition(){transformTimer_.stop();dragging_=false;settings_.setValue("customPosition",false);settings_.remove("cardPositions");settings_.sync();refreshPanel();}
void LookoutDock::savePreset(const QString &name){
 if(transformTimer_.isActive()){transformTimer_.stop();saveTransform();}
 auto list=settings_.value("positionPresets").toList();
 QVariantMap item;item["name"]=name;
 const QVariantMap defaults={{"monitor",0},{"corner",1},{"offsetX",24},{"offsetY",24},{"panelWidth",520},{"thumbnailWidth",320},{"thumbnailPosition",0},{"customPosition",false},{"positionX",0},{"positionY",0},{"cardPositions",QVariantMap()},{"cardWidths",QVariantMap()},{"independentCards",false},{"panelScale",1},{"customScale",1.0},{"customSizePercent",100},{"customPanelWidth",336},{"customThumbnailWidth",304},{"cardSpacing",8},{"snapDistance",12},{"snapCards",true},{"snapScreen",true}};
 for(auto it=defaults.cbegin();it!=defaults.cend();++it)item[it.key()]=settings_.value(it.key(),it.value());
 for(int i=list.size()-1;i>=0;--i)if(list[i].toMap().value("name").toString()==name)list.removeAt(i);
 if(list.size()>=20)list.removeFirst();list.append(item);settings_.setValue("positionPresets",list);settings_.sync();dockButtons_["cyclePositionPresets"]->setEnabled(true);
}
void LookoutDock::removePreset(const QString &name){
 auto list=settings_.value("positionPresets").toList();
 list.erase(std::remove_if(list.begin(),list.end(),[&name](const QVariant &item){
  return item.toMap().value("name").toString()==name;
 }),list.end());
 settings_.setValue("positionPresets",list);settings_.sync();dockButtons_["cyclePositionPresets"]->setEnabled(!list.isEmpty());
}
bool LookoutDock::loadPreset(const QString &name){
 transformTimer_.stop();dragging_=false;
 for(const auto &entry:settings_.value("positionPresets").toList()){auto item=entry.toMap();if(item.value("name").toString()!=name)continue;
  if(!item.contains("cardWidths"))settings_.remove("cardWidths");
  for(auto it=item.cbegin();it!=item.cend();++it)if(it.key()!="name")settings_.setValue(it.key(),it.value());
  settings_.setValue("lastPositionPreset",name);settings_.sync();syncGeometryControls();refreshPanel();return true;
 }return false;
}
QMap<QString,QRectF> LookoutDock::visibleCardRects() const {
 QMap<QString,QRectF> result;for(const auto &entry:{qMakePair(QString("thumbnail"),previewCard_),qMakePair(QString("alert"),static_cast<QWidget*>(badgeText_)),qMakePair(QString("stats"),stats_),qMakePair(QString("bitrate"),static_cast<QWidget*>(bitrates_)),qMakePair(QString("replay"),static_cast<QWidget*>(replay_)),qMakePair(QString("active-icons"),static_cast<QWidget*>(activeIcons_))})if(entry.second->isVisibleTo(badge_))result[entry.first]=QRectF(entry.second->mapToGlobal(QPoint()),entry.second->size());
 for(int metric=0;metric<HistoryGraphs::Count;++metric){auto *view=independentLayout_?graphViews_[metric]:graphs_;if(view->isVisibleTo(badge_)){const auto rect=view->metricRect(metric);if(!rect.isEmpty())result[HistoryGraphs::metrics[metric].card]=rect.translated(view->mapToGlobal(QPoint()));}}
 return result;
}
void LookoutDock::enableQuickRenderer(const QString &executable){
 if(quick_)return;badge_->hide();badge_->setProperty("quickModel",true);quick_=new QuickPanelBridge(this,executable);updateBranding();refreshPanel();
}
void LookoutDock::presentPanel(){
 if(!quick_){showProtected(badge_);return;}
 quickVisible_=ready_;quick_->publish();
}
void LookoutDock::hidePanel(){quickVisible_=false;badge_->hide();if(quick_)quick_->publish();}

double LookoutDock::panelScale() const {
 const int preset=settings_.value("panelScale",1).toInt();
 const bool custom=preset==4;
 auto *screen=QGuiApplication::screens().value(settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());const QSize available=screen?screen->availableGeometry().size():QSize(1920,1080);const double requested=custom?std::clamp(settings_.value("customScale",1).toDouble(),.45,3.0):LookoutLayout::presetScale(preset,available);
 const bool thumbnail=thumbnailDisplayed();
 if(iconStripMode() || (custom && !thumbnail))return requested;
 const double line=QFontMetricsF(font()).height();
 double units=0;int count=0,fixedThumbnail=0;
 if(thumbnail){
  if(custom)fixedThumbnail=std::min(std::clamp(settings_.value("thumbnailWidth",320).toInt(),80,800)*9/16+16,available.height()/3);
  else units+=187;
  ++count;
 }
 if(metricsVisible_){
  int rows=0;
  for(int i=0;i<StatCount;++i)rows+=hasReading(i);
  if(rows){units+=rows*(line+2)+16;++count;}
  auto addGraph=[&](const QString &id,double mini,double full){
   const auto mode=graphMode(id);
   if(mode!=GraphOff){units+=mode==GraphMini?mini:full;++count;}
  };
  addGraph("bitrate",56,150);addGraph("replay",48,140);
  const bool slate=!quick_ && settings_.value("graphAppearance",1).toInt()==7;
  for(const auto &metric:HistoryGraphs::metrics)addGraph(metric.id,48,90+std::max(18.0,line+2)*(slate?3:2));
 }
 if(alertActive_ || !previewAlertTitle_.isEmpty()){units+=100;++count;}
 const double height=available.height()-2*settings_.value("offsetY",24).toInt()-16-fixedThumbnail-std::max(0,count-1)*settings_.value("cardSpacing",8).toInt();return std::clamp(units>0?std::min(requested,height/units):requested,0.45,3.0);
}
void LookoutDock::applyScalePreset(int preset){
 auto *screen=QGuiApplication::screens().value(settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());const QSize available=screen?screen->availableGeometry().size():QSize(1920,1080);settings_.remove("cardWidths");settings_.setValue("panelScale",preset);const int width=int(std::round(320*panelScale()));settings_.setValue("panelWidth",width+16);settings_.setValue("thumbnailWidth",std::max(80,width-16));settings_.sync();syncGeometryControls();refreshPanel();
}
void LookoutDock::applyCustomSize(){
 settings_.remove("cardWidths");settings_.setValue("panelScale",4);settings_.setValue("panelWidth",settings_.value("customPanelWidth",336));settings_.setValue("thumbnailWidth",settings_.value("customThumbnailWidth",304));settings_.sync();syncGeometryControls();refreshPanel();
}
void LookoutDock::rememberCustomSize(){
 const double scale=settings_.value("panelScale",1).toInt()==4?settings_.value("customScale",1).toDouble():panelScale();settings_.setValue("customScale",scale);settings_.setValue("panelScale",4);settings_.setValue("customPanelWidth",settings_.value("panelWidth",336));settings_.setValue("customThumbnailWidth",settings_.value("thumbnailWidth",304));settings_.setValue("customSizePercent",std::clamp(int(std::round(scale*100)),45,300));
}
QMap<QString,QWidget*> LookoutDock::movableCards() const {
 QMap<QString,QWidget*> result;for(const auto &entry:{qMakePair(QString("thumbnail"),previewCard_),qMakePair(QString("alert"),static_cast<QWidget*>(badgeText_)),qMakePair(QString("stats"),stats_),qMakePair(QString("bitrate"),static_cast<QWidget*>(bitrates_)),qMakePair(QString("replay"),static_cast<QWidget*>(replay_)),qMakePair(QString("active-icons"),static_cast<QWidget*>(activeIcons_))})if(!entry.second->isHidden())result.insert(entry.first,entry.second);
 if(independentLayout_){for(int i=0;i<HistoryGraphs::Count;++i)if(!graphViews_[i]->isHidden())result.insert(HistoryGraphs::metrics[i].card,graphViews_[i]);}else if(!graphs_->isHidden())result.insert("graphs",graphs_);
 return result;
}
void LookoutDock::setIndependentLayout(bool enabled){
 if(enabled==independentLayout_)return;placing_=true;transformTimer_.stop();independentLayout_=enabled;
 // Keep ownership stable. Reparenting hidden cards can leave their old
 // rectangles in the next scene sent to the Quick renderer.
 information_->layout()->setEnabled(!enabled);
 badge_->layout()->setEnabled(!enabled);
 placing_=false;
}
void LookoutDock::placeIndependent(){
 auto *screen=QGuiApplication::screens().value(settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());if(!screen)return;const QRect available=screen->availableGeometry();const int spacing=settings_.value("cardSpacing",8).toInt(),insetX=std::clamp(settings_.value("offsetX",24).toInt(),0,std::max(0,(available.width()-160)/2)),insetY=std::clamp(settings_.value("offsetY",24).toInt(),0,std::max(0,(available.height()-80)/2));const int width=std::min(std::max(1,available.width()-2*insetX),std::clamp(settings_.value("panelWidth",LookoutLayout::presetWidth(settings_.value("panelScale",1).toInt(),available.size())+16).toInt()-16,112,1200));
 placing_=true;
 const auto widths=settings_.value("cardWidths").toMap();
 const auto cardWidth=[&](const QString &id,int fallback){
  return std::clamp(widths.value(id,fallback).toInt(),std::min(96,available.width()),available.width());
 };
 layoutStats(cardWidth("stats",width));
 const int thumbWidth=std::max(1,cardWidth("thumbnail",std::min(width,std::clamp(settings_.value("thumbnailWidth",320).toInt(),80,800)+16))-16);
 previewCard_->setFixedSize(thumbWidth+16,std::max(1,thumbWidth*9/16)+16);
 preview_->setFixedSize(thumbWidth,std::max(1,thumbWidth*9/16));
 updateAlertIcon();
 const auto widgets=movableCards();QMap<QString,QSize> sizes;int total=0;
 QStringList order{"thumbnail","alert","stats","bitrate","replay"};
 for(const auto &metric:HistoryGraphs::metrics)order.append(metric.card);
 for(const auto &id:order)if(auto *widget=widgets.value(id)){
  const int w=cardWidth(id,width);
  const int height=widget==previewCard_?previewCard_->height():widget==badgeText_?badgeText_->heightForWidth(w):widget==stats_?statsLayout_->sizeHint().height():widget==bitrates_?bitrates_->heightForWidth(w):widget==replay_?replay_->heightForWidth(w):static_cast<HistoryGraphs*>(widget)->heightForWidth(w);
  sizes[id]=QSize(widget==previewCard_?previewCard_->width():w,std::max(16,height));
  total+=sizes[id].height()+spacing;
 }
 const int corner=settings_.value("corner",1).toInt();int y=corner<2?available.top()+insetY:std::max(available.top(),available.bottom()-total+spacing-insetY+1);const auto positions=settings_.value("cardPositions").toMap();QRect bounds;cardGeometry_.clear();
 for(const auto &id:order)if(sizes.contains(id)){const int x=corner%2?available.right()-sizes[id].width()-insetX+1:available.left()+insetX;QPoint origin(x,y);if(positions.contains(id)){const QPointF saved=positions.value(id).toPointF();origin=available.topLeft()+QPoint(std::round(saved.x()*available.width()),std::round(saved.y()*available.height()));}origin.setX(std::clamp(origin.x(),available.left(),std::max(available.left(),available.right()-sizes[id].width()+1)));origin.setY(std::clamp(origin.y(),available.top(),std::max(available.top(),available.bottom()-sizes[id].height()+1)));QVector<QRect> occupied;for(const auto &r:cardGeometry_)occupied<<r;origin=LookoutLayout::snapPosition(QRect(origin,sizes[id]),occupied,available,spacing,0,settings_.value("snapCards",true).toBool(),settings_.value("snapScreen",true).toBool(),QMargins(insetX,insetY,insetX,insetY));const QRect card(origin,sizes[id]);cardGeometry_[id]=card;bounds=bounds.isNull()?card:bounds.united(card);y+=sizes[id].height()+spacing;}
 if(bounds.isNull()){placing_=false;return;}const int halo=overlayPadding();bounds.adjust(-8-halo,-8-halo,8+halo,8+halo);badge_->setMinimumSize(1,1);badge_->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);badge_->setGeometry(bounds);
 information_->setGeometry(badge_->rect());
 for(auto it=cardGeometry_.cbegin();it!=cardGeometry_.cend();++it){auto *widget=widgets.value(it.key());widget->setGeometry(QRect(it.value().topLeft()-bounds.topLeft(),it.value().size()));if(widget->layout())widget->layout()->setGeometry(widget->rect());}
 static_cast<NotificationPanel*>(badge_)->updateCards();placing_=false;
}
QVector<QRectF> LookoutDock::glowTargets() const {
 QVector<QRectF> result;const bool preview=glowPreviewUntil_>glowClock_.elapsed();bool statsAdded=false;
 if(activeIcons_->isVisibleTo(badge_) && activeIcons_->hasAlerts())return {QRectF(activeIcons_->mapTo(badge_,QPoint()),activeIcons_->size())};
 for(int metric=0;metric<3;++metric){const int stat=1+2*metric;if(preview?glowPreviewMetric_!=stat:metricRisk(stat)<0)continue;
  if(stats_->isVisibleTo(badge_) && hasReading(stat) && !statsAdded){result<<QRectF(stats_->mapTo(badge_,QPoint()),stats_->size());statsAdded=true;}
  HistoryGraphs *view=independentLayout_?graphViews_[metric]:graphs_;if(view->isVisibleTo(badge_)){const QRectF card=view->metricRect(metric);if(!card.isEmpty())result<<card.translated(view->mapTo(badge_,QPoint()));}
 }
 if(result.isEmpty() && !thumbnailAlertPreview_.isActive() && alertIcon_->isVisibleTo(badge_))result<<QRectF(previewCard_->mapTo(badge_,QPoint()),previewCard_->size());return result;
}
bool LookoutDock::eventFilter(QObject *watched,QEvent *event){
 if(event->type()==QEvent::LayoutRequest && !placing_)cardLayoutTimer_.start();

 if(watched==badge_ && event->type()==QEvent::Hide){badge_->setProperty("glowActive",false);glow_->hide();glowTimer_.stop();}
 if(watched==badge_ && (event->type()==QEvent::Move || event->type()==QEvent::Resize) && glow_->isVisible())updateGlow();
 if(dialog_ && watched==dialog_ && event->type()==QEvent::Hide)setEditMode(false);
 if(dialog_ && watched==dialog_ && event->type()==QEvent::Show)setEditMode(true);
 if(watched==badge_ && event->type()==QEvent::WinIdChange)QTimer::singleShot(0,this,[this]{if(ready_)refreshPanel();});
 // Programmatic layout resizes are never saved as user transforms.
 // Explicit card drags commit their placement on mouse release.
 if(editMode_ && (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove || event->type()==QEvent::MouseButtonRelease)){
  auto *mouse=static_cast<QMouseEvent*>(event);auto *target=qobject_cast<QWidget*>(watched);
  if(event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){const auto cards=movableCards();for(auto it=cards.cbegin();it!=cards.cend();++it)if(target && (target==it.value() || it.value()->isAncestorOf(target))){dragging_=true;transformChanged_=false;dragCard_=it.key();dragOrigin_=mouse->globalPosition().toPoint();windowOrigin_=badge_->pos();dragCardStart_=QRect(quick_?windowOrigin_+it.value()->mapTo(badge_,QPoint()):it.value()->mapToGlobal(QPoint()),it.value()->size());resizing_=dragCardStart_.right()-mouse->globalPosition().x()<13 && dragCardStart_.bottom()-mouse->globalPosition().y()<13;resizeScaleStart_=panelScale();resizePanelStart_=QSize(settings_.value("panelWidth",badge_->width()).toInt(),badge_->height());resizeThumbnailStart_=settings_.value("thumbnailWidth",320).toInt();if(resizing_)badge_->setCursor(Qt::SizeFDiagCursor);if(!quick_)target->grabMouse();return true;}}
  if(event->type()==QEvent::MouseMove && dragging_){const QPoint delta=mouse->globalPosition().toPoint()-dragOrigin_;
   if(delta.isNull() && !transformChanged_)return true;
   const bool startingTransform=!transformChanged_;
   transformChanged_=true;
   if(resizing_){
    if(independentLayout_){
     if(startingTransform){
      auto *screen=QGuiApplication::screens().value(settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());
      if(!screen)return true;
      const QRect available=screen->availableGeometry();
      const QPoint origin=dragCardStart_.topLeft()-available.topLeft();
      auto positions=settings_.value("cardPositions").toMap();
      positions[dragCard_]=QPointF(double(origin.x())/available.width(),double(origin.y())/available.height());
      settings_.setValue("cardPositions",positions);
     }
     auto widths=settings_.value("cardWidths").toMap();
     const int change=dragCard_=="thumbnail" && std::abs(delta.y()*16/9)>std::abs(delta.x())?delta.y()*16/9:delta.x();
     widths[dragCard_]=std::clamp(dragCardStart_.width()+change,96,960);
     settings_.setValue("cardWidths",widths);
     placeIndependent();
     return true;
    }
    const int span=dragCardStart_.right()-windowOrigin_.x()-overlayPadding()+1;
    const double ratio=std::clamp(1+double(delta.x())/std::max(1,span),.25,4.0);
    settings_.setValue("panelScale",4);
    settings_.setValue("customScale",std::clamp(resizeScaleStart_*ratio,.45,3.0));
    settings_.setValue("panelWidth",std::clamp(qRound(resizePanelStart_.width()*ratio),128,960));
    settings_.setValue("thumbnailWidth",std::clamp(qRound(resizeThumbnailStart_*ratio),80,800));
    settings_.setValue("customPosition",true);
    settings_.setValue("positionX",windowOrigin_.x()+overlayPadding());
    settings_.setValue("positionY",windowOrigin_.y()+overlayPadding());
    rememberCustomSize();applyPanelAppearance();configureGraphs();updateStats();place(badge_,"");syncGeometryControls();return true;
   }
   if(independentLayout_){auto *screen=QGuiApplication::screens().value(settings_.value("monitor",0).toInt(),QGuiApplication::primaryScreen());if(!screen)return true;QVector<QRect> others;for(auto it=cardGeometry_.cbegin();it!=cardGeometry_.cend();++it)if(it.key()!=dragCard_)others<<it.value();const QRect available=screen->availableGeometry();const QPoint origin=LookoutLayout::snapPosition(QRect(dragCardStart_.topLeft()+delta,dragCardStart_.size()),others,available,settings_.value("cardSpacing",8).toInt(),settings_.value("snapDistance",12).toInt(),settings_.value("snapCards",true).toBool(),settings_.value("snapScreen",true).toBool(),QMargins(settings_.value("offsetX",24).toInt(),settings_.value("offsetY",24).toInt(),settings_.value("offsetX",24).toInt(),settings_.value("offsetY",24).toInt()));auto positions=settings_.value("cardPositions").toMap();positions[dragCard_]=QPointF(double(origin.x()-available.left())/available.width(),double(origin.y()-available.top())/available.height());settings_.setValue("cardPositions",positions);placeIndependent();}
   else badge_->move(windowOrigin_+delta);updateGlow();return true;}
  if(event->type()==QEvent::MouseButtonRelease && dragging_){if(target && !quick_)target->releaseMouse();dragging_=false;resizing_=false;badge_->setCursor(Qt::SizeAllCursor);dragCard_.clear();if(transformChanged_)saveTransform();return true;}
 }
 return QWidget::eventFilter(watched,event);
}

void LookoutDock::syncGeometryControls(){
 if(!dialog_)return;
 if(auto *position=dialog_->findChild<QComboBox*>("thumbnailPosition")){QSignalBlocker blocker(position);position->setCurrentIndex(std::clamp(settings_.value("thumbnailPosition",0).toInt(),0,3));position->setEnabled(!settings_.value("independentCards",false).toBool());position->setToolTip(position->isEnabled()?"Thumbnail arrangement within the group":"Individual cards use their saved positions; drag the thumbnail to place it.");}
 for(const auto &key:{"independentCards","snapCards","snapScreen"})if(auto *check=dialog_->findChild<QCheckBox*>(key)){QSignalBlocker blocker(check);check->setChecked(settings_.value(key,QString(key)!="independentCards").toBool());}
 for(int i=0;i<5;++i)if(auto *button=dialog_->findChild<QPushButton*>(QString("sizePreset%1").arg(i)))button->setChecked(settings_.value("panelScale",1).toInt()==i);
 for(const auto &key:QStringList{"offsetX","offsetY","panelWidth","thumbnailWidth","cardSpacing","snapDistance","customSizePercent"})if(auto *spin=dialog_->findChild<QSpinBox*>(key)){QSignalBlocker blocker(spin);spin->setValue(settings_.value(key,spin->value()).toInt());}
 for(const auto &pair:{qMakePair(QString("Corner"),QString("corner")),qMakePair(QString("Monitor"),QString("monitor"))})if(auto *combo=dialog_->findChild<QComboBox*>(pair.first)){QSignalBlocker blocker(combo);combo->setCurrentIndex(settings_.value(pair.second,combo->currentIndex()).toInt());}
}

double LookoutDock::metricRisk(int index) const {
 if(index==1 && settings_.value("storage",true).toBool()){
  if(storageUnavailable_)return 1;
  if(freeBytes_<0)return -1;
  const double threshold=std::clamp(settings_.value("gib",20).toDouble(),.1,10000.0)*1024*1024*1024;
  if(freeBytes_>threshold)return -1;return std::clamp(1.0-freeBytes_/threshold,0.0,1.0);
 }
 double ratio=0;
 if(index==3 && lastOutput_ && settings_.value("encoder",true).toBool())ratio=encoderPercent_/std::max(1.0,settings_.value("skip",2).toDouble());
 else if(index==5 && settings_.value("network",true).toBool()){
  if(reconnectAlarm_)return 1;
  if(!lastStream_)return -1;
  ratio=networkPercent_/std::max(1.0,settings_.value("networkPercent",2).toDouble());
 }else return -1;
 return ratio>=1?1:-1;
}
QColor LookoutDock::warningColor(double risk) const {
 QColor yellow(settings_.value("glowWarningColor","#ffd43b").toString()),red(settings_.value("glowCriticalColor","#ff3b4d").toString());
 if(!yellow.isValid())yellow=QColor("#ffd43b");if(!red.isValid())red=QColor("#ff3b4d");risk=std::clamp(risk,0.0,1.0);
 return risk>=1?red:yellow;
}
void LookoutDock::previewGlow(double risk){
 previewAlertTitle_.clear();previewAlertBody_.clear();
 glowPreviewRisk_=std::clamp(risk,0.0,1.0);glowPreviewMetric_=risk<1?1:1+2*QRandomGenerator::global()->bounded(3);
 glowPreviewUntil_=glowClock_.elapsed()+10000;glowPhase_=3.141592654;glowLastFrame_=glowClock_.elapsed();refreshPanel();
}
void LookoutDock::previewAlert(int type){
 const QStringList titles={"Storage running low","Recording storage unavailable","Encoder overload",
   "Stream quality degraded","Stream disconnected","Recording disk full",
   "Stream reconnected","Stream stopped with an error"};
 if(type<0 || type>=titles.size())return;
 const QStringList bodies={
  QString("Storage · %1 GiB available").arg(settings_.value("gib",20).toDouble(),0,'f',1),
  "The recording destination could not be checked. Verify the drive is connected and accessible.",
  QString("Encoder · %1% skipped in last sample").arg(settings_.value("skip",2).toInt()),
  QString("%1% of frames dropped in the last sample. Check the connection or lower the streaming bitrate.")
   .arg(settings_.value("networkPercent",2).toInt()),
  "OBS is attempting to reconnect to the streaming server.",
  "No recording space remains.",
  "Connection to the streaming server restored.",
  "The streaming server closed the connection."
 };
 previewAlertTitle_=titles[type];previewAlertBody_=bodies[type];
 glowPreviewMetric_=type==6?-1:type<2 || type==5?StorageStat:type==2?EncoderStat:NetworkStat;
 glowPreviewRisk_=type==6?-1:type==0?0:1;
 glowPreviewUntil_=glowClock_.elapsed()+10000;glowPhase_=3.141592654;
 glowLastFrame_=glowClock_.elapsed();refreshPanel();
}
void LookoutDock::applyMetricColors(double pulse){
 const bool preview=glowPreviewUntil_>glowClock_.elapsed();
 if(pulse<0)pulse=0.08+0.92*(0.5-0.5*std::cos(glowPhase_));
 for(int i=0;i<StatCount;++i){
  const double risk=metricRisk(i);QString style=risk<0?QString():"color:"+warningColor(risk).name()+";";
  if(preview && i==glowPreviewMetric_){
   const QColor base=stats_->palette().color(QPalette::WindowText),color=warningColor(glowPreviewRisk_);
   const double mix=std::clamp(pulse,0.0,1.0);
   const QColor flash=QColor::fromRgbF(base.redF()*(1-mix)+color.redF()*mix,base.greenF()*(1-mix)+color.greenF()*mix,base.blueF()*(1-mix)+color.blueF()*mix);
   style="color:"+flash.name()+";";
  }
  if(i==1 || i==3 || i==5){const QString key=QString("metricColor%1").arg((i-1)/2);const QColor color=style.isEmpty()?QColor():QColor(style.mid(6,7));if(graphs_->property(key.toUtf8().constData()).value<QColor>()!=color){graphs_->setProperty(key.toUtf8().constData(),color);if(graphs_->isVisible())graphs_->update();for(auto *view:graphViews_){view->setProperty(key.toUtf8().constData(),color);if(view->isVisible())view->update();}}}
  statIcons_[i]->setToolTip(QString::fromUtf8(statDefinitions_[i].label.full)+": "+statValues_[i]->text());
  if(statValues_[i]->styleSheet()!=style)statValues_[i]->setStyleSheet(style);
  if(statNames_[i]->styleSheet()!=style)statNames_[i]->setStyleSheet(style);
 }
}
void LookoutDock::updateGlow(){
 const qint64 now=glowClock_.elapsed();
 if(glowPreviewUntil_ && now>=glowPreviewUntil_){glowPreviewUntil_=0;glowPreviewMetric_=-1;previewAlertTitle_.clear();previewAlertBody_.clear();applyMetricColors();refreshPanel();return;}
 const bool preview=glowPreviewUntil_>now;
 if(!ready_ || (!badge_->isVisible() && !quickVisible_) || (quick_ && quick_->overlayBlocked()) || (!preview && !settings_.value("glowEnabled",false).toBool())){badge_->setProperty("glowActive",false);glow_->hide();glowTimer_.stop();glowLastFrame_=now;return;}
 double risk=preview?glowPreviewRisk_:glowRisk();
 if(risk<0){badge_->setProperty("glowActive",false);glow_->hide();glowTimer_.stop();glowLastFrame_=now;return;}
 const auto targets=glowTargets();if(targets.isEmpty()){badge_->setProperty("glowActive",false);glow_->hide();glowTimer_.stop();glowLastFrame_=now;return;}
 const double period=glowPeriod(risk);
 glowPhase_+=std::max<qint64>(0,std::min<qint64>(100,now-glowLastFrame_))*6.283185307/period;glowLastFrame_=now;
 QRectF targetBounds;for(const auto &target:targets)targetBounds=targetBounds.isNull()?target:targetBounds.united(target);const QRect anchor=targetBounds.toAlignedRect();glow_->setGeometry(QRect(anchor.topLeft(),anchor.size()).adjusted(-glowPadding(),-glowPadding(),glowPadding(),glowPadding()));QVector<QRectF> localTargets;for(const auto &target:targets)localTargets<<target.translated(-anchor.topLeft());
 const double pulse=0.08+0.92*(0.5-0.5*std::cos(glowPhase_));
 static_cast<GlowOutline*>(glow_)->frame(warningColor(risk),pulse,std::clamp(settings_.value("glowIntensity",100).toDouble()/100.0,0.0,2.0),std::clamp(settings_.value("glowSpread",24).toInt(),4,64),localTargets);if(preview)applyMetricColors(pulse);
#ifdef _WIN32
 DWORD affinity=0;const DWORD requested=settings_.value("showInCaptures",false).toBool()?WDA_NONE:WDA_EXCLUDEFROMCAPTURE;if(!quick_ && (!GetWindowDisplayAffinity(reinterpret_cast<HWND>(badge_->winId()),&affinity) || affinity!=requested) && !showProtected(badge_)){badge_->setProperty("glowActive",false);glow_->hide();glowTimer_.stop();return;}
#endif
 badge_->setProperty("glowActive",true);glow_->show();glow_->raise();static_cast<NotificationPanel*>(badge_)->updateCards();
 if(!glowTimer_.isActive())glowTimer_.start();
}

int LookoutDock::overlayPadding() const {return settings_.value("glowEnabled",false).toBool() || glowPreviewUntil_>glowClock_.elapsed()?glowPadding():0;}
int LookoutDock::glowPadding() const {return std::clamp(settings_.value("glowSpread",24).toInt(),4,64)+2;}
double LookoutDock::glowPeriod(double risk) const {return (4000-3200*std::clamp(risk,0.0,1.0))/std::clamp(settings_.value("glowSpeed",1).toDouble(),0.25,2.0);}

void LookoutDock::setCaptureStatus(const QString &text){captureStatus_=text;if(dialog_)if(auto *label=dialog_->findChild<QLabel*>("captureStatus"))label->setText(text);}
void LookoutDock::setHotkeyBinding(int index,obs_key_combination_t combination){
 if(index<0 || index>=HotkeyCount)return;
 if(obs_key_combination_is_empty(combination))obs_hotkey_load_bindings(keys_[index],nullptr,0);
 else obs_hotkey_load_bindings(keys_[index],&combination,1);
 saveKeys();if(dialog_)if(auto *edit=dialog_->findChild<QLineEdit*>(QString("hotkeyEditor%1").arg(index))){QSignalBlocker blocker(edit);struct dstr text={};if(!obs_key_combination_is_empty(combination))obs_key_combination_to_str(combination,&text);edit->setText(QString::fromUtf8(text.array?text.array:""));dstr_free(&text);}refreshHotkeyEditors();
}
void LookoutDock::refreshHotkeyEditors(){
 if(!dialog_)return;
 QMap<QString,QStringList> bindings;
 obs_enum_hotkey_bindings([](void *data,size_t,obs_hotkey_binding_t *binding){
  auto &bindings=*static_cast<QMap<QString,QStringList>*>(data);struct dstr text={};obs_key_combination_to_str(obs_hotkey_binding_get_key_combination(binding),&text);
  bindings[QString::fromUtf8(obs_hotkey_get_name(obs_hotkey_binding_get_hotkey(binding)))]<<QString::fromUtf8(text.array?text.array:"");dstr_free(&text);return true;
 },&bindings);
 QStringList all;
 for(int i=0;i<HotkeyCount;++i){const QString text=bindings.value(names[i]).join(" / ");all<<text;if(auto *edit=dialog_->findChild<QLineEdit*>(QString("hotkeyEditor%1").arg(i)))if(!edit->hasFocus())edit->setText(text);}
 const QString fingerprint=all.join("\n");if(fingerprint!=hotkeyFingerprint_){hotkeyFingerprint_=fingerprint;saveKeys();}
}

double LookoutDock::glowRisk() const {return std::max({metricRisk(1),metricRisk(3),metricRisk(5)});}

int LookoutDock::panelMode() const {return std::clamp(settings_.value("panelMode",0).toInt(),0,2);}
void LookoutDock::syncVisibilityControls(){
 dockButtons_["toggleThumbnail"]->setChecked(thumbnailEnabled());
 dockButtons_["toggleMetrics"]->setChecked(metricsVisible_);
 dockButtons_["toggleCaptureVisibility"]->setChecked(settings_.value("showInCaptures",false).toBool());
 dockButtons_["dismissAlerts"]->setEnabled(alertActive_ || sessionNotice_);
 if(!dialog_)return;
 for(const auto &metric:statDefinitions_){
  if(auto *box=dialog_->findChild<QCheckBox*>(QString("metric_")+metric.id)){
   QSignalBlocker blocker(box);box->setChecked(metricEnabled(metric.id));
  }
  if(auto *graph=dialog_->findChild<QComboBox*>(QString("graph_")+metric.id)){
   QSignalBlocker blocker(graph);graph->setCurrentIndex(graphMode(metric.id));
  }
 }
 if(auto *mode=dialog_->findChild<QComboBox*>("presentation")){
  QSignalBlocker blocker(mode);mode->setCurrentIndex(mode->findData(displayMode()));
 }
 if(auto *box=dialog_->findChild<QCheckBox*>("independentCards"))box->setEnabled(!iconStripMode());
 if(auto *button=dialog_->findChild<QPushButton*>("dismissCurrentAlerts"))button->setEnabled(alertActive_ || sessionNotice_);
 if(auto *mode=dialog_->findChild<QComboBox*>("panelMode")){QSignalBlocker blocker(mode);mode->setCurrentIndex(panelMode());}
 for(const auto &name:QStringList{"showThumbnail","showInCaptures"})if(auto *check=dialog_->findChild<QCheckBox*>(name)){QSignalBlocker blocker(check);check->setChecked(name=="showThumbnail"?thumbnailEnabled():settings_.value(name,name=="showStatus").toBool());}
}
void LookoutDock::activateAction(int action){
 if(!ready_)return;
 if((action==0 || action==1) && transformTimer_.isActive()){transformTimer_.stop();saveTransform();}
 const bool wasDisabled=panelMode()==2;
 switch(action){
 case 0:editHidden_=false;settings_.setValue("panelMode",1);break;
 case 1:editHidden_=true;sessionNotice_=false;settings_.setValue("panelMode",0);glowPreviewUntil_=0;glowPreviewMetric_=-1;thumbnailAlertPreview_.stop();if(!alertActive_)dismiss_.stop();break;
 case 2:settings_.setValue("panelMode",2);glowPreviewUntil_=0;glowPreviewMetric_=-1;thumbnailAlertPreview_.stop();break;
 case 3:settings_.setValue("showThumbnail",!thumbnailEnabled());break;
 case 4:metricsVisible_=!metricsVisible_;break;
 case 5:settings_.setValue("showInCaptures",!settings_.value("showInCaptures",false).toBool());break;
 case 6:
  settings_.setValue("corner",(std::clamp(settings_.value("corner",1).toInt(),0,3)+1)%4);
  defaultPosition();syncGeometryControls();return;
 case 7:{
  QStringList presets;
  for(const auto &entry:settings_.value("positionPresets").toList())presets.append(entry.toMap().value("name").toString());
  if(!presets.isEmpty()){
   const int current=presets.indexOf(settings_.value("lastPositionPreset").toString());
   loadPreset(presets[(current+1)%presets.size()]);
  }
  return;
 }
 default:return;
 }
 settings_.sync();syncVisibilityControls();if(wasDisabled && panelMode()!=2){storageAlarm_=false;storageFull_=false;encoderAlarm_=false;networkAlarm_=false;reconnectAlarm_=false;tick();}refreshPanel();
}
void LookoutDock::previewThumbnailAlert(){
 if(thumbnailAlertPreview_.isActive())thumbnailAlertPreview_.stop();
 else thumbnailAlertPreview_.start();
 refreshPanel();
}
void LookoutDock::updateAlertIcon(){
 if(!alertIcon_)return;
 const bool preview=glowPreviewUntil_>glowClock_.elapsed() && !previewAlertTitle_.isEmpty();
 const bool enabled=(alertActive_ || preview) && settings_.value("showAlertIcon",true).toBool() && thumbnailEnabled() && (preview || glowPreviewUntil_<=glowClock_.elapsed());
 const bool thumbnailPreview=thumbnailAlertPreview_.isActive();
 if(!enabled && !thumbnailPreview){alertIcon_->hide();return;}
 QString role="alerts";
 const QString alert=thumbnailPreview?QStringLiteral("storage"):(preview?previewAlertTitle_:lastAlertTitle_+" "+lastAlertBody_).toLower();
 if(alert.contains("storage") || alert.contains("disk"))role="storage";else if(alert.contains("encoder"))role="encoder";else if(alert.contains("stream") || alert.contains("network"))role="network";else if(alert.contains("recording"))role="record";else if(alert.contains("camera"))role="camera";
 if(!thumbnailPreview && !metricEnabled(role=="record"?"recording":role=="stream"?"streaming":role)){alertIcon_->hide();return;}
 alertIcon_->setProperty("alertRole",role);const int size=std::clamp(std::min(preview_->width(),preview_->height())/2,24,160);alertIcon_->setFixedSize(size,size);const qreal tileDpr=preview_->devicePixelRatioF();QPixmap tile(QSize(int(std::ceil(size*tileDpr)),int(std::ceil(size*tileDpr))));tile.setDevicePixelRatio(tileDpr);tile.fill(Qt::transparent);QPainter painter(&tile);painter.setRenderHint(QPainter::Antialiasing);QColor backdrop=badge_->palette().color(QPalette::Window);backdrop.setAlpha(210);painter.setBrush(backdrop);const bool restored=(preview?previewAlertTitle_:lastAlertTitle_)=="Stream reconnected";const QColor rim=thumbnailPreview?warningColor(0):restored?badge_->palette().color(QPalette::WindowText):warningColor(preview?glowPreviewRisk_:std::max(0.0,glowRisk()));alertIcon_->setProperty("alertColor",rim);painter.setPen(QPen(rim,LookoutVisuals::BorderWidth));painter.drawRoundedRect(QRectF(.5,.5,size-1,size-1),LookoutVisuals::PanelRadius,LookoutVisuals::PanelRadius);const int inset=size/5;painter.drawPixmap(QRect(inset,inset,size-2*inset,size-2*inset),uiIcon(role,backdrop.lightness()<128).pixmap(QSize(int((size-2*inset)*tileDpr),int((size-2*inset)*tileDpr))));painter.end();alertIcon_->setPixmap(tile);LookoutVisuals::roundWidget(alertIcon_,LookoutVisuals::PanelRadius);alertIcon_->move((preview_->width()-size)/2,(preview_->height()-size)/2);alertIcon_->show();alertIcon_->raise();
}

bool LookoutDock::thumbnailEnabled() const {return settings_.value("showThumbnail",false).toBool();}
bool LookoutDock::thumbnailDisplayed() const {return thumbnailEnabled() || thumbnailAlertPreview_.isActive();}
void LookoutDock::updateBitrateCard(){
 bitrates_->setOutputs(recordingActive_,streamingActive_,recordingBitrate_,streamingBitrate_,historyClock_.elapsed());
 bitrates_->setVisible(metricsVisible_ && graphMode("bitrate")!=GraphOff && !iconStripMode());
}
std::optional<double> LookoutDock::diskMinutesRemaining() const {
 if(!recordingActive_ || recordingPaused_)return std::nullopt;
 if(freeBytes_==0)return 0;
 const auto seconds=recordingSpace_.secondsRemaining(freeBytes_);
 return seconds?std::optional<double>(std::min(*seconds/60,365*1440.0)):std::nullopt;
}
void LookoutDock::configureGraphs(){
 const int design=quick_?0:settings_.value("graphAppearance",1).toInt(),seconds=settings_.value("graphWindow",60).toInt();
 const std::array<double,HistoryGraphs::Count> thresholds={settings_.value("gib",20).toDouble(),
   settings_.value("skip",2).toDouble(),settings_.value("networkPercent",2).toDouble(),-1,-1,-1};
 const bool dark=quick_?quick_->panelPalette().color(QPalette::Window).lightness()<128:design==0?obs_frontend_is_theme_dark():design==8?darkLogo():design!=7;
 std::array<bool,HistoryGraphs::Count> selected{},mini{};
 std::array<QIcon,HistoryGraphs::Count> icons;
 for(int i=0;i<HistoryGraphs::Count;++i){
  const auto *id=HistoryGraphs::metrics[i].id;
  const auto mode=graphMode(id);
  selected[i]=mode!=GraphOff;mini[i]=mode==GraphMini;icons[i]=uiIcon(id,dark);
 }
 auto configure=[&](HistoryGraphs *graph,const std::array<bool,HistoryGraphs::Count> &selection){
  graph->setCompactMetrics(mini);graph->setMetricIcons(icons);graph->setAppearance(design);
  graph->configure(seconds,selection,thresholds);
 };
 configure(graphs_,selected);
 for(int i=0;i<HistoryGraphs::Count;++i){
  std::array<bool,HistoryGraphs::Count> individual{};
  individual[i]=true;
  configure(graphViews_[i],individual);
 }
 bitrates_->setCompact(graphMode("bitrate")==GraphMini);
 bitrates_->setAppearance(design);bitrates_->setWindowSeconds(seconds);
 bitrates_->setIcons(uiIcon("record",dark),uiIcon("stream",dark));
}


void LookoutDock::applyPanelAppearance(){
 const int design=quick_?0:std::clamp(settings_.value("graphAppearance",1).toInt(),0,8);
 replay_->setPresentation(design,graphMode("replay")==GraphMini,settings_.value("graphWindow",60).toInt(),uiIcon("replay",design==0?obs_frontend_is_theme_dark():design==8?darkLogo():design!=7),panelScale());
 const double scale=panelScale();if(settings_.value("panelScale",1).toInt()!=4){const int width=int(std::round(320*scale));settings_.setValue("panelWidth",width+16);settings_.setValue("thumbnailWidth",std::max(80,width-16));syncGeometryControls();}
 QFont textFont=font();if(textFont.pointSizeF()>0)textFont.setPointSizeF(textFont.pointSizeF()*scale);else textFont.setPixelSize(std::max(8,int(textFont.pixelSize()*scale)));if(design==6)textFont.setFamily("Consolas");if(badge_->property("panelFont").value<QFont>()!=textFont){badge_->setFont(textFont);badge_->setProperty("panelFont",textFont);}
 auto graphs=QVector<HistoryGraphs*>{graphs_};for(auto *view:graphViews_)graphs.append(view);for(auto *graph:graphs)if(graph){if(graph->property("uiScale").toDouble()!=scale){graph->setProperty("uiScale",scale);graph->setMinimumWidth(1);graph->updateGeometry();graph->update();}if(graph->property("cardSpacing")!=settings_.value("cardSpacing",8)){graph->setProperty("cardSpacing",settings_.value("cardSpacing",8));graph->updateGeometry();graph->update();}}
 if(bitrates_->property("uiScale").toDouble()!=scale){bitrates_->setProperty("uiScale",scale);bitrates_->setMinimumWidth(1);bitrates_->updateGeometry();bitrates_->update();}
 const int spacing=settings_.value("cardSpacing",8).toInt();panelLayout_->setSpacing(spacing);information_->layout()->setSpacing(spacing);badge_->layout()->setSpacing(spacing);
 for(int i=0;i<StatCount;++i){const int size=std::max(10,int(14*scale));statIcons_[i]->setFixedSize(size,size);statIcons_[i]->setPixmap(uiIcon(statDefinitions_[i].icon,design==0?obs_frontend_is_theme_dark():design==8?darkLogo():design!=7).pixmap(size,size));}

 const QPalette colors=quick_?quick_->panelPalette():LookoutVisuals::panelPalette(QApplication::palette(),design,darkLogo());
 if(badge_->property("appliedPanelPalette").value<QPalette>()!=colors || badge_->property("panelDesign").toInt()!=design){badge_->setPalette(colors);badge_->setProperty("appliedPanelPalette",colors);badge_->setProperty("panelDesign",design);badge_->update();}
 // Scope palette/font rules to the custom overlay, shielding it from OBS's
 // global QWidget stylesheet without changing native dock/settings controls.
 const QColor surface=colors.color(QPalette::Window);QString family=textFont.family();family.remove('"');const QString rules=QString("QWidget { color: %1; background-color: rgba(%2,%3,%4,0); selection-background-color: %5; font-family: \"%6\"; font-size: %7px; border: none; } QLabel { background: transparent; }").arg(colors.color(QPalette::WindowText).name()).arg(surface.red()).arg(surface.green()).arg(surface.blue()).arg(colors.color(QPalette::Highlight).name()).arg(family).arg(std::max(1,QFontInfo(textFont).pixelSize()));
 if(badge_->styleSheet()!=rules)badge_->setStyleSheet(rules);
 const double opacity=std::clamp(settings_.value("backgroundOpacity",100).toDouble()/100.0,0.0,1.0);
 if(badge_->property("backgroundOpacity").toDouble()!=opacity || !badge_->property("backgroundOpacity").isValid()){badge_->setProperty("backgroundOpacity",opacity);badge_->update();}
 const bool blur=settings_.value("blurAmount",0).toInt()>0;badge_->setProperty("clipBlur",blur);badge_->setProperty("publicCards",settings_.value("showInCaptures",false).toBool());
#ifdef _WIN32
 const auto handle=badge_->winId();if(backdropHandle_!=handle){backdropHandle_=handle;const HWND hwnd=reinterpret_cast<HWND>(handle);const int none=1;DwmSetWindowAttribute(hwnd,38,&none,sizeof(none));const MARGINS margins{0,0,0,0};DwmExtendFrameIntoClientArea(hwnd,&margins);setNativeBlur(hwnd,false);}
#endif
 static_cast<NotificationPanel*>(badge_)->updateCards();
 if(dialog_)if(auto *status=dialog_->findChild<QLabel*>("blurStatus")){status->setText("Background blur is unavailable on this system.");status->setVisible(blur&&!badge_->property("blurAvailable").toBool());}
}

void LookoutDock::replayPacket(obs_output_t*,encoder_packet *packet,encoder_packet_time*,void *data){if(packet)static_cast<LookoutDock*>(data)->replayTracker_.append(packet->dts_usec,packet->size,packet->type==OBS_ENCODER_VIDEO && packet->keyframe);}
void LookoutDock::detachReplay(){if(replayOutput_){obs_output_remove_packet_callback(replayOutput_,replayPacket,this);obs_output_release(replayOutput_);replayOutput_=nullptr;}replayTracker_.reset();}
void LookoutDock::updateReplay(){
 if(!ready_)return;replayActive_=obs_frontend_replay_buffer_active();obs_output_t *output=replayActive_?obs_frontend_get_replay_buffer_output():nullptr;
 if(output!=replayOutput_){detachReplay();replayOutput_=output;if(output){auto *settings=obs_output_get_settings(output);replayTracker_.reset(obs_data_get_int(settings,"max_time_sec"),quint64(std::max<long long>(0,obs_data_get_int(settings,"max_size_mb")))*1048576);obs_data_release(settings);obs_output_add_packet_callback(output,replayPacket,this);}}else if(output)obs_output_release(output);
 replay_->setSample(replayActive_,replayTracker_.sample(),historyClock_.elapsed());
}
