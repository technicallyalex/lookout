// SPDX-License-Identifier: GPL-2.0-or-later
#include "card-layout.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
using namespace SentinelLayout;
static void check(bool condition,const char *message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 const QRect screen(0,0,1920,1080),neighbor(100,100,100,100);
 check(snapPosition(QRect(211,100,100,100),{neighbor},screen,8,12,true,false)==QPoint(208,100),"eight clear pixels on horizontal snap");
 check(snapPosition(QRect(100,211,100,100),{neighbor},screen,8,12,true,false)==QPoint(100,208),"eight clear pixels on vertical snap");
 check(snapPosition(QRect(110,110,100,100),{neighbor},screen,8,12,true,false)==QPoint(110,208),"overlap resolves to nearest spaced edge deterministically");
 check(snapPosition(QRect(211,700,100,100),{neighbor},screen,8,12,true,false)==QPoint(211,700),"distant perpendicular card does not attract");
 check(snapPosition(QRect(29,29,100,100),{},screen,8,12,false,true)==QPoint(24,24),"screen insets respected");
 check(snapPosition(QRect(29,29,100,100),{},screen,8,12,false,false)==QPoint(29,29),"screen snapping can be disabled");
 check(snapPosition(QRect(2000,-20,100,100),{},screen,8,12,false,false)==QPoint(1820,0),"off-screen cards stay accessible");
 check(snapPosition(QRect(-1940,-20,100,100),{},QRect(-1920,0,1920,1080),8,12,false,true)==QPoint(-1896,24),"negative monitor origin and insets");
 const auto trapped=snapPosition(QRect(10,10,200,200),{QRect(0,0,320,320)},QRect(0,0,320,320),8,12,true,true);
 check(trapped.x()>=24 && trapped.y()>=24 && trapped.x()+200<=296 && trapped.y()+200<=296,"overfull screen remains bounded");
 check(std::abs(presetScale(1,{1920,1080})-.72)<.001 && presetWidth(1,{1920,1080})==230,"normal scale at 1080 logical pixels");
 check(presetWidth(1,{3840,2160})==265,"resolution-proportional scale");
 check(presetScale(0,{1920,1080})<presetScale(1,{1920,1080}) && presetScale(2,{1920,1080})<presetScale(3,{1920,1080}),"preset sizes ordered");
 check(presetScale(3,{7680,4320})<1.36 && presetScale(0,{240,320})==.45,"readable and bounded scale");
 for(int preset=0;preset<4;++preset){
  const int metrics=presetWidth(preset,{1920,1080});
  const int thumbnail=metrics-16;
  const auto stack=groupedWidths(1920,metrics+16,thumbnail,24,false,true,true);
  const auto side=groupedWidths(1920,metrics+16,thumbnail,24,true,true,true);
  check(side.alongside && side.metrics==stack.metrics && side.thumbnail==stack.thumbnail,
        "all scale presets preserve both card widths when moving the thumbnail beside metrics");
  check(side.window==side.metrics+side.thumbnail+32+24,"side-by-side reserves the configured gap and padding");
 }
 const auto constrained=groupedWidths(640,480,320,24,true,true,true);
 check(constrained.alongside && constrained.window==640 && constrained.metrics==464 && constrained.thumbnail==120,
       "narrow displays shrink the preview before reducing metrics");
 const auto narrow=groupedWidths(576,480,320,24,true,true,true);
 check(!narrow.alongside && narrow.metrics==464 && narrow.thumbnail==320,"narrow displays stack cards when the preview would be too small");
 const auto videoOnly=groupedWidths(640,480,800,24,true,false,true);
 check(!videoOnly.alongside && videoOnly.window==640 && videoOnly.thumbnail==608,"thumbnail-only mode fits inside the screen");
 std::puts("PASS: snapping, collision spacing, screen bounds, and size presets");
}
