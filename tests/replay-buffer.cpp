// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay-buffer.hpp"
#include <QApplication>
#include <cstdio>
#include <cstdlib>
#include <cmath>
void check(bool yes,const char *message){if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(int argc,char **argv){QApplication app(argc,argv);SentinelTelemetry::ReplayTracker tracker;
 tracker.reset(10,0);tracker.append(0,1000,false);check(!tracker.sample().known,"waits for a decodable keyframe");
 for(int second=0;second<=30;++second){tracker.append(second*1000000LL,1000,second%2==0);const auto sample=tracker.sample();check(sample.known && sample.percent>=0 && sample.percent<=100,"real packet history yields bounded fill");if(second>12)check(sample.seconds<=10 && sample.seconds>=8,"rolling duration evicts complete old GOPs");}
 tracker.reset(60,5000);for(int i=0;i<20;++i)tracker.append(i*1000000LL,1000,true);auto fill=tracker.sample();check(fill.bytes<=5000 && fill.bytes>=3000 && fill.limitBytes==5000 && fill.limitSeconds==60,"memory limit governs fill before duration limit");check(fill.percent==100*double(fill.bytes)/5000,"fill respects the limiting resource");
 tracker.reset(5,0);tracker.append(10000000,600,true);tracker.append(0,200,true);check(tracker.sample().seconds==0 && tracker.sample().bytes==200,"backward timestamp resets stale history");tracker.reset();check(!tracker.sample().known && tracker.sample().bytes==0,"stop clears tracked metadata");
 ReplayCard card;card.setPresentation(2,false,60,QIcon(),1);card.setSample(true,fill,0);card.setSample(true,fill,2000);check(card.sampleCount()==2 && card.toolTip().contains("Estimated fill") && card.toolTip().contains("MiB"),"card labels the measurement estimate and limits");card.setSample(false,{},4000);check(card.toolTip().contains("stopped"),"inactive buffer retains card with stopped state");
 for(int design=0;design<9;++design)for(bool compact:{false,true}){card.setPresentation(design,compact,60,QIcon(),1);card.resize(320,card.heightForWidth(320));check(!card.grab().isNull(),"replay card renders every design and compact mode");}card.clearHistory();check(card.sampleCount()==0,"clear history removes replay graph samples");std::puts("PASS: replay keyframes, rolling time/memory limits, reset and card states");}
