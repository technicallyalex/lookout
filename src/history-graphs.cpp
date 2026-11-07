// SPDX-License-Identifier: GPL-2.0-or-later
#include "history-graphs.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>
HistoryGraphs::HistoryGraphs(QWidget *parent):QWidget(parent){QSizePolicy policy(QSizePolicy::Preferred,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);setMinimumWidth(1);}
void HistoryGraphs::append(Sample sample){
 if(source_)return;
 if(!history_.empty() && sample.timeMs-history_.back().timeMs<1900)return;
 history_.push_back(sample);
 while(history_.size()>512 || (history_.size()>1 && history_[1].timeMs<sample.timeMs-900000))history_.pop_front();
 if(isVisible())update();
}
void HistoryGraphs::clearHistory(){if(!source_)history_.clear();if(isVisible())update();}
void HistoryGraphs::configure(int seconds,const std::array<bool,Count> &selected,const std::array<double,Count> &thresholds){
 seconds=std::clamp(seconds,30,900);if(seconds==seconds_ && selected==selected_ && thresholds==thresholds_)return;
 seconds_=seconds;selected_=selected;thresholds_=thresholds;updateGeometry();if(isVisible())update();
}
void HistoryGraphs::setCompactMetrics(const std::array<bool,Count> &compact){if(compact_==compact)return;compact_=compact;setMinimumWidth(1);updateGeometry();if(isVisible())update();}
void HistoryGraphs::setMetricIcons(const std::array<QIcon,Count> &icons){metricIcons_=icons;if(isVisible())update();}
void HistoryGraphs::setAppearance(int preset){preset=std::clamp(preset,0,8);if(preset==appearance_)return;appearance_=preset;updateGeometry();if(isVisible())update();}
void HistoryGraphs::setHistorySource(const HistoryGraphs *source){
 for(auto *item=source;item;item=item->source_.data())if(item==this)return;
 if(source_==source)return;
 disconnect(sourceConnection_);source_=source;
 if(source)sourceConnection_=connect(source,&QObject::destroyed,this,[this]{source_=nullptr;update();});
 if(isVisible())update();
}
double HistoryGraphs::uiScale() const {return std::clamp(property("uiScale").isValid()?property("uiScale").toDouble():1.0,0.4,4.0);}
QFont HistoryGraphs::drawingFont() const {
 QFont result=font();const double scale=uiScale();
 if(result.pixelSize()>0)result.setPixelSize(std::max(1,int(std::round(result.pixelSize()/scale))));else result.setPointSizeF(std::max(1.0,result.pointSizeF()/scale));
 if(appearance_==6){QFont mono=QFontDatabase::systemFont(QFontDatabase::FixedFont);if(result.pixelSize()>0)mono.setPixelSize(result.pixelSize());else mono.setPointSizeF(result.pointSizeF());result=mono;}
 return result;
}
int HistoryGraphs::columns(int width) const {return property("columns").toInt()==2 && width>=360*uiScale()?2:1;}
double HistoryGraphs::cardHeight(double logicalWidth,bool compact) const {
 if(compact)return 48;
 const double line=std::max(18.0,std::ceil(QFontMetricsF(drawingFont()).height())+2);
 const bool stacked=appearance_==7 || logicalWidth<260;
 return 8+line*(stacked?2:1)+6+(appearance_==4?68:64)+line+8;
}
QVector<QRectF> HistoryGraphs::rectangles(int width) const {
 const double gap=std::clamp(property("cardSpacing").isValid()?property("cardSpacing").toDouble():8.0,0.0,128.0);
 const int cols=columns(width);
 const double cw=std::max(1.0,(width-gap*(cols-1))/cols);
 QVector<QRectF> result;
 double y=0,rowHeight=0;
 for(int i=0;i<Count;++i)if(selected_[i]){
  const int column=result.size()%cols;
  const double height=std::ceil(cardHeight(cw/uiScale(),compact(i))*uiScale());
  result<<QRectF(column*(cw+gap),y,cw,height);
  rowHeight=std::max(rowHeight,height);
  if(column==cols-1){y+=rowHeight+gap;rowHeight=0;}
 }
 return result;
}
int HistoryGraphs::heightForWidth(int width) const {
 double bottom=0;for(const auto &card:rectangles(width))bottom=std::max(bottom,card.bottom());
 return int(std::ceil(bottom));
}
QVector<QRectF> HistoryGraphs::cardRects() const {return rectangles(width());}
QRectF HistoryGraphs::metricRect(int metric) const {
 if(metric<0 || metric>=Count || !selected_[metric])return {};
 int index=0;for(int i=0;i<metric;++i)index+=selected_[i];return rectangles(width())[index];
}
QPainterPath HistoryGraphs::curve(const QVector<QPointF> &points,bool smooth){
 QPainterPath path;if(points.isEmpty())return path;path.moveTo(points.first());
 if(!smooth || points.size()<3){for(int i=1;i<points.size();++i)path.lineTo(points[i]);return path;}
 QVector<double> slopes(points.size()-1),tangents(points.size());
 for(int i=0;i<slopes.size();++i)slopes[i]=(points[i+1].y()-points[i].y())/(points[i+1].x()-points[i].x());
 tangents[0]=slopes.first();tangents.last()=slopes.last();
 for(int i=1;i<points.size()-1;++i)tangents[i]=slopes[i-1]*slopes[i]<=0?0:2/(1/slopes[i-1]+1/slopes[i]);
 for(int i=0;i<slopes.size();++i){if(std::abs(slopes[i])<1e-12){tangents[i]=tangents[i+1]=0;continue;}const double a=tangents[i]/slopes[i],b=tangents[i+1]/slopes[i],length=std::hypot(a,b);if(length>3){tangents[i]=3*a*slopes[i]/length;tangents[i+1]=3*b*slopes[i]/length;}}
 for(int i=0;i<points.size()-1;++i){const double third=(points[i+1].x()-points[i].x())/3;path.cubicTo(points[i]+QPointF(third,tangents[i]*third),points[i+1]-QPointF(third,tangents[i+1]*third),points[i+1]);}
 return path;
}
void HistoryGraphs::paintEvent(QPaintEvent *){
 const double scale=uiScale();QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.scale(scale,scale);
 const QFont base=drawingFont();p.setFont(base);const double lineHeight=std::max(18.0,std::ceil(QFontMetricsF(base).height())+2);
 const bool styled=appearance_!=0;
 const QColor text=palette().color(QPalette::WindowText),muted=palette().color(QPalette::PlaceholderText),grid=palette().color(QPalette::Mid);

 const auto &history=samples();const qint64 now=history.empty()?0:history.back().timeMs,begin=now-qint64(seconds_)*1000;
 const QColor neonStart[]={QColor("#13d8eb"),QColor("#ffb069"),QColor("#9e5bff")},neonEnd[]={QColor("#50e3a4"),QColor("#ff4b91"),QColor("#f43cdc")};
 bool any=false;
 for(int metric=0;metric<Count;++metric){if(!selected_[metric])continue;any=true;const bool mini=compact(metric);
  const QRectF physical=metricRect(metric);QRectF card(physical.x()/scale,physical.y()/scale,physical.width()/scale,physical.height()/scale),cell=card.adjusted(8,8,-8,-8);
  const bool stacked=appearance_==7 || card.width()<260;const double headerHeight=lineHeight*(stacked?2:1);
  const double current=history.empty()?NAN:history.back().values[metric];
  const QColor alertColor=property(QString("metricColor%1").arg(metric).toUtf8().constData()).value<QColor>();const QColor headerColor=alertColor.isValid()?alertColor:text;
  if(mini){
   const int pixels=std::max(20,int(std::ceil(20*scale*devicePixelRatioF())));QPixmap icon=metricIcons_[metric].pixmap(pixels,pixels);icon.setDevicePixelRatio(1);
   if(!icon.isNull() && alertColor.isValid()){QPainter tint(&icon);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(icon.rect(),alertColor);}
   p.drawPixmap(QRectF(cell.left(),card.center().y()-10,20,20),icon,QRectF(icon.rect()));
  }else{
  QFont title=base;title.setBold(appearance_!=6);if(appearance_==7)title.setLetterSpacing(QFont::AbsoluteSpacing,0.8);
  p.setFont(title);p.setPen(headerColor);
  const QString label=appearance_==6?"> "+QString::fromUtf8(metrics[metric].label):appearance_==7?QString::fromUtf8(metrics[metric].label).toUpper():QString::fromUtf8(metrics[metric].label);
  p.drawText(QRectF(cell.left(),cell.top(),cell.width(),lineHeight),Qt::AlignLeft|Qt::AlignVCenter,label);
  p.setFont(base);
  if(appearance_==4){QColor highlight("#e8d8ff");highlight.setAlpha(42);p.setPen(QPen(highlight,1));p.drawLine(QPointF(cell.left()+39,cell.top()+headerHeight+2),QPointF(cell.right(),cell.top()+headerHeight+2));}
  if(appearance_==5){QLinearGradient ribbon(cell.topLeft(),cell.topRight());ribbon.setColorAt(0,QColor("#51d9ed"));ribbon.setColorAt(1,QColor("#b795ff"));p.setPen(QPen(QBrush(ribbon),2));p.drawLine(QPointF(cell.left()+39,cell.top()+headerHeight+2),QPointF(cell.right(),cell.top()+headerHeight+2));}
  if(appearance_==7){p.setPen(QPen(headerColor,2));p.drawLine(QPointF(card.left()+1,cell.top()),QPointF(card.left()+1,cell.top()+headerHeight));}
  }
  double minimum=INFINITY,maximum=-INFINITY;for(const auto &sample:history)if(sample.timeMs>=begin && std::isfinite(sample.values[metric])){minimum=std::min(minimum,sample.values[metric]);maximum=std::max(maximum,sample.values[metric]);}
  const bool data=std::isfinite(minimum);if(!data){minimum=0;maximum=metric==0?1:std::max(1.0,thresholds_[metric]*1.25);}
  else if(metric==0){const double padding=std::max(1.0,(maximum-minimum)*0.15);minimum=std::max(0.0,minimum-padding);maximum+=padding;}
  else{minimum=0;maximum=std::max({1.0,thresholds_[metric]*1.25,maximum*1.15});}
  if(metric==3){minimum=0;maximum=100;}
  const QRectF plot=mini?QRectF(cell.left()+32,cell.top(),std::max(1.0,cell.width()-32),32):QRectF(cell.left()+39,cell.top()+headerHeight+6,std::max(1.0,cell.width()-43),appearance_==4?68:64);
  if(!mini && appearance_==4){p.setPen(QPen(QColor(0,0,0,90),2));p.setBrush(QColor(0,0,0,22));p.drawRoundedRect(plot.adjusted(-2,-2,2,2),5,5);}
  if(!mini){
  QColor gridColor=grid;gridColor.setAlpha(styled?145:110);p.setPen(QPen(gridColor,0.7,appearance_==6?Qt::DotLine:Qt::SolidLine));
  const int divisions=appearance_==7?2:4;
  if(appearance_!=7)for(int i=0;i<=8;++i){double x=plot.left()+i*plot.width()/8;p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));}
  for(int i=0;i<=divisions;++i){double y=plot.top()+i*plot.height()/divisions;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));}
  p.setPen(muted);p.drawText(QRectF(cell.left(),plot.top()-lineHeight/2,35,lineHeight),Qt::AlignRight|Qt::AlignVCenter,QString::number(maximum,'f',metric==0?1:0));p.drawText(QRectF(cell.left(),plot.bottom()-lineHeight/2,35,lineHeight),Qt::AlignRight|Qt::AlignVCenter,QString::number(minimum,'f',metric==0?1:0));
  const QString duration=seconds_<60?QStringLiteral("−%1s").arg(seconds_):QStringLiteral("−%1m").arg(seconds_/60);
  p.drawText(QRectF(plot.left(),plot.bottom()+2,70,lineHeight),Qt::AlignLeft|Qt::AlignVCenter,duration);p.drawText(QRectF(plot.right()-40,plot.bottom()+2,40,lineHeight),Qt::AlignRight|Qt::AlignVCenter,"Now");
  }
  p.save();p.setClipRect(plot.adjusted(-1,-1,1,1));auto y=[&](double value){return plot.bottom()-(value-minimum)/(maximum-minimum)*plot.height();};
  if(thresholds_[metric]>=minimum && thresholds_[metric]<=maximum){QColor threshold=appearance_==7?QColor("#687181"):styled?QColor("#c8bfd8"):palette().color(QPalette::Link);threshold.setAlpha(160);p.setPen(QPen(threshold,1,Qt::DashLine));p.drawLine(QPointF(plot.left(),y(thresholds_[metric])),QPointF(plot.right(),y(thresholds_[metric])));}
  QColor first=neonStart[metric%3],last=neonEnd[metric%3];
  if(appearance_==2){first=QColor(metric==0?"#ffd047":metric==1?"#ff7ea6":"#f655ff");last=QColor(metric==0?"#ff4e93":metric==1?"#f23473":"#ff4380");}
  if(appearance_==3){first=QColor(metric==0?"#00d5ff":metric==1?"#a42bff":"#ffbb16");last=QColor(metric==0?"#00a7ff":metric==1?"#ff35ba":"#04d7f2");}
  if(appearance_==4){first=QColor("#d6a4ff");last=QColor("#8d66e7");}
  if(appearance_==5){first=QColor("#78eced");last=QColor("#bca2ff");}
  if(appearance_==6)first=last=QColor("#80edac");
  if(appearance_==7)first=last=QColor("#486582");
  if(appearance_==8){first=palette().color(QPalette::Highlight);last=palette().color(QPalette::WindowText);}
  if(!styled)first=last=palette().color(QPalette::Highlight);
  if(mini && alertColor.isValid())first=last=alertColor;
  QLinearGradient stroke(plot.topLeft(),plot.topRight());stroke.setColorAt(0,first);stroke.setColorAt(1,last);
  QVector<QVector<QPointF>> segments;QVector<QPointF> points;
  for(const auto &sample:history){if(sample.timeMs<begin)continue;const double value=sample.values[metric];if(!std::isfinite(value)){if(!points.isEmpty()){segments<<points;points.clear();}continue;}points<<QPointF(plot.left()+double(sample.timeMs-begin)/(seconds_*1000.0)*plot.width(),y(value));}
  if(!points.isEmpty())segments<<points;
  for(const auto &segment:segments){QPainterPath line=curve(segment,styled && appearance_!=6);
   if(appearance_==6){line=QPainterPath();line.moveTo(segment.first());for(int i=1;i<segment.size();++i){line.lineTo(segment[i].x(),segment[i-1].y());line.lineTo(segment[i]);}}
   if((appearance_==2 || appearance_==4 || appearance_==5 || appearance_==8) && segment.size()>1){QPainterPath area=line;area.lineTo(segment.last().x(),plot.bottom());area.lineTo(segment.first().x(),plot.bottom());area.closeSubpath();QLinearGradient fill(plot.topLeft(),plot.bottomLeft());QColor top=last,bottom=last;top.setAlpha(appearance_==2?170:appearance_==4?60:95);bottom.setAlpha(appearance_==2?18:0);fill.setColorAt(0,top);fill.setColorAt(1,bottom);p.fillPath(area,fill);}
   if(appearance_==3 || appearance_==4){QColor halo=last;halo.setAlpha(appearance_==3?35:25);p.setPen(QPen(halo,appearance_==3?5:4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawPath(line);}
   p.setPen(QPen(QBrush(stroke),appearance_==2?2.8:appearance_==7?1.8:1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawPath(line);
  }
  if(!mini && appearance_==2)for(const auto &segment:segments){const auto point=segment[segment.size()/2];p.setPen(QPen(QColor("#fff0fc"),.8));p.setBrush(Qt::NoBrush);p.drawEllipse(point,4,4);p.drawEllipse(point,2,2);}
  if(!segments.isEmpty() && std::isfinite(current)){const auto endpoint=segments.last().last();p.setPen(QPen(appearance_==7?QColor("#253040"):styled?QColor("#fff0fc"):text,1));p.setBrush(appearance_==3?QColor("#ffffff"):last);if(appearance_==6)p.drawRect(QRectF(endpoint-QPointF(2,2),QSizeF(4,4)));else p.drawEllipse(endpoint,appearance_==3?3.5:2.5,appearance_==3?3.5:2.5);}
  p.restore();if(!data && !mini){p.setPen(muted);p.drawText(plot,Qt::AlignCenter,"Waiting for samples");}
 }
 if(!any){p.setPen(muted);p.drawText(QRectF(8,8,std::max(1.0,width()/scale-16),cardHeight(width()/scale,false)-16),Qt::AlignCenter|Qt::TextWordWrap,"Select a metric in Settings > Appearance > Layout");}
}
