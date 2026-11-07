// SPDX-License-Identifier: GPL-2.0-or-later
#include "status-icons.hpp"
#include <QPainter>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <utility>

StatusIconsCard::StatusIconsCard(QWidget *parent):QWidget(parent){
 QSizePolicy policy(QSizePolicy::Preferred,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);setAutoFillBackground(false);setAccessibleName("Active outputs and alerts");
}
void StatusIconsCard::setState(bool recording,bool streaming,const QVector<AlertIcon> &alerts){
 const int before=iconCount();const QStringList oldRoles=roles();QVector<AlertIcon> unique;QSet<QString> present;
 for(const auto &alert:alerts){if(alert.id.isEmpty())continue;int found=-1;for(int i=0;i<unique.size();++i)if(unique[i].id==alert.id){found=i;break;}if(found>=0)unique[found]=alert;else unique<<alert;present.insert(alert.id);}
 freshAlerts_.intersect(present);
 for(const auto &alert:unique){bool known=false;for(const auto &old:alerts_)if(old.id==alert.id && old.color==alert.color && old.label==alert.label){known=true;break;}if(!known)freshAlerts_.insert(alert.id);}
 recording_=recording;streaming_=streaming;alerts_=std::move(unique);
 QStringList labels;for(const auto &item:persistent_)labels<<item.label;if(recording_)labels<<"Recording active";if(streaming_)labels<<"Streaming active";for(const auto &alert:alerts_)labels<<(!alert.label.isEmpty()?alert.label:alert.id);setToolTip(labels.join("\n"));setAccessibleDescription(labels.join(", "));
 if(before!=iconCount() || oldRoles!=roles())updateGeometry();update();
}
void StatusIconsCard::setIcons(QIcon recording,QIcon streaming){recordingIcon_=std::move(recording);streamingIcon_=std::move(streaming);update();}
void StatusIconsCard::setPersistentIcons(const QVector<AlertIcon> &icons){const auto before=roles();persistent_=icons;if(before!=roles())updateGeometry();update();}
void StatusIconsCard::setScale(double scale){scale=std::isfinite(scale)?std::clamp(scale,.4,4.0):1.0;if(scale_==scale)return;scale_=scale;updateGeometry();update();}
void StatusIconsCard::setPulse(double pulse){pulse=std::isfinite(pulse)?std::clamp(pulse,0.0,1.0):1.0;if(pulse>=.999)freshAlerts_.clear();if(pulse_==pulse)return;pulse_=pulse;update();}
QStringList StatusIconsCard::roles() const {QStringList result;if(persistent_.isEmpty()){if(recording_)result<<"recording";if(streaming_)result<<"streaming";}else for(const auto &item:persistent_)result<<item.id;for(const auto &alert:alerts_){bool present=false;for(const auto &item:persistent_)present|=item.id==alert.id;if(!present)result<<"alert:"+alert.id;}return result;}
int StatusIconsCard::columnsFor(int width) const {const double pad=8*scale_,glyph=24*scale_,gap=8*scale_;return std::max(1,std::min(iconCount(),int(std::floor((width-2*pad+gap)/(glyph+gap)))));}
QSize StatusIconsCard::sizeHint() const {const int count=iconCount();if(!count)return QSize(0,0);return QSize(int(std::ceil((16+count*24+(count-1)*8)*scale_)),int(std::ceil(40*scale_)));}
int StatusIconsCard::heightForWidth(int width) const {const int count=iconCount();if(!count)return 0;const int columns=columnsFor(width),rows=(count+columns-1)/columns;return int(std::ceil((16+rows*24+(rows-1)*8)*scale_));}
QVector<StatusIconsCard::Glyph> StatusIconsCard::glyphs() const {
 QVector<Glyph> result;const bool light=palette().color(QPalette::Window).lightness()>128;
 if(persistent_.isEmpty()){
 if(recording_)result<<Glyph{recordingIcon_.isNull()?style()->standardIcon(QStyle::SP_MediaStop):recordingIcon_,QColor(light?"#be2c37":"#ff4b55"),{}};
 if(streaming_)result<<Glyph{streamingIcon_.isNull()?style()->standardIcon(QStyle::SP_ArrowUp):streamingIcon_,QColor(light?"#176eaa":"#40baf0"),{}};
 }else for(const auto &item:persistent_){Glyph glyph{item.icon,item.color,{}};for(const auto &alert:alerts_)if(alert.id==item.id){glyph.color=alert.color;glyph.alertId=alert.id;}result<<glyph;}
 for(const auto &alert:alerts_){bool present=false;for(const auto &item:persistent_)present|=item.id==alert.id;if(!present)result<<Glyph{alert.icon.isNull()?style()->standardIcon(QStyle::SP_MessageBoxWarning):alert.icon,alert.color.isValid()?alert.color:palette().color(QPalette::WindowText),alert.id};}return result;
}
void StatusIconsCard::paintEvent(QPaintEvent*){
 if(!iconCount())return;QPainter painter(this);painter.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);const auto items=glyphs();const int columns=columnsFor(width());const double padding=8*scale_,glyphSize=24*scale_,step=32*scale_;
 for(int i=0;i<items.size();++i){const auto &item=items[i];const double pop=!item.alertId.isEmpty() && freshAlerts_.contains(item.alertId)?.85+.15*pulse_:1.0;const double size=glyphSize*pop,offset=(glyphSize-size)/2;
  const QRectF target(padding+(i%columns)*step+offset,padding+(i/columns)*step+offset,size,size);const int pixels=std::max(1,int(std::ceil(size*devicePixelRatioF())));QPixmap glyph=item.icon.pixmap(pixels,pixels);glyph.setDevicePixelRatio(1);
  if(glyph.isNull())continue;{QPainter ink(&glyph);ink.setCompositionMode(QPainter::CompositionMode_SourceIn);ink.fillRect(glyph.rect(),item.color);}painter.drawPixmap(target,glyph,QRectF(glyph.rect()));
 }
}
