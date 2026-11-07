// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay-buffer.hpp"
#include "history-graphs.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>
using namespace SentinelTelemetry;
void ReplayTracker::reset(double seconds,quint64 bytes){std::lock_guard<std::mutex> lock(mutex_);groups_.clear();bytes_=0;seconds_=std::max(0.0,seconds);limit_=bytes;}
void ReplayTracker::append(qint64 dts,quint64 bytes,bool keyframe){
 std::lock_guard<std::mutex> lock(mutex_);
 if(!groups_.empty() && dts<groups_.back().last-1000000){groups_.clear();bytes_=0;}
 if(keyframe)groups_.push_back({dts,dts,0});if(groups_.empty())return;
 groups_.back().last=std::max(dts,groups_.back().last);groups_.back().bytes+=bytes;bytes_+=bytes;
 // Trim complete GOPs, retaining decodable history and bounded metadata.
 while(groups_.size()>2 && ((seconds_>0 && dts-groups_.front().first>seconds_*1000000) || (limit_ && bytes_>limit_) || groups_.size()>1800)){bytes_-=groups_.front().bytes;groups_.pop_front();}
}
ReplayFill ReplayTracker::sample() const {std::lock_guard<std::mutex> lock(mutex_);ReplayFill result;result.limitSeconds=seconds_;result.limitBytes=limit_;result.bytes=bytes_;result.known=!groups_.empty();if(result.known)result.seconds=std::max(0.0,(groups_.back().last-groups_.front().first)/1000000.0);double ratio=seconds_>0?result.seconds/seconds_:0;if(limit_)ratio=std::max(ratio,double(bytes_)/limit_);result.percent=std::clamp(ratio*100,0.0,100.0);return result;}
ReplayCard::ReplayCard(QWidget *parent):QWidget(parent){QSizePolicy policy(QSizePolicy::Preferred,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);setAutoFillBackground(false);setAccessibleName("Replay buffer fill history");}
void ReplayCard::setSample(bool active,ReplayFill fill,qint64 time){
 if(active!=active_)history_.push_back({time,NAN});active_=active;fill_=fill;now_=time;
 const double value=active && fill.known?fill.percent:NAN;if(!history_.empty() && history_.back().time==time)history_.back().value=value;else history_.push_back({time,value});while(!history_.empty() && history_.front().time<time-900000)history_.pop_front();
 QString details=!active?"Replay buffer stopped":!fill.known?"Waiting for a keyframe":QString("Estimated fill: %1%\n%2 seconds retained%3\n%4 MiB retained%5").arg(fill.percent,0,'f',0).arg(fill.seconds,0,'f',1).arg(fill.limitSeconds>0?QString(" / %1 seconds").arg(fill.limitSeconds):QString()).arg(fill.bytes/1048576.0,0,'f',1).arg(fill.limitBytes?QString(" / %1 MiB").arg(fill.limitBytes/1048576.0,0,'f',0):QString());setToolTip(details+"\nCalculated from observed encoded packets and OBS replay limits. OBS does not expose exact buffer occupancy.");setAccessibleDescription(details);update();
}
void ReplayCard::setPresentation(int design,bool compact,int seconds,QIcon icon,double scale){const bool resized=compact_!=compact || scale_!=scale;design_=design;compact_=compact;seconds_=std::clamp(seconds,30,900);icon_=std::move(icon);scale_=std::clamp(scale,.4,4.0);if(resized)updateGeometry();update();}
void ReplayCard::clearHistory(){history_.clear();update();}
int ReplayCard::heightForWidth(int width) const {QFont f=font();if(f.pixelSize()>0)f.setPixelSize(std::max(1,int(f.pixelSize()/scale_)));else f.setPointSizeF(std::max(1.0,f.pointSizeF()/scale_));const double line=std::max(20.0,QFontMetricsF(f).height()+4);const bool narrow=width/scale_<260;return int(std::ceil((!graphs_?16+line*(narrow?2:1):compact_?48:93+line*(narrow?3:2))*scale_));}
void ReplayCard::paintEvent(QPaintEvent*){
 QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);p.scale(scale_,scale_);const double w=width()/scale_,h=height()/scale_;QFont f=font();if(f.pointSizeF()>0)f.setPointSizeF(f.pointSizeF()/scale_);else f.setPixelSize(std::max(8,int(f.pixelSize()/scale_)));p.setFont(f);
 const QColor text=palette().color(QPalette::WindowText),muted=palette().color(QPalette::PlaceholderText),accent=design_==8?palette().color(QPalette::Highlight):design_==6?QColor("#80edac"):design_==7?QColor("#486582"):design_==0?palette().color(QPalette::Highlight):QColor("#65d8dc");const double line=std::max(20.0,QFontMetricsF(f).height()+4);const bool stacked=w<260;
 const QRectF plot=compact_?QRectF(40,8,std::max(1.0,w-48),32):QRectF(40,8+line*(stacked?2:1)+5,std::max(1.0,w-48),std::max(24.0,h-20-line*(stacked?3:2)));
 p.drawPixmap(QRectF(8,compact_?h/2-10:8,20,20),icon_.pixmap(32,32),QRectF(0,0,32,32));
 if(!compact_){p.setPen(text);p.drawText(QRectF(34,8,w-42,line),Qt::AlignLeft|Qt::AlignVCenter,"Replay buffer");const QString value=!active_?"Stopped":!fill_.known?"Collecting...":QString("~%1% · %2 s").arg(fill_.percent,0,'f',0).arg(fill_.seconds,0,'f',0);if(!property("readingsInStats").toBool())p.drawText(QRectF(8,8+(stacked?line:0),w-16,line),Qt::AlignRight|Qt::AlignVCenter,value);if(!graphs_)return;p.setPen(muted);p.drawText(QRectF(8,plot.top()-line/2,28,line),Qt::AlignRight|Qt::AlignVCenter,"100");p.drawText(QRectF(8,plot.bottom()-line/2,28,line),Qt::AlignRight|Qt::AlignVCenter,"0");p.drawText(QRectF(plot.left(),plot.bottom()+2,plot.width(),line),Qt::AlignLeft|Qt::AlignVCenter,"Estimated fill");p.drawText(QRectF(plot.left(),plot.bottom()+2,plot.width(),line),Qt::AlignRight|Qt::AlignVCenter,"Now");}
 if(!graphs_)return;
 QColor grid=palette().color(QPalette::Mid);grid.setAlpha(100);p.setPen(QPen(grid,.7));for(int i=0;i<=4;++i){double y=plot.top()+i*plot.height()/4;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));}const qint64 begin=now_-seconds_*1000;QVector<QPointF> segment;
 auto draw=[&]{if(segment.isEmpty())return;const auto path=HistoryGraphs::curve(segment,design_!=6 && design_!=7);if(design_==2 || design_==4 || design_==5 || design_==8){auto area=path;area.lineTo(segment.last().x(),plot.bottom());area.lineTo(segment.first().x(),plot.bottom());area.closeSubpath();QColor tint=accent;tint.setAlpha(55);p.fillPath(area,tint);}p.setPen(QPen(accent,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawPath(path);segment.clear();};
 p.save();p.setClipRect(plot.adjusted(-1,-1,1,1));for(const auto &point:history_){if(point.time<begin)continue;if(!std::isfinite(point.value)){draw();continue;}segment<<QPointF(plot.left()+double(point.time-begin)/(seconds_*1000)*plot.width(),plot.bottom()-point.value*.01*plot.height());}draw();p.restore();if(!active_ || !fill_.known){p.setPen(muted);if(!compact_)p.drawText(plot,Qt::AlignCenter,active_?"Waiting for samples":"Replay buffer stopped");}
}
