// SPDX-License-Identifier: GPL-2.0-or-later
#include "bitrate-card.hpp"
#include "history-graphs.hpp"
#include <QPainter>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QString rateLabel(double bps){
 if(!std::isfinite(bps))return QStringLiteral("Measuring…");
 const double unit=bps>=1e9?1e9:bps>=1e6?1e6:bps>=1e3?1e3:1;
 const QString suffix=unit==1e9?"Gb/s":unit==1e6?"Mb/s":unit==1e3?"kb/s":"b/s";
 return QString::number(bps/unit,'f',unit==1?0:1)+" "+suffix;
}
}
BitrateCard::BitrateCard(QWidget *parent):QWidget(parent){
 current_.fill(NAN);QSizePolicy policy(QSizePolicy::Preferred,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);setMinimumWidth(120);setAccessibleName("Recording and streaming bitrate");
}
void BitrateCard::setOutputs(bool recording,bool streaming,std::optional<double> recordingBps,std::optional<double> streamingBps,qint64 timeMs){
 const std::array<bool,2> active={recording,streaming};const std::array<std::optional<double>,2> rates={recordingBps,streamingBps};
 const bool changed=active!=active_;const auto oldCurrent=current_;const int oldRows=int(active_[0])+int(active_[1]);
 for(int i=0;i<2;++i){if(active[i]!=active_[i])++generation_[i];current_[i]=active[i] && rates[i] && std::isfinite(*rates[i]) && *rates[i]>=0?*rates[i]:NAN;}active_=active;
 // A restarted monotonic clock invalidates time positions. Rapid activity changes
 // still get their own sample/generation so stopped sessions cannot be bridged.
 if(!history_.empty() && timeMs+2000<history_.back().timeMs)history_.clear();
 now_=std::max<qint64>(0,timeMs);bool appended=false;
 if(history_.empty() || changed || now_-history_.back().timeMs>=1900){
  const qint64 stamp=history_.empty()?now_:std::max(now_,history_.back().timeMs+1);history_.push_back({stamp,current_,generation_});now_=stamp;appended=true;
  while(history_.size()>512 || (history_.size()>1 && history_[1].timeMs<now_-900000))history_.pop_front();
 }
 if(oldRows!=int(active_[0])+int(active_[1]))updateGeometry();
 if(changed || appended || !std::equal(current_.begin(),current_.end(),oldCurrent.begin(),[](double a,double b){return a==b || (std::isnan(a)&&std::isnan(b));})){if(isVisible())update();}
 QStringList outputs;if(recording)outputs<<"Recording: "+rateLabel(current_[0]);if(streaming)outputs<<"Streaming: "+rateLabel(current_[1]);setToolTip(outputs.isEmpty()?"Bitrate: no active recording or stream":outputs.join("\n"));
}
void BitrateCard::setIcons(QIcon recording,QIcon streaming){icons_={std::move(recording),std::move(streaming)};if(isVisible())update();}
void BitrateCard::setAppearance(int design){design=std::clamp(design,0,8);if(appearance_==design)return;appearance_=design;updateGeometry();if(isVisible())update();}
void BitrateCard::setCompact(bool compact){if(compact_==compact)return;compact_=compact;updateGeometry();if(isVisible())update();}
void BitrateCard::setWindowSeconds(int seconds){seconds=std::clamp(seconds,30,900);if(seconds_==seconds)return;seconds_=seconds;if(isVisible())update();}
void BitrateCard::clearHistory(){history_.clear();current_.fill(NAN);if(isVisible())update();}
double BitrateCard::uiScale() const {return std::clamp(property("uiScale").isValid()?property("uiScale").toDouble():1.0,0.4,4.0);}
QFont BitrateCard::drawingFont() const {
 QFont result=font();const double scale=uiScale();if(result.pixelSize()>0)result.setPixelSize(std::max(1,int(std::round(result.pixelSize()/scale))));else result.setPointSizeF(std::max(1.0,result.pointSizeF()/scale));
 if(appearance_==6){QFont mono=QFontDatabase::systemFont(QFontDatabase::FixedFont);if(result.pixelSize()>0)mono.setPixelSize(result.pixelSize());else mono.setPointSizeF(result.pointSizeF());result=mono;}return result;
}
double BitrateCard::logicalHeight(double width) const {
 const int rows=std::max(1,int(active_[0])+int(active_[1]));if(compact_)return 16+std::max(40,rows*20);
 const double line=std::max(20.0,std::ceil(QFontMetricsF(drawingFont()).height())+4);const bool narrow=width<260;
 return graphs_?8+rows*line*(narrow?2:1)+6+64+line+8:16+rows*line*(narrow?2:1);
}
int BitrateCard::heightForWidth(int width) const {return int(std::ceil(logicalHeight(width/uiScale())*uiScale()));}
double BitrateCard::niceMaximum(double value){
 if(!std::isfinite(value) || value<=0)return 1000;
 const double padded=value*1.1,magnitude=std::pow(10.0,std::floor(std::log10(padded))),ratio=padded/magnitude;
 for(double step:{1.0,2.0,2.5,5.0,10.0})if(step>=ratio)return step*magnitude;return 10*magnitude;
}
double BitrateCard::rangeMaximumBps() const {
 double maximum=0;const qint64 begin=now_-qint64(seconds_)*1000;
 for(int series=0;series<2;++series)if(active_[series]){
  if(std::isfinite(current_[series]))maximum=std::max(maximum,current_[series]);
  for(const auto &sample:history_)if(sample.timeMs>=begin && std::isfinite(sample.bps[series]))maximum=std::max(maximum,sample.bps[series]);
 }return niceMaximum(maximum);
}
void BitrateCard::paintEvent(QPaintEvent *){
 const double scale=uiScale();QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.scale(scale,scale);const QFont base=drawingFont();p.setFont(base);
 const double w=width()/scale,line=std::max(20.0,std::ceil(QFontMetricsF(base).height())+4);const int rows=std::max(1,int(active_[0])+int(active_[1]));const bool narrow=w<260;
 const QColor text=palette().color(QPalette::WindowText),muted=palette().color(QPalette::PlaceholderText);
 if(!hasActiveOutputs()){
  p.setPen(muted);
  if(compact_){const int count=int(!icons_[0].isNull())+int(!icons_[1].isNull());int iconRow=0;for(int i=0;i<2;++i){if(icons_[i].isNull())continue;QPixmap glyph=icons_[i].pixmap(16*scale*devicePixelRatioF(),16*scale*devicePixelRatioF());if(!glyph.isNull()){QPainter tint(&glyph);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(glyph.rect(),muted);}p.drawPixmap(QRectF(8,8+(40-count*20)/2+(iconRow++)*20+2,16,16),glyph,QRectF(glyph.rect()));}}
  else{p.setPen(text);p.drawText(QRectF(8,8,w-16,line),Qt::AlignLeft|Qt::AlignVCenter,"Bitrate");if(!graphs_){p.setPen(muted);p.drawText(QRectF(8,8+(narrow?line:0),w-16,line),Qt::AlignRight|Qt::AlignVCenter,"No active outputs");}}
 }
 const std::array<QColor,2> colors=appearance_==8?std::array<QColor,2>{palette().color(QPalette::Highlight),palette().color(QPalette::Window).lightness()<128?QColor("#eac98f"):QColor("#856440")}:(appearance_==7 || (appearance_==0 && palette().color(QPalette::Window).lightness()>128))?std::array<QColor,2>{QColor("#ab4963"),QColor("#247393")}:std::array<QColor,2>{QColor("#ff7c91"),QColor("#38d4e5")};
 const QStringList names={"Recording","Streaming"};int row=0;
 for(int series=0;series<2;++series){if(!active_[series])continue;
  const double iconSize=compact_?16:14,y=compact_?8+(40-rows*20)/2+row*20+2:8+row*line*(narrow?2:1)+(line-iconSize)/2;
  const int pixels=std::max(16,int(std::ceil(iconSize*scale*devicePixelRatioF())));QPixmap icon=icons_[series].pixmap(pixels,pixels);icon.setDevicePixelRatio(1);
  if(!icon.isNull()){QPainter tint(&icon);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(icon.rect(),colors[series]);}
  p.drawPixmap(QRectF(8,y,iconSize,iconSize),icon,QRectF(icon.rect()));
  if(!compact_){p.setPen(text);p.drawText(QRectF(28,8+row*line*(narrow?2:1),std::max(1.0,w-36),line),Qt::AlignLeft|Qt::AlignVCenter,names[series]);if(!property("readingsInStats").toBool()){p.setPen(colors[series]);p.drawText(QRectF(28,8+row*line*(narrow?2:1)+(narrow?line:0),std::max(1.0,w-36),line),Qt::AlignRight|Qt::AlignVCenter,rateLabel(current_[series]));}}
  ++row;
 }
 if(!graphs_)return;
 const QRectF plot=compact_?QRectF(36,8,std::max(1.0,w-44),40):QRectF(44,8+rows*line*(narrow?2:1)+6,std::max(1.0,w-52),64);
 const double maximum=rangeMaximumBps(),unit=maximum>=1e9?1e9:maximum>=1e6?1e6:maximum>=1000?1000:1;const QString unitName=unit==1e9?"Gb/s":unit==1e6?"Mb/s":unit==1000?"kb/s":"b/s";
 if(!compact_){QColor grid=muted;grid.setAlpha(60);p.setPen(QPen(grid,.7,appearance_==6?Qt::DotLine:Qt::SolidLine));for(int i=0;i<=4;++i){const double y=plot.top()+plot.height()*i/4;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));}if(appearance_!=7)for(int i=0;i<=8;++i){const double x=plot.left()+plot.width()*i/8;p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));}
  p.setPen(muted);p.drawText(QRectF(4,plot.top()-line/2,35,line),Qt::AlignRight|Qt::AlignVCenter,hasActiveOutputs()?QString::number(maximum/unit,'g',3):QString::fromUtf8("\u2014"));p.drawText(QRectF(4,plot.bottom()-line/2,35,line),Qt::AlignRight|Qt::AlignVCenter,"0");p.drawText(QRectF(plot.left(),plot.bottom()+2,plot.width()/2,line),Qt::AlignLeft|Qt::AlignVCenter,QString("−%1%2").arg(seconds_<60?seconds_:seconds_/60).arg(seconds_<60?"s":"m"));p.drawText(QRectF(plot.center().x(),plot.bottom()+2,plot.width()/2,line),Qt::AlignRight|Qt::AlignVCenter,hasActiveOutputs()?unitName:QStringLiteral("Now"));
 }
 if(!hasActiveOutputs() && !compact_){p.setPen(muted);p.drawText(plot,Qt::AlignCenter,property("emptyHint").isValid()?property("emptyHint").toString():QStringLiteral("No active outputs"));}
 const qint64 begin=now_-qint64(seconds_)*1000;p.save();p.setClipRect(plot.adjusted(-1,-1,1,1));
 for(int series=0;series<2;++series){if(!active_[series])continue;QVector<QVector<QPointF>> segments;QVector<QPointF> points;quint64 generation=0;bool haveGeneration=false;
  auto flush=[&]{if(!points.isEmpty()){segments<<points;points.clear();}};
  for(const auto &sample:history_){if(sample.timeMs<begin)continue;const double bps=sample.bps[series];if(!std::isfinite(bps)){flush();haveGeneration=false;continue;}if(haveGeneration && generation!=sample.generation[series])flush();generation=sample.generation[series];haveGeneration=true;points<<QPointF(plot.left()+double(sample.timeMs-begin)/(seconds_*1000.0)*plot.width(),plot.bottom()-bps/maximum*plot.height());}flush();
  for(const auto &segment:segments){QPainterPath path=HistoryGraphs::curve(segment,appearance_!=0 && appearance_!=6);if(appearance_==6){path=QPainterPath();path.moveTo(segment.first());for(int i=1;i<segment.size();++i){path.lineTo(segment[i].x(),segment[i-1].y());path.lineTo(segment[i]);}}
   if(appearance_==2 || appearance_==4 || appearance_==5 || appearance_==8){QPainterPath area=path;area.lineTo(segment.last().x(),plot.bottom());area.lineTo(segment.first().x(),plot.bottom());area.closeSubpath();QColor top=colors[series],bottom=top;top.setAlpha(compact_?30:65);bottom.setAlpha(0);QLinearGradient fill(plot.topLeft(),plot.bottomLeft());fill.setColorAt(0,top);fill.setColorAt(1,bottom);p.fillPath(area,fill);}
   if(appearance_==3){QColor halo=colors[series];halo.setAlpha(30);p.setPen(QPen(halo,4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(path);}
   p.setBrush(Qt::NoBrush);p.setPen(QPen(colors[series],compact_?1.5:1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(path);
  }
  if(!segments.isEmpty() && std::isfinite(current_[series])){const auto point=segments.last().last();p.setPen(Qt::NoPen);p.setBrush(colors[series]);p.drawEllipse(point,compact_?1.5:2.5,compact_?1.5:2.5);}
 }
 p.restore();
}
