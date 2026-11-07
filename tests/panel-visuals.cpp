// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel-visuals.hpp"
#include <QApplication>
#include <cstdio>
#include <cstdlib>
#include <cmath>
void check(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
double luminance(QColor c){auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());}
double contrast(QColor a,QColor b){double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);}
int main(int argc,char **argv){QApplication app(argc,argv);
 for(int design=1;design<=8;++design)for(bool dark:{false,true}){const auto colors=LookoutVisuals::panelPalette(app.palette(),design,dark);for(auto background:{QPalette::Window,QPalette::Base})for(auto foreground:{QPalette::WindowText,QPalette::PlaceholderText}){const double value=contrast(colors.color(background),colors.color(foreground));if(value<4.5)std::fprintf(stderr,"design=%d contrast=%.2f\n",design,value);check(value>=4.5,"styled normal and secondary text remain readable across the full card gradient");}
  for(double opacity:{0.0,.25,1.0}){QImage image(160,100,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);LookoutVisuals::paintCardSurface(p,QRectF(4,4,152,92),colors,design,opacity);p.end();check(image.pixelColor(0,0).alpha()==0 && image.pixelColor(4,4).alpha()==0,"rounded surfaces do not paint square corner fragments");check(std::abs(image.pixelColor(80,50).alpha()-int(opacity*255))<=1,"opacity applies consistently without accumulating decoration alpha");}
 }
 const auto light=LookoutVisuals::panelPalette(app.palette(),8,false);check(light.color(QPalette::Window).red()>light.color(QPalette::Window).blue() && light.color(QPalette::WindowText).lightness()<40,"light lookout is warm ivory with dark ink");std::puts("PASS: all style contrast, rounded alpha, opacity and light lookout palette");}
