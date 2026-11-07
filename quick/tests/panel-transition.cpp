// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel-transition.hpp"
#include <cstdio>
#include <cstdlib>

void check(bool condition,const char *message)
{
 if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}

int main()
{
 using Style=PanelTransition::Style;
 const QRectF screen(-1280,-300,1280,720);
 const QVector<QRectF> layout{{-240,190,200,80},{-240,279,200,82}};
 PanelTransition transition;
 for(const auto style:{Style::Instant,Style::Fade,Style::Flash,Style::Slide,Style::Blur,Style::Fall}){
  transition.begin(style,true,layout,screen,true);
  const auto arrival=transition.sample(transition.duration());
  check(arrival.cards==layout && arrival.opacity==1 && arrival.blur==0 && arrival.flash==0,
        "all styles land exactly on the stored layout");
  if(style!=Style::Instant)check(transition.sample(0).opacity==0,"show begins transparent");
  transition.begin(style,false,layout,screen,true);
  check(transition.sample(transition.duration()).opacity==0,"all styles finish hidden");
 }
 transition.begin(Style::Fade,false,layout,screen,true);
 check(transition.sample(100).cards==layout && transition.sample(100).opacity<1,"fade keeps cards still");
 transition.begin(Style::Flash,true,layout,screen,true);
 check(transition.sample(52).flash>.30 && transition.sample(125).flash==0,"flash is a single brief pulse");
 transition.begin(Style::Slide,true,layout,screen,true);
 check(transition.sample(0).cards[0].x()>layout[0].x(),"right-side slide enters from the right");
 transition.begin(Style::Slide,false,layout,screen,false);
 check(transition.sample(transition.duration()).cards[0].x()<layout[0].x(),"left-side slide exits to the left");
 transition.begin(Style::Blur,true,layout,screen,true);
 check(transition.sample(0).blur==1 && transition.sample(180).blur>0 && transition.sample(360).blur==0,
       "blur progressively resolves without moving the cards");
 transition.begin(Style::Fall,true,layout,screen,true);
 check(transition.sample(0).cards[0].y()<layout[0].y() && transition.sample(0).cards[1].y()<layout[1].y(),
       "cards fall in from above the layout");
 const auto previous=transition.sample(270);
 transition.begin(Style::Fall,false,layout,screen,true,&previous);
 check(transition.sample(0).cards==previous.cards && transition.sample(0).opacity==previous.opacity,
       "reversing starts from the current frame");
 transition.begin(Style::Fall,false,layout,screen,true);
 PanelTransition solo;
 solo.begin(Style::Fall,false,{layout.first()},screen,true);
 bool contactChangedTrajectory=false,wallBounce=false,cardBounce=false;
 for(int ms=10;ms<transition.duration();ms+=10){
  const auto frame=transition.sample(ms);
  for(const auto &card:frame.cards)
   check(screen.adjusted(-.01,-.01,.01,.01).contains(card),"falling cards stay within a negative-origin display");
  contactChangedTrajectory|=frame.cards.first()!=solo.sample(ms).cards.first();
  const auto next=transition.sample(ms+10);
  for(int i=0;i<frame.cards.size();++i)
   if(qAbs(frame.cards[i].bottom()-screen.bottom())<2 && next.cards[i].bottom()<frame.cards[i].bottom())
    wallBounce=true;
  const auto a=frame.cards[0], b=frame.cards[1];
  const auto overlap=a.intersected(b);
  check(overlap.isEmpty() || overlap.width()<2 || overlap.height()<2,"collision solver separates adjacent cards");
  if(qAbs(a.bottom()-b.top())<3 && next.cards[0].bottom()<next.cards[1].top())cardBounce=true;
 }
 check(contactChangedTrajectory && cardBounce,"card collisions change trajectories");
 check(wallBounce,"cards bounce off the display boundary");
 check(PanelTransition::parse("unknown")==Style::Instant,"unsupported values use Instant");
 std::puts("PASS: six transition styles, reversal, card collisions and display bounds");
}
