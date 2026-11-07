// SPDX-License-Identifier: GPL-2.0-or-later
#include "bitrate-card.hpp"
#include "history-graphs.hpp"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace {
void check(bool condition,const char *message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
QImage render(QWidget &widget,int width){widget.resize(width,widget.heightForWidth(width));QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);widget.render(&image);return image;}
int colored(const QImage &image,QColor color){int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const QColor pixel=image.pixelColor(x,y);count+=pixel.alpha()>220 && std::abs(pixel.red()-color.red())<3 && std::abs(pixel.green()-color.green())<3 && std::abs(pixel.blue()-color.blue())<3;}return count;}
QIcon icon(){QPixmap image(32,32);image.fill(Qt::transparent);QPainter painter(&image);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawEllipse(QRectF(2,2,28,28));return QIcon(image);}
bool within(const QPointF &point,const QVector<QRectF> &regions){for(const auto &region:regions)if(region.contains(point))return true;return false;}
void onlyIn(const QImage &image,const QVector<QRectF> &regions,const char *message){for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(image.pixelColor(x,y).alpha()>0)check(within(QPointF(x+.5,y+.5),regions),message);}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);BitrateCard bitrate;bitrate.setAttribute(Qt::WA_TranslucentBackground);bitrate.setIcons(icon(),icon());
 for(int i=0;i<16;++i)bitrate.setOutputs(true,true,80000000.0,6000000.0,i*2000);
 check(bitrate.hasActiveOutputs(),"active outputs are reported");check(bitrate.rangeMaximumBps()==100000000.0,"both rates share one truthful range");
 const auto dual=render(bitrate,360);if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT"))check(dual.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".bitrate-dual.png"),"dual bitrate visual export");const int dualHeight=dual.height();check(colored(dual,QColor("#ff7c91"))>0 && colored(dual,QColor("#38d4e5"))>0,"both active outputs render distinct series");
 bitrate.setOutputs(false,true,std::nullopt,6000000.0,32000);check(bitrate.rangeMaximumBps()==10000000.0,"inactive recording history cannot flatten the live stream range");
 const auto stream=render(bitrate,360);if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT"))check(stream.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".bitrate-stream.png"),"stream-only bitrate visual export");check(stream.height()<dualHeight,"stopped output row is removed");check(colored(stream,QColor("#ff7c91"))==0 && colored(stream,QColor("#38d4e5"))>0,"inactive recording has no visible row or graph line");
 const auto count=bitrate.sampleCount();bitrate.setOutputs(false,true,std::nullopt,6100000.0,32100);check(bitrate.sampleCount()==count,"rapid refreshes do not invent history samples");
 bitrate.setOutputs(true,true,81000000.0,6200000.0,32500);check(bitrate.samples().back().generation[0]!=bitrate.samples()[bitrate.sampleCount()-3].generation[0],"restart begins a separate recording trace generation");check(std::isnan(bitrate.samples()[bitrate.sampleCount()-2].bps[0]),"stop inserts a recording history gap even within two seconds");
 bitrate.setOutputs(true,true,-1.0,INFINITY,34000);bitrate.setOutputs(true,true,-1.0,INFINITY,36000);check(std::isnan(bitrate.samples().back().bps[0]) && std::isnan(bitrate.samples().back().bps[1]),"invalid rates remain unavailable instead of becoming zero");
 bitrate.clearHistory();for(int i=0;i<=600;++i)bitrate.setOutputs(true,true,80000000.0,6000000.0,qint64(i)*2000);check(bitrate.sampleCount()<=452 && bitrate.samples().front().timeMs>=1200000-902000,"bitrate history is bounded to fifteen minutes");
 bitrate.setOutputs(false,true,std::nullopt,6000000.0,1202000);bitrate.setCompact(true);const auto compact=render(bitrate,192);if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT"))check(compact.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".bitrate-compact.png"),"compact bitrate visual export");check(compact.height()==56 && compact.height()<stream.height(),"icon-only bitrate card has compact height");
 onlyIn(compact,{QRectF(7,19,18,18),QRectF(35,7,150,42)},"compact bitrate draws only its icon and sparkline, without labels or axes");
 bitrate.setOutputs(false,false,std::nullopt,std::nullopt,1204000);check(!bitrate.hasActiveOutputs(),"idle bitrate card accurately reports inactive outputs");const auto idle=render(bitrate,192);check(idle.height()==56 && bitrate.toolTip().contains("no active"),"idle card has compact discoverable state without invented rates");check(colored(idle,QColor("#ff7c91"))==0 && colored(idle,QColor("#38d4e5"))==0,"idle outputs leave no orphan series");
 HistoryGraphs history;history.configure(60,{true,true,true},{20,2,2});for(int i=0;i<20;++i)history.append({i*2000,{100.0-i,0.5+0.1*(i%3),0.2+0.1*(i%5)}});
 HistoryGraphs graphs;graphs.setAttribute(Qt::WA_TranslucentBackground);graphs.setHistorySource(&history);graphs.configure(60,{true,true,true},{20,2,2});graphs.setMetricIcons({icon(),icon(),icon()});graphs.setCompactMetrics({true,true,true});graphs.setProperty("metricColor1",QColor("#e23b43"));
 check(graphs.sampleCount()==history.sampleCount(),"independent graph views share collector history");const auto mini=render(graphs,192);if(!qEnvironmentVariableIsEmpty("SENTINEL_SCREENSHOT"))check(mini.save(qEnvironmentVariable("SENTINEL_SCREENSHOT")+".graphs-compact.png"),"compact stat graph visual export");check(colored(mini,QColor("#e23b43"))>0,"offending compact graph icon and trace use its alert color");
 const auto offendingCard=graphs.metricRect(1);check(colored(mini.copy(QRect(int(offendingCard.left()+7),int(offendingCard.top()+13),22,22)),QColor("#e23b43"))>0,"the offending graph glyph itself is highlighted");
 QVector<QRectF> regions;for(const auto &card:graphs.cardRects()){regions<<QRectF(card.left()+7,card.top()+13,22,22);regions<<QRectF(card.left()+39,card.top()+7,card.width()-46,34);}
 onlyIn(mini,regions,"compact metric graphs omit numerical titles, axes, and grid outside icon/trace areas");
 for(double scale:{0.65,1.0,1.5})for(int design=0;design<9;++design){graphs.setProperty("uiScale",scale);graphs.setAppearance(design);graphs.setCompactMetrics({});QFont font=app.font();font.setPointSizeF(font.pointSizeF()*scale);graphs.setFont(font);const int width=int(std::round(192*scale));graphs.resize(width,graphs.heightForWidth(width));
  check(graphs.metricRect(0).left()==0 && graphs.metricRect(1).top()>graphs.metricRect(0).bottom(),"every design defaults to one graph column");for(int metric=0;metric<3;++metric)check(graphs.metricRect(metric).bottom()<=graphs.height(),"scaled normal graph geometry fits its widget");const int normalHeight=graphs.height();graphs.setCompactMetrics({true,true,true});graphs.resize(width,graphs.heightForWidth(width));check(graphs.height()<normalHeight,"icon-only graphs remain smaller at every design/scale");for(int metric=0;metric<3;++metric)check(graphs.metricRect(metric).bottom()<=graphs.height(),"scaled compact graph geometry fits its widget");
  bitrate.setOutputs(true,true,80000000.0,6000000.0,1300000);bitrate.setProperty("uiScale",scale);bitrate.setFont(font);bitrate.setAppearance(design);bitrate.setCompact(true);const auto scaled=render(bitrate,width);check(scaled.width()==width && scaled.height()==int(std::ceil(56*scale)),"compact bitrate fits proportional card widths");
 }
 std::puts("PASS: shared bitrate scaling, active outputs, history gaps, compact icons and scaled graph geometry");
}
