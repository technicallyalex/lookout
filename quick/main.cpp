// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared-preview.hpp"
#include "backdrop.hpp"
#include "panel-transition.hpp"
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQuickGraphicsDevice>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QQuickImageProvider>
#include <QSvgRenderer>
#include <QPainter>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <QFile>
#include <QScreen>
#include <QSurfaceFormat>
#include <QKeyEvent>
#include <QCursor>
#include <QQuickStyle>
#include <QtMath>
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <cstdio>
#include <algorithm>
#include <array>
#include <memory>
#include <optional>

class Glyphs final:public QQuickImageProvider {
public:Glyphs():QQuickImageProvider(QQuickImageProvider::Image){}
 QImage requestImage(const QString &id,QSize *size,const QSize &requested) override{
  const auto fields=id.split('/');const QString name=fields.value(0);static const QRegularExpression safe("^[a-z-]+$");if(!safe.match(name).hasMatch())return {};const QSize target=requested.isValid()?requested:QSize(96,96);if(size)*size=target;QImage image(target.boundedTo(QSize(512,512)),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QSvgRenderer svg(":/lookout/icons/"+QString(fields.value(2)=="dark"?"dark/":"light/")+name+".svg");QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);svg.render(&p);QColor color('#'+fields.value(1));if(color.isValid()){p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(image.rect(),color);}return image;
 }
};
class Controller final:public QObject {
 Q_OBJECT
 Q_PROPERTY(QVariantMap scene READ scene NOTIFY changed)
 Q_PROPERTY(QVariantMap motion READ motion NOTIFY motionChanged)
 Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
 Q_PROPERTY(int shortcutIndex READ shortcutIndex NOTIFY shortcutChanged)
public:
 QQuickView view;
 std::unique_ptr<QQuickView> settingsView;
 HICON settingsCaptionIcon=nullptr;
 std::optional<bool> settingsIconDark;
 QTimer settingsFit;
 QLocalSocket socket;
 QVariantMap scene()const{return scene_;}
 QVariantMap motion()const{return motion_;}
 QVariantMap settings() const { return settings_; }
 int shortcutIndex() const { return shortcutIndex_; }
 QByteArray incoming;
 QVariantMap scene_;
 QVariantMap motion_;
 QJsonObject visibleScene_;
 PanelTransition transition_;
 QElapsedTimer motionClock_;
 QTimer motionTimer_;
 bool targetVisible_=false;
 QVariantMap settings_;
 int shortcutIndex_=-1;
 int pendingKey_=0;
 int pendingModifiers_=0;
 QElapsedTimer heartbeat;
 HANDLE parent=nullptr;
 bool initialized=false,privateMode=true;
 bool colorPickerOpen=false;
 QVector<HWND> backdrops;
 BackdropHost backdropHost;
 Controller(){
  motionTimer_.setTimerType(Qt::PreciseTimer);
  motionTimer_.setInterval(16);
  connect(&motionTimer_,&QTimer::timeout,this,[this]{advanceTransition();});
  settingsFit.setSingleShot(true);
  connect(&settingsFit,&QTimer::timeout,this,[this]{fitSettings(false);});
 }
 ~Controller(){
  if(settingsView){
   settingsView->removeEventFilter(this);
   RemovePropW(reinterpret_cast<HWND>(settingsView->winId()),L"LookoutSettings");
  }
  settingsView.reset();
  if(settingsCaptionIcon)DestroyIcon(settingsCaptionIcon);
  clearBackdrops();
  if(parent)CloseHandle(parent);
 }
 void send(QJsonObject o){socket.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n');socket.flush();}
 Q_INVOKABLE void beginTransform(const QString &card,bool resize){
  if(motionTimer_.isActive())return;
  const QPoint origin=QCursor::pos();
  send({{"type","transform"},{"phase","begin"},{"card",card},{"resize",resize},
        {"trackPointer",true},{"sx",origin.x()},{"sy",origin.y()},{"ex",origin.x()},{"ey",origin.y()}});
 }
 Q_INVOKABLE void dismiss(){send({{"type","dismiss"}});}
 Q_INVOKABLE void setting(QString action,QVariant value={},QString key={}){
  send({{"type","settings"},{"action",action},{"key",key},{"value",QJsonValue::fromVariant(value)}});
 }
 Q_INVOKABLE void chooseGlowColor(const QString &key,const QString &code){
  const QColor current(code);
  if(!settingsView || colorPickerOpen || !current.isValid() ||
     (key!="glowWarningColor" && key!="glowCriticalColor"))return;
  static auto custom=[]{
   std::array<COLORREF,16> colors;
   colors.fill(RGB(255,255,255));
   return colors;
  }();
  CHOOSECOLORW dialog{};
  dialog.lStructSize=sizeof(dialog);
  dialog.hwndOwner=reinterpret_cast<HWND>(settingsView->winId());
  dialog.rgbResult=RGB(current.red(),current.green(),current.blue());
  dialog.lpCustColors=custom.data();
  dialog.Flags=CC_RGBINIT|CC_FULLOPEN;
  colorPickerOpen=true;
  const bool accepted=ChooseColorW(&dialog);
  colorPickerOpen=false;
  heartbeat.restart();
  if(accepted){
   const QColor selected(GetRValue(dialog.rgbResult),GetGValue(dialog.rgbResult),GetBValue(dialog.rgbResult));
   setting("set",selected.name(),key);
  }
 }
 Q_INVOKABLE void captureShortcut(int index){
  pendingKey_=0;
  shortcutIndex_=index;
  emit shortcutChanged();
  setting("capture",index>=0);
 }
 Q_INVOKABLE void clearShortcut(int index){
  captureShortcut(-1);
  send({{"type","settings"},{"action","shortcut"},{"index",index},{"value",0}});
 }
 void fitSettings(bool center){
  if(!settingsView || (!center && !settingsView->isVisible()) ||
     settingsView->windowState()==Qt::WindowMaximized)return;
  const auto *root=settingsView->rootObject();
  const auto *display=settingsView->screen();
  if(!root || !display)return;
  const QRect bounds=display->availableGeometry().adjusted(16,32,-16,-16);
  if(bounds.isEmpty())return;
  const QSize minimum=QSize(560,0).boundedTo(bounds.size());
  const QSize size=QSize(center?qCeil(root->implicitWidth()):settingsView->width(),qCeil(root->implicitHeight()))
   .expandedTo(minimum).boundedTo(bounds.size());
  if(size.isEmpty())return;
  settingsView->setMinimumSize(minimum);
  QPoint pos=center?bounds.center()-QPoint(size.width()/2,size.height()/2):settingsView->position();
  pos.setX(std::clamp(pos.x(),bounds.left(),bounds.right()-size.width()+1));
  pos.setY(std::clamp(pos.y(),bounds.top(),bounds.bottom()-size.height()+1));
  const QRect geometry(pos,size);
  if(settingsView->geometry()!=geometry)settingsView->setGeometry(geometry);
 }
 void acceptSettings(const QJsonObject &state,bool open){
  settings_=state.toVariantMap();
  emit settingsChanged();
  if(!open){
   updateSettingsIcon();
   return;
  }
  const bool firstOpen=!settingsView;
  if(firstOpen){
   settingsView=std::make_unique<QQuickView>();
   settingsView->setInitialProperties({{"controller",QVariant::fromValue(this)}});
   // A visually blank caption preserves the native window buttons.
   settingsView->setTitle(QStringLiteral(" "));
   settingsView->setFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
   settingsView->engine()->addImageProvider("glyph",new Glyphs);
   settingsView->setResizeMode(QQuickView::SizeRootObjectToView);
   auto *display=QGuiApplication::screenAt(QCursor::pos());
   if(!display)display=QGuiApplication::primaryScreen();
   settingsView->setScreen(display);
   settingsView->resize(800,440);
   settingsView->installEventFilter(this);
   settingsView->setSource(QUrl("qrc:/quick/Settings.qml"));
   if(settingsView->status()==QQuickView::Error){
    settingsView.reset();
    setting("close");
    return;
   }
   auto *root=settingsView->rootObject();
   connect(root,&QQuickItem::implicitHeightChanged,this,[this]{settingsFit.start();});
   connect(settingsView.get(),&QWindow::screenChanged,this,[this]{
    settingsFit.start();
    QTimer::singleShot(0,this,&Controller::updateSettingsIcon);
   });
   fitSettings(true);
  }
  updateSettingsIcon();
  settingsView->show();
  settingsView->raise();
  settingsView->requestActivate();
  settingsFit.start();
  const auto settingsWindow=reinterpret_cast<HWND>(settingsView->winId());
  SetPropW(settingsWindow,L"LookoutSettings",reinterpret_cast<HANDLE>(1));
  const auto extendedStyle=GetWindowLongPtrW(settingsWindow,GWL_EXSTYLE);
  if(!(extendedStyle&WS_EX_DLGMODALFRAME)){
   SetWindowLongPtrW(settingsWindow,GWL_EXSTYLE,extendedStyle|WS_EX_DLGMODALFRAME);
   SetWindowPos(settingsWindow,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
  }
 }
 void updateSettingsIcon(){
  if(!settingsView)return;
  const bool dark=settings_.value("logoDark").toBool();
  if(settingsIconDark!=dark){
   settingsView->setIcon(QIcon(QString(":/lookout/brand/")+(dark?"moon.svg":"light.svg")));
   settingsIconDark=dark;
  }
  if(!settingsCaptionIcon){
   // A real, fully transparent small icon prevents Windows from supplying
   // its default application glyph while keeping the native caption buttons.
   std::array<BYTE,32> andMask,xorMask{};
   andMask.fill(0xff);
   settingsCaptionIcon=CreateIcon(GetModuleHandleW(nullptr),16,16,1,1,andMask.data(),xorMask.data());
  }
  const auto settingsWindow=reinterpret_cast<HWND>(settingsView->winId());
  if(settingsCaptionIcon && SendMessageW(settingsWindow,WM_GETICON,ICON_SMALL,0)!=reinterpret_cast<LPARAM>(settingsCaptionIcon))
   SendMessageW(settingsWindow,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(settingsCaptionIcon));
 }
 bool eventFilter(QObject *object,QEvent *event) override {
  if(object!=settingsView.get())return false;
  if(event->type()==QEvent::Close || event->type()==QEvent::Hide){
   captureShortcut(-1);
   setting("close");
  }
  if(event->type()==QEvent::WindowDeactivate && shortcutIndex_>=0)captureShortcut(-1);
  if(shortcutIndex_<0)return false;
  if(event->type()==QEvent::ShortcutOverride){event->accept();return true;}
  if(event->type()!=QEvent::KeyPress && event->type()!=QEvent::KeyRelease)return false;
  auto *key=static_cast<QKeyEvent*>(event);
  if(key->isAutoRepeat())return true;
  if(event->type()==QEvent::KeyPress){
   if(key->key()==Qt::Key_Escape){captureShortcut(-1);return true;}
   if(key->key()==Qt::Key_Control || key->key()==Qt::Key_Shift ||
      key->key()==Qt::Key_Alt || key->key()==Qt::Key_Meta)return true;
   pendingKey_=int(key->nativeVirtualKey());
   const auto modifiers=key->modifiers();
   pendingModifiers_=(modifiers.testFlag(Qt::ShiftModifier)?1:0) |
                     (modifiers.testFlag(Qt::ControlModifier)?2:0) |
                     (modifiers.testFlag(Qt::AltModifier)?4:0) |
                     (modifiers.testFlag(Qt::MetaModifier)?8:0);
  }else if(pendingKey_ && pendingKey_==int(key->nativeVirtualKey())){
   const int index=shortcutIndex_;
   const int native=pendingKey_;
   const int modifiers=pendingModifiers_;
   QTimer::singleShot(100,this,[this,index,native,modifiers]{
    if(shortcutIndex_!=index || pendingKey_!=native)return;
    send({{"type","settings"},{"action","shortcut"},{"index",index},{"value",native},{"modifiers",modifiers}});
    captureShortcut(-1);
   });
  }
  return true;
 }
 bool protect(HWND hwnd,bool publicMode){DWORD actual=0,requested=publicMode?WDA_NONE:WDA_EXCLUDEFROMCAPTURE;if(GetWindowDisplayAffinity(hwnd,&actual) && actual==requested)return true;return SetWindowDisplayAffinity(hwnd,requested) && GetWindowDisplayAffinity(hwnd,&actual) && actual==requested;}
 static QRect rect(const QJsonObject &r){
  return {r["x"].toInt(),r["y"].toInt(),r["w"].toInt(),r["h"].toInt()};
 }
 static QString cardKey(const QJsonObject &card){
  return card["id"].toString()+":"+card["label"].toString(card["type"].toString());
 }
 static bool sameLayout(const QJsonObject &a,const QJsonObject &b){
  if(a["bounds"]!=b["bounds"] || a["screen"]!=b["screen"])return false;
  const auto old=a["cards"].toArray(), next=b["cards"].toArray();
  if(old.size()!=next.size())return false;
  for(int i=0;i<old.size();++i){
   const auto first=old[i].toObject(), second=next[i].toObject();
   if(cardKey(first)!=cardKey(second) || rect(first)!=rect(second))return false;
  }
  return true;
 }
 void setMotion(const QVariantMap &frame){
  if(motion_==frame)return;
  motion_=frame;
  emit motionChanged();
 }
 void hideNow(){
  motionTimer_.stop();
  targetVisible_=false;
  view.hide();hideBackdrops();
  visibleScene_={};
  if(!scene_.isEmpty()){scene_.clear();emit changed();}
  setMotion({});
 }
 void showOnTop(){
  if(view.isVisible())return;
  view.show();
  SetWindowPos(reinterpret_cast<HWND>(view.winId()),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
 }
 void settle(){
  motionTimer_.stop();
  if(!targetVisible_){hideNow();return;}
  const auto bounds=rect(visibleScene_["bounds"].toObject());
  if(view.geometry()!=bounds)view.setGeometry(bounds);
  QRegion region;
  const int margin=visibleScene_["glow"].toBool()?visibleScene_["glowSpread"].toInt()+3:2;
  for(const auto &entry:visibleScene_["cards"].toArray())
   region+=rect(entry.toObject()).adjusted(-margin,-margin,margin,margin);
  view.setMask(region);
  setMotion({});
  showOnTop();
  syncBackdrops(visibleScene_,reinterpret_cast<HWND>(view.winId()));
  view.update();
 }
 void renderTransition(const PanelTransition::Frame &frame){
  const auto bounds=rect(visibleScene_["bounds"].toObject());
  const auto cards=visibleScene_["cards"].toArray();
  QVariantMap offsets;
  for(int i=0;i<cards.size();++i){
   const auto card=cards[i].toObject();
   const QPointF at=bounds.topLeft()+rect(card).topLeft();
   offsets.insert(cardKey(card),frame.cards[i].topLeft()-at);
  }
  setMotion({{"offset",bounds.topLeft()-view.position()},{"cards",offsets},
             {"opacity",frame.opacity},{"blur",frame.blur},{"flash",frame.flash}});
  view.update();
 }
 void advanceTransition(){
  const int elapsed=int(motionClock_.elapsed());
  if(elapsed>=transition_.duration()){settle();return;}
  renderTransition(transition_.sample(elapsed));
 }
 void startTransition(PanelTransition::Style style,const PanelTransition::Frame *previous){
  const auto bounds=rect(visibleScene_["bounds"].toObject());
  QRect screen=rect(visibleScene_["screen"].toObject());
  if(screen.isEmpty()){
   auto *display=QGuiApplication::screenAt(bounds.center());
   screen=display?display->geometry():bounds;
  }
  QVector<QRectF> cards;
  for(const auto &entry:visibleScene_["cards"].toArray())
   cards.append(rect(entry.toObject()).translated(bounds.topLeft()));
  transition_.begin(style,targetVisible_,cards,screen,visibleScene_["right"].toBool(),previous);
  const int margin=std::max(visibleScene_["glow"].toBool()?visibleScene_["glowSpread"].toInt()+3:2,
                            style==PanelTransition::Style::Blur?26:2);
  const auto envelope=transition_.envelope().toAlignedRect().adjusted(-margin,-margin,margin,margin);
  const QRect canvas=bounds.united(envelope.intersected(screen));
  hideBackdrops();
  if(view.geometry()!=canvas)view.setGeometry(canvas);
  view.setMask({});
  renderTransition(transition_.sample(0));
  showOnTop();
  motionClock_.start();
  motionTimer_.start();
 }
 void accept(const QJsonObject &s){
  const auto bounds=rect(s["bounds"].toObject());
  if(s["version"].toInt()!=1 || bounds.isEmpty() || bounds.width()>32768 || bounds.height()>32768 || s["cards"].toArray().size()>32){hideNow();return;}
  const auto blockReason=s["blockReason"].toString();
  if(!blockReason.isEmpty()){hideNow();send({{"type","status"},{"text",blockReason}});return;}
  const auto texture=s["texture"].toObject();if(!initialized){const quint32 low=quint32(texture["luidLow"].toDouble());const qint32 high=qint32(texture["luidHigh"].toDouble());if(!texture["graphicsReady"].toBool())return;if(low || high)view.setGraphicsDevice(QQuickGraphicsDevice::fromAdapter(low,high));view.engine()->addImageProvider("glyph",new Glyphs);view.rootContext()->setContextProperty("controller",this);view.setColor(Qt::transparent);view.setResizeMode(QQuickView::SizeRootObjectToView);view.setTitle("lookout panels");view.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);view.setSource(QUrl("qrc:/quick/Main.qml"));if(view.status()==QQuickView::Error){send({{"type","status"},{"text","lookout QML could not load. Reinstall the complete package."}});QCoreApplication::exit(2);return;}initialized=true;}
  // Hide before changing affinity or recreating styles: no public frame during
  // a private/public transition. Verify every native window before exposure.
  const bool publicMode=s["public"].toBool();const bool edit=s["edit"].toBool();
  const bool inputChange=view.flags().testFlag(Qt::WindowTransparentForInput)==edit;
  const bool hardCut=(privateMode==publicMode && view.isVisible()) || inputChange || s["immediate"].toBool();
  if(hardCut)hideNow();
  if(inputChange)view.setFlag(Qt::WindowTransparentForInput,!edit);
  privateMode=!publicMode;
  const auto hwnd=reinterpret_cast<HWND>(view.winId());if(!protect(hwnd,publicMode)){hideNow();send({{"type","status"},{"text","Panel hidden: Windows capture exclusion could not be verified"}});return;}
  LONG_PTR flags=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);flags|=WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE;if(edit)flags&=~WS_EX_TRANSPARENT;else flags|=WS_EX_TRANSPARENT;SetWindowLongPtrW(hwnd,GWL_EXSTYLE,flags);
  const auto style=PanelTransition::parse(s["transition"].toString());
  const bool animate=!hardCut && !edit && style!=PanelTransition::Style::Instant;
  const bool visible=s["visible"].toBool() && !s["cards"].toArray().isEmpty();
  if(visible){
   const bool stable=sameLayout(visibleScene_,s);
   const bool entering=!targetVisible_;
   const bool reversing=entering && stable && motionTimer_.isActive();
   const auto previous=reversing?transition_.sample(int(motionClock_.elapsed())):PanelTransition::Frame{};
   visibleScene_=s;
   targetVisible_=true;
   view.setIcon(QIcon(QString(":/lookout/brand/")+(s["logoDark"].toBool()?"moon.svg":"light.svg")));
   scene_=s.toVariantMap();emit changed();
   if(entering && animate)startTransition(style,reversing?&previous:nullptr);
   else if(!stable || !motionTimer_.isActive() || !animate)settle();
  }else if(targetVisible_ && animate && view.isVisible()){
   const bool reversing=motionTimer_.isActive();
   const auto previous=reversing?transition_.sample(int(motionClock_.elapsed())):PanelTransition::Frame{};
   targetVisible_=false;
   startTransition(style,reversing?&previous:nullptr);
  }else if(!motionTimer_.isActive() || !animate)hideNow();
  send({{"type","status"},{"text",publicMode?"Panels can appear in desktop capture":"Windows capture exclusion is on"}});
 }
 void hideBackdrops(){for(auto w:backdrops)ShowWindow(w,SW_HIDE);}
 void clearBackdrops(){while(!backdrops.isEmpty())removeBackdrop();}
 void removeBackdrop(){const auto window=backdrops.takeLast();backdropHost.remove(window);DestroyWindow(window);}
 void syncBackdrops(const QJsonObject &s,HWND panel){
  const int amount=std::clamp(s["blurAmount"].toInt(),0,100);
  if(amount==0){clearBackdrops();return;}
  const auto cards=s["cards"].toArray();
  static bool registered=false;if(!registered){WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LookoutQuickBackdrop";cls.lpfnWndProc=[](HWND hwnd,UINT message,WPARAM w,LPARAM l)->LRESULT{if(message==WM_NCCALCSIZE && w)return 0;if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_PAINT){PAINTSTRUCT paint;BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);return 0;}return DefWindowProcW(hwnd,message,w,l);};registered=RegisterClassW(&cls)!=0;}
  while(backdrops.size()>cards.size())removeBackdrop();while(registered && backdrops.size()<cards.size()){HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,L"LookoutQuickBackdrop",L"",WS_POPUP|WS_THICKFRAME|WS_CAPTION,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!w)break;backdrops<<w;}
  RECT origin{};GetWindowRect(panel,&origin);const qreal dpi=view.devicePixelRatio();
  for(int i=0;i<backdrops.size();++i){
   auto w=backdrops[i];
   if(!protect(w,s["public"].toBool())){ShowWindow(w,SW_HIDE);continue;}
   if(!backdropHost.update(w,amount)){ShowWindow(w,SW_HIDE);continue;}
   const auto c=cards[i].toObject();
   const int x=origin.left+qRound(c["x"].toDouble()*dpi),y=origin.top+qRound(c["y"].toDouble()*dpi),
    width=qRound(c["w"].toDouble()*dpi),height=qRound(c["h"].toDouble()*dpi);
   const int corner=2;const DWORD border=0xfffffffe;
   DwmSetWindowAttribute(w,33,&corner,sizeof(corner));DwmSetWindowAttribute(w,34,&border,sizeof(border));
   SetWindowPos(w,panel,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
  }
 }
signals:
 void changed();
 void motionChanged();
 void settingsChanged();
 void shortcutChanged();
};
int main(int argc,char **argv){
 qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString &message){const auto text=message.toUtf8();std::fprintf(stderr,"%s\n",text.constData());std::fflush(stderr);});
 QGuiApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QCoreApplication::setApplicationName("lookout UI");QQuickStyle::setStyle("Fusion");QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);QSurfaceFormat format;format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);qmlRegisterType<SharedPreview>("Lookout",1,0,"SharedPreview");
 QCommandLineParser args;args.addOption(QCommandLineOption(QStringLiteral("socket"),QStringLiteral("Local endpoint"),QStringLiteral("endpoint")));args.addOption(QCommandLineOption(QStringLiteral("token"),QStringLiteral("Session token"),QStringLiteral("token")));args.addOption(QCommandLineOption(QStringLiteral("parent"),QStringLiteral("OBS process"),QStringLiteral("pid")));args.process(app);if(args.value("socket").isEmpty() || args.value("token").isEmpty() || args.value("parent").isEmpty())return 1;
 Controller c;c.parent=OpenProcess(SYNCHRONIZE,FALSE,args.value("parent").toULong());if(!c.parent)return 1;c.heartbeat.start();
 QObject::connect(&c.socket,&QLocalSocket::connected,&c,[&]{c.send({{"type","hello"},{"version",1},{"token",args.value("token")}});});QObject::connect(&c.socket,&QLocalSocket::disconnected,&app,&QCoreApplication::quit);
 QObject::connect(&c.socket,&QLocalSocket::readyRead,&c,[&]{c.incoming+=c.socket.readAll();if(c.incoming.size()>2*1024*1024){app.exit(3);return;}for(int end;(end=c.incoming.indexOf('\n'))>=0;){auto line=c.incoming.left(end);c.incoming.remove(0,end+1);QJsonParseError error;auto document=QJsonDocument::fromJson(line,&error);if(error.error!=QJsonParseError::NoError){app.exit(3);return;}auto object=document.object();c.heartbeat.restart();if(object["type"]=="quit"){app.quit();return;}if(object["type"]=="scene")c.accept(object["scene"].toObject());else if(object["type"]=="settings")c.acceptSettings(object["state"].toObject(),object["open"].toBool());}});
 QTimer watchdog;watchdog.setInterval(500);QObject::connect(&watchdog,&QTimer::timeout,&c,[&]{if((!c.colorPickerOpen && c.heartbeat.elapsed()>5000) || WaitForSingleObject(c.parent,0)!=WAIT_TIMEOUT){c.view.hide();c.hideBackdrops();app.quit();}});watchdog.start();
 c.socket.connectToServer(args.value("socket"));return app.exec();
}
#include "main.moc"
