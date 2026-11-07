// SPDX-License-Identifier: GPL-2.0-or-later
#include "status-icons.hpp"
#include <QApplication>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace {
void check(bool condition,const char *message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
QIcon glyph(){QPixmap image(32,32);image.fill(Qt::transparent);QPainter painter(&image);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawRoundedRect(QRectF(2,2,28,28),5,5);return QIcon(image);}
QImage render(StatusIconsCard &card,int width){card.resize(width,std::max(1,card.heightForWidth(width)));QImage image(card.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);card.render(&image,QPoint(),QRegion(),QWidget::DrawChildren);return image;}
int pixels(const QImage &image,QColor color){int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto pixel=image.pixelColor(x,y);count+=pixel.alpha()>220 && std::abs(pixel.red()-color.red())<3 && std::abs(pixel.green()-color.green())<3 && std::abs(pixel.blue()-color.blue())<3;}return count;}
void blank(const QImage &image,const char *message){for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)check(image.pixelColor(x,y).alpha()==0,message);}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);StatusIconsCard card;QPalette dark=card.palette();dark.setColor(QPalette::Window,QColor("#20242a"));dark.setColor(QPalette::WindowText,Qt::white);card.setPalette(dark);card.setIcons(glyph(),glyph());
 card.setState(false,false,{});check(card.iconCount()==0 && card.roles().isEmpty() && card.sizeHint()==QSize(0,0) && card.heightForWidth(80)==0,"idle icon-only mode has no status icons or layout height");blank(render(card,80),"empty mode paints no text, icons or background");
 card.setState(true,false,{});const auto rec=render(card,72);check(card.roles()==QStringList{"recording"} && card.iconCount()==1,"recording-only state has just its active output icon");check(pixels(rec,QColor("#ff4b55"))>0 && pixels(rec,QColor("#40baf0"))==0,"inactive stream glyph is not rendered");
 card.setState(false,true,{});const auto stream=render(card,72);check(card.roles()==QStringList{"streaming"} && pixels(stream,QColor("#ff4b55"))==0 && pixels(stream,QColor("#40baf0"))>0,"streaming-only state omits recording");
 card.setState(true,true,{});check(card.iconCount()==2 && card.sizeHint()==QSize(72,40) && card.heightForWidth(72)==40,"both active output icons fit a compact horizontal strip");
 const StatusIconsCard::AlertIcon storage{"storage",glyph(),QColor("#ffd43b"),"Storage running low"};card.setState(false,false,{storage});card.setPulse(1);const auto alert=render(card,40);check(card.roles()==QStringList{"alert:storage"} && pixels(alert,storage.color)>0,"an alert can appear without any active output");check(card.toolTip().contains("Storage running low"),"icon meaning is available through its tooltip");
 const StatusIconsCard::AlertIcon encoder{"encoder",glyph(),QColor("#ff3b4d"),"Encoder skips"},network{"network",glyph(),QColor("#ff3b4d"),"Network interruption"};card.setState(true,true,{storage,encoder,network});check(card.iconCount()==5 && card.roles().contains("alert:network") && card.heightForWidth(72)==104,"multiple alerts wrap into rows without adding unrelated status icons");const auto wrapped=render(card,72);check(wrapped.height()==104 && pixels(wrapped,storage.color)>0,"wrapped alert glyphs remain visible");
 StatusIconsCard::AlertIcon critical=storage;critical.color=QColor("#ff3b4d");critical.label="Storage full";card.setState(false,false,{storage,critical,{"",glyph(),Qt::red,"Invalid"}});check(card.iconCount()==1 && card.roles()==QStringList{"alert:storage"} && card.toolTip()=="Storage full","duplicate alert identities update one glyph; unnamed alerts do not create empty slots");
 card.setPulse(0);const auto small=render(card,40);card.setPulse(1);const auto full=render(card,40);check(pixels(full,critical.color)>pixels(small,critical.color),"new alert glyphs can gently pop into place");card.setPulse(0);check(pixels(render(card,40),critical.color)==pixels(full,critical.color),"a completed pop does not repeat for an unchanged alert");
 card.setState(true,false,{});check(card.roles()==QStringList{"recording"} && pixels(render(card,40),critical.color)==0,"cleared alerts disappear while active output icons remain");card.setState(false,false,{});blank(render(card,40),"clearing all states leaves no residual icon or chart");
 for(double scale:{.65,1.0,1.5,2.0}){card.setScale(scale);card.setState(true,true,{storage,encoder,network});const int width=int(std::ceil(72*scale));const auto image=render(card,width);check(image.height()==card.heightForWidth(width) && image.height()==int(std::ceil(104*scale)),"wrapped icon-only cards scale and size consistently");}
 std::puts("PASS: icon-only active outputs, relevant alerts, clearing, pulse, wrapping and scale");
}
