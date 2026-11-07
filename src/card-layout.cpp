// SPDX-License-Identifier: GPL-2.0-or-later
#include "card-layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace SentinelLayout {
namespace {
QRect usableScreen(QRect screen, QMargins insets) {
 const int left=std::clamp(insets.left(),0,std::max(0,screen.width()-1));
 const int top=std::clamp(insets.top(),0,std::max(0,screen.height()-1));
 const int right=std::clamp(insets.right(),0,std::max(0,screen.width()-left-1));
 const int bottom=std::clamp(insets.bottom(),0,std::max(0,screen.height()-top-1));
 return screen.adjusted(left,top,-right,-bottom);
}
bool nearSpan(int a,int size,int b,int otherSize,int tolerance) {
 return a<=b+otherSize+tolerance && a+size>=b-tolerance;
}
bool clearOf(QPoint point,QSize size,QRect other,int spacing) {
 return point.x()+size.width()+spacing<=other.left() ||
        point.x()>=other.right()+1+spacing ||
        point.y()+size.height()+spacing<=other.top() ||
        point.y()>=other.bottom()+1+spacing;
}
long long distanceSquared(QPoint a,QPoint b) {
 const long long x=static_cast<long long>(a.x())-b.x(), y=static_cast<long long>(a.y())-b.y();return x*x+y*y;
}
}
QPoint snapPosition(QRect moving,const QVector<QRect> &others,QRect screen,
                    int spacing,int snapDistance,bool snapCards,bool snapScreen,QMargins insets) {
 if(!moving.isValid() || !screen.isValid())return moving.topLeft();
 spacing=std::max(0,spacing);snapDistance=std::max(0,snapDistance);
 const QRect bounds=snapScreen?usableScreen(screen,insets):screen;
 const int minX=bounds.left(),minY=bounds.top();
 const int maxX=std::max(minX,bounds.right()+1-moving.width());
 const int maxY=std::max(minY,bounds.bottom()+1-moving.height());
 auto clamp=[&](QPoint p){return QPoint(std::clamp(p.x(),minX,maxX),std::clamp(p.y(),minY,maxY));};
 const QPoint original=clamp(moving.topLeft());QPoint snapped=original;
 int bestX=snapDistance+1,bestY=snapDistance+1;
 auto snapX=[&](int x){const int delta=std::abs(x-original.x());if(x>=minX && x<=maxX && delta<=snapDistance && delta<bestX){snapped.setX(x);bestX=delta;}};
 auto snapY=[&](int y){const int delta=std::abs(y-original.y());if(y>=minY && y<=maxY && delta<=snapDistance && delta<bestY){snapped.setY(y);bestY=delta;}};
 if(snapScreen){snapX(minX);snapX(maxX);snapY(minY);snapY(maxY);}
 if(!snapCards)return snapped;
 QVector<QRect> blockers;for(auto other:others)if(other.isValid())blockers<<other;
 for(auto other:blockers){
  if(nearSpan(original.y(),moving.height(),other.y(),other.height(),spacing+snapDistance)){
   snapX(other.left());snapX(other.right()+1-moving.width());
   snapX(other.right()+1+spacing);snapX(other.left()-spacing-moving.width());
  }
  if(nearSpan(original.x(),moving.width(),other.x(),other.width(),spacing+snapDistance)){
   snapY(other.top());snapY(other.bottom()+1-moving.height());
   snapY(other.bottom()+1+spacing);snapY(other.top()-spacing-moving.height());
  }
 }
 auto isClear=[&](QPoint p){for(auto other:blockers)if(!clearOf(p,moving.size(),other,spacing))return false;return true;};
 if(isClear(snapped))return snapped;
 // The nearest valid point outside a union of axis-aligned exclusion regions
 // lies on one of their edges or edge intersections. Search those candidates
 // rather than repeatedly pushing cards (which can oscillate between neighbors).
 QVector<int> xs{snapped.x(),original.x(),minX,maxX},ys{snapped.y(),original.y(),minY,maxY};
 for(auto other:blockers){
  xs<<std::clamp(other.left()-spacing-moving.width(),minX,maxX)<<std::clamp(other.right()+1+spacing,minX,maxX);
  ys<<std::clamp(other.top()-spacing-moving.height(),minY,maxY)<<std::clamp(other.bottom()+1+spacing,minY,maxY);
 }
 auto unique=[](QVector<int> &values){std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());};unique(xs);unique(ys);
 QPoint best=snapped;long long bestDistance=std::numeric_limits<long long>::max();bool found=false;
 for(int x:xs)for(int y:ys){const QPoint candidate(x,y);if(!isClear(candidate))continue;const auto distance=distanceSquared(candidate,original);if(!found || distance<bestDistance){best=candidate;bestDistance=distance;found=true;}}
 // An overfilled screen has no collision-free solution. Retain the bounded
 // requested position rather than sending a card outside the visible screen.
 return found?best:snapped;
}
double presetScale(int preset,QSize screen) {
 constexpr double factors[]={0.50,0.72,0.95,1.18};
 const double resolution=screen.height()>0?std::clamp(screen.height()/1080.0,.90,1.15):1.0;
 return resolution*factors[std::clamp(preset,0,3)];
}
int presetWidth(int preset,QSize screen){return int(std::lround(320*presetScale(preset,screen)));}
GroupedWidths groupedWidths(int screenWidth,int panelWidth,int thumbnailWidth,
                           int spacing,bool sideRequested,bool showMetrics,bool showThumbnail)
{
 const int column=std::min(screenWidth,panelWidth);
 const int metrics=showMetrics?std::max(1,column-16):0;
 if(!showThumbnail)return {column,metrics,0,false};
 const int beside=screenWidth-column-spacing-16;
 if(sideRequested && showMetrics && beside>=std::min(80,thumbnailWidth)){
  const int thumbnail=std::min(beside,thumbnailWidth);
  return {column+spacing+16+thumbnail,metrics,thumbnail,true};
 }
 const int stack=std::min(screenWidth,std::max(showMetrics?column:0,thumbnailWidth+32));
 return {stack,metrics,std::min(thumbnailWidth,std::max(1,stack-32)),false};
}
}
