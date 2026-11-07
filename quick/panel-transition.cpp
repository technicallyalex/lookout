// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel-transition.hpp"
#include <QEasingCurve>
#include <algorithm>
#include <cmath>

namespace {
constexpr int physicsHz=120;
constexpr qreal step=1.0/physicsHz;
struct Body {
 QRectF rect;
 QPointF velocity;
 qreal inverseMass;
};

void bounce(qreal &position,qreal &velocity,qreal min,qreal max,qreal restitution)
{
 if(max<=min){position=min;velocity=0;return;}
 if(position<min){position=min;velocity=std::max(0.0,-velocity*restitution);}
 if(position>max){position=max;velocity=std::min(0.0,-velocity*restitution);}
}

void walls(Body &body,const QRectF &screen)
{
 qreal x=body.rect.x(), y=body.rect.y();
 bounce(x,body.velocity.rx(),screen.left(),screen.right()-body.rect.width(),.62);
 bounce(y,body.velocity.ry(),screen.top(),screen.bottom()-body.rect.height(),.48);
 body.rect.moveTo(x,y);
}

void collide(Body &a, Body &b)
{
 const QRectF overlap=a.rect.intersected(b.rect);
 if(overlap.isEmpty())return;
 const bool horizontal=overlap.width()<overlap.height();
 const QPointF difference=b.rect.center()-a.rect.center();
 const QPointF normal=horizontal?QPointF(difference.x()<0?-1:1,0):QPointF(0,difference.y()<0?-1:1);
 const qreal inverseMass=a.inverseMass+b.inverseMass;
 const QPointF correction=normal*((horizontal?overlap.width():overlap.height())/inverseMass);
 a.rect.translate(-correction*a.inverseMass);
 b.rect.translate(correction*b.inverseMass);
 const qreal speed=QPointF::dotProduct(b.velocity-a.velocity,normal);
 if(speed>=0)return;
 const QPointF impulse=normal*(-1.32*speed/inverseMass);
 a.velocity-=impulse*a.inverseMass;
 b.velocity+=impulse*b.inverseMass;
}

qreal mix(qreal a,qreal b,qreal t){return a+(b-a)*t;}
}

PanelTransition::Style PanelTransition::parse(const QString &name)
{
 if(name=="fade")return Style::Fade;
 if(name=="flash")return Style::Flash;
 if(name=="slide")return Style::Slide;
 if(name=="blur")return Style::Blur;
 if(name=="fall")return Style::Fall;
 return Style::Instant;
}

void PanelTransition::begin(Style style, bool showing, const QVector<QRectF> &layout,
                            const QRectF &screen, bool right, const Frame *previous)
{
 style_=style;
 showing_=showing;
 start_={layout,showing?0.0:1.0};
 end_={layout,showing?1.0:0.0};
 envelope_={};
 tumble_.clear();
 const bool resume=previous && previous->cards.size()==layout.size();
 if(resume)start_=*previous;
 switch(style){
 case Style::Instant: duration_=0;break;
 case Style::Fade: duration_=250;break;
 case Style::Flash: duration_=260;break;
 case Style::Slide:{
  duration_=330;
  const qreal shift=(right?1:-1)*std::clamp(screen.width()*.065,52.0,144.0);
  if(!showing || !resume)for(auto &rect:(showing?start_:end_).cards)rect.translate(shift,0);
  break;
 }
 case Style::Blur:
  duration_=360;
  if(showing && !resume)start_.blur=1;
  if(!showing)end_.blur=1;
  break;
 case Style::Fall:
  duration_=showing?720:950;
  if(showing && !resume){
   const qreal drop=std::clamp(screen.height()*.23,96.0,320.0);
   for(auto &rect:start_.cards)rect.translate(0,-drop);
  }
  if(!showing){
   QVector<Body> bodies;
   for(int i=0;i<start_.cards.size();++i){
    const auto &rect=start_.cards[i];
    const qreal inward=rect.center().x()>screen.center().x()?-1:1;
    bodies.append({rect,{inward*(180+i%3*95)*(i%2==0?1.0:-.5),-135.0+i%4*105},
                   1.0/std::max(1.0,rect.width()*rect.height())});
   }
   tumble_.append(start_.cards);
   for(int tick=1;tick<=duration_*physicsHz/1000+1;++tick){
    for(auto &body:bodies){
     body.velocity.ry()+=1750*step;
     body.rect.translate(body.velocity*step);
     walls(body,screen);
    }
    for(int pass=0;pass<5;++pass){
     for(int i=0;i<bodies.size();++i)
      for(int j=i+1;j<bodies.size();++j)collide(bodies[i],bodies[j]);
     for(auto &body:bodies)walls(body,screen);
    }
    QVector<QRectF> positions;
    for(const auto &body:bodies)positions.append(body.rect);
    tumble_.append(std::move(positions));
   }
   end_.cards=tumble_.last();
  }
  break;
 }
 for(const auto &rect:start_.cards)envelope_|=rect;
 for(const auto &rect:end_.cards)envelope_|=rect;
 for(const auto &positions:tumble_)
  for(const auto &rect:positions)envelope_|=rect;
}

PanelTransition::Frame PanelTransition::sample(int elapsedMs) const
{
 if(duration_<=0 || elapsedMs>=duration_)return end_;
 const qreal t=std::clamp(qreal(elapsedMs)/duration_,0.0,1.0);
 const qreal smooth=QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(t);
 qreal position=smooth, opacity=smooth;
 Frame frame=start_;
 if(style_==Style::Slide){
  position=QEasingCurve(showing_?QEasingCurve::OutCubic:QEasingCurve::InCubic).valueForProgress(t);
  opacity=showing_?std::min(1.0,t*2.2):std::clamp((t-.18)/.82,0.0,1.0);
 }
 if(style_==Style::Flash){
  opacity=showing_?std::min(1.0,t*5):std::clamp((t-.32)/.68,0.0,1.0);
  frame.flash=std::max(start_.flash*(1-t),.32*std::max(0.0,1-std::abs(t-.20)/.20));
 }else frame.flash=mix(start_.flash,end_.flash,smooth);
 if(style_==Style::Fall){
  position=QEasingCurve(QEasingCurve::OutBounce).valueForProgress(t);
  opacity=showing_?std::min(1.0,t*3.6):std::clamp((t-.50)/.50,0.0,1.0);
 }
 frame.opacity=mix(start_.opacity,end_.opacity,opacity);
 frame.blur=mix(start_.blur,end_.blur,smooth);
 for(int i=0;i<frame.cards.size();++i){
  if(tumble_.isEmpty()){
   const auto first=start_.cards[i].topLeft(), last=end_.cards[i].topLeft();
   frame.cards[i].moveTopLeft(first+(last-first)*position);
  }else{
   const qreal cursor=std::max(0,elapsedMs)*physicsHz/1000.0;
   const int index=int(cursor);
   const auto first=tumble_[index][i].topLeft(), last=tumble_[index+1][i].topLeft();
   frame.cards[i].moveTopLeft(first+(last-first)*(cursor-index));
  }
 }
 return frame;
}
