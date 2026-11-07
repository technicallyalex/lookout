// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel-visuals.hpp"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace SentinelVisuals {
QPalette panelPalette(const QPalette &native,int design,bool dark){
 design=std::clamp(design,0,8);if(!design)return native;QPalette result=native;
 const char *tops[]={"","#353747","#393344","#242232","#49414f","#284459","#102018","#f6f7fa"};
 const char *bottoms[]={"","#242132","#242132","#11101b","#292630","#172437","#102018","#e3e7ed"};
 const QColor top=design==8?QColor(dark?"#3c2134":"#faf6ee"):QColor(tops[design]);
 const QColor bottom=design==8?QColor(dark?"#211923":"#f3ece1"):QColor(bottoms[design]);
 const QColor text=design==8?QColor(dark?"#fff0cf":"#171820"):QColor(design==7?"#253040":design==6?"#b0ecc4":"#eae8f5");
 const QColor muted=design==8?QColor(dark?"#c1a7b1":"#6b5d61"):QColor(design==7?"#586270":design==6?"#78a08a":"#bcb8ca");
 result.setColor(QPalette::Window,top);result.setColor(QPalette::Base,bottom);result.setColor(QPalette::WindowText,text);result.setColor(QPalette::Text,text);result.setColor(QPalette::PlaceholderText,muted);
 result.setColor(QPalette::Mid,QColor(design==8?(dark?"#9a6679":"#b5a49b"):design==7?"#aeb9c7":design==6?"#365b47":design==2?"#a35370":design==3?"#318097":"#777086"));
 result.setColor(QPalette::Highlight,QColor(design==8?(dark?"#ff7266":"#ae3545"):design==7?"#486582":design==6?"#80edac":"#c891ed"));return result;
}
void paintCardSurface(QPainter &painter,const QRectF &card,const QPalette &palette,int design,double opacity){
 design=std::clamp(design,0,8);opacity=std::clamp(opacity,0.0,1.0);const auto colors=panelPalette(palette,design,palette.color(QPalette::Window).lightness()<128);painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
 QColor top=colors.color(QPalette::Window),bottom=design?colors.color(QPalette::Base):top;top.setAlphaF(opacity);bottom.setAlphaF(opacity);
 QLinearGradient surface(card.topLeft(),card.bottomLeft());surface.setColorAt(0,top);surface.setColorAt(1,bottom);painter.setPen(Qt::NoPen);painter.setBrush(surface);painter.drawRoundedRect(card,PanelRadius,PanelRadius);
 QColor edge=colors.color(QPalette::Mid);edge.setAlphaF(opacity*(design==8?.7:.55));painter.setPen(QPen(edge,BorderWidth));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(card.adjusted(.5,.5,-.5,-.5),PanelRadius,PanelRadius);painter.restore();
}
void roundWidget(QWidget *widget,int radius){
 if(widget->size().isEmpty())return;
 QPainterPath shape;shape.addRoundedRect(QRectF(widget->rect()),radius,radius);
 const QRegion mask(shape.toFillPolygon().toPolygon());if(widget->mask()!=mask)widget->setMask(mask);
}
void paintGlow(QPainter &painter,const QRectF &panel,const QColor &base,double pulse,double intensity,double spread){
 pulse=std::clamp(pulse,0.0,1.0);intensity=std::clamp(intensity,0.0,2.0);spread=std::clamp(spread,4.0,64.0);
 if(pulse<=0 || intensity<=0)return;
 const double reach=spread*(0.12+0.88*pulse);
 QPainterPath interior;interior.addRoundedRect(panel,PanelRadius,PanelRadius);
 QPainterPath outside;outside.addRect(panel.adjusted(-spread-2,-spread-2,spread+2,spread+2));
 painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setClipPath(outside.subtracted(interior),Qt::IntersectClip);painter.setBrush(Qt::NoBrush);
 // Linear edge fields and radial corner fields meet at the same tangent and
 // use identical falloff, avoiding both stroke banding and rectangular seams.
 const double radius=std::min({double(PanelRadius),panel.width()/2,panel.height()/2});
 const double alpha=std::clamp(0.64*intensity*pulse,0.0,1.0);
 auto at=[&](double distance){QColor color=base;color.setAlphaF(alpha*std::pow(1-distance,2));return color;};
 auto edge=[&](const QRectF &rect,const QPointF &start,const QPointF &end){QLinearGradient gradient(start,end);for(int i=0;i<=16;++i)gradient.setColorAt(i/16.0,at(i/16.0));painter.fillRect(rect,gradient);};
 edge(QRectF(panel.left()+radius,panel.top()-reach,panel.width()-2*radius,reach),QPointF(0,panel.top()),QPointF(0,panel.top()-reach));
 edge(QRectF(panel.left()+radius,panel.bottom(),panel.width()-2*radius,reach),QPointF(0,panel.bottom()),QPointF(0,panel.bottom()+reach));
 edge(QRectF(panel.left()-reach,panel.top()+radius,reach,panel.height()-2*radius),QPointF(panel.left(),0),QPointF(panel.left()-reach,0));
 edge(QRectF(panel.right(),panel.top()+radius,reach,panel.height()-2*radius),QPointF(panel.right(),0),QPointF(panel.right()+reach,0));
 auto corner=[&](const QPointF &center,const QRectF &rect){QRadialGradient gradient(center,radius+reach);gradient.setColorAt(0,at(0));for(int i=0;i<=16;++i)gradient.setColorAt((radius+reach*i/16.0)/(radius+reach),at(i/16.0));painter.fillRect(rect,gradient);};
 const double extent=radius+reach;
 corner(QPointF(panel.left()+radius,panel.top()+radius),QRectF(panel.left()-reach,panel.top()-reach,extent,extent));
 corner(QPointF(panel.right()-radius,panel.top()+radius),QRectF(panel.right()-radius,panel.top()-reach,extent,extent));
 corner(QPointF(panel.left()+radius,panel.bottom()-radius),QRectF(panel.left()-reach,panel.bottom()-radius,extent,extent));
 corner(QPointF(panel.right()-radius,panel.bottom()-radius),QRectF(panel.right()-radius,panel.bottom()-radius,extent,extent));
 painter.restore();
}
}
