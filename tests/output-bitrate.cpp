// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-bitrate.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace LookoutTelemetry;
static void check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool equal(std::optional<double> measured,double expected){return measured && std::abs(*measured-expected)<=std::max(1e-6,std::abs(expected)*1e-12);}
int main(){
 BitrateSampler sampler;
 check(!sampler.sample(1,true,0,0),"first active observation establishes baseline");
 check(!sampler.sample(1,true,100000,100),"quick polling before first measurement remains measuring");
 check(equal(sampler.sample(1,true,1000000,1000),8000000),"one decimal MB per second is eight Mbps");
 check(equal(sampler.sample(1,true,1100000,1100),8000000),"quick polling holds last measured rate");
 check(equal(sampler.sample(1,true,3000000,2000),16000000),"quick polling preserves the byte/time baseline");
 check(!sampler.sample(1,true,3000000,2000),"zero elapsed time is not a new rate");
 check(equal(sampler.sample(1,true,3000000,3000),0),"stalled byte counter reports valid zero throughput");
 check(!sampler.sample(2,true,9000000,4000),"new output identity clears old rate");
 check(equal(sampler.sample(2,true,10000000,5000),8000000),"new identity measures its own counter");
 check(!sampler.sample(2,true,20,6000),"counter reset clears stale rate");
 check(!sampler.sample(2,true,120,6100),"counter reset baseline remains measuring until sufficient interval");
 check(equal(sampler.sample(2,true,1020,6250),32000),"quarter-second samples scale bitrate correctly");
 check(!sampler.sample(2,true,2020,6200),"monotonic clock rollback resets without signed underflow");
 check(equal(sampler.sample(2,true,3020,6450),32000),"measurement recovers after clock rollback");
 check(!sampler.sample(2,false,3020,7000),"inactive output clears rate and baseline");
 check(!sampler.sample(2,true,999999,8000),"reactivation never averages across inactive time");
 check(equal(sampler.sample(2,true,1999999,9000),8000000),"reactivated measurement uses fresh interval");
 sampler.reset();check(!sampler.sample(2,true,500,10000),"explicit reset clears previous rate");
 check(!sampler.sample(2,true,900,10100),"quick sample records last counter observation");
 check(!sampler.sample(2,true,800,10200),"rollback within quick polls resets even above old baseline");
 check(equal(sampler.sample(2,true,1800,10450),32000),"quick-poll rollback establishes a fresh baseline");
 check(!sampler.sample(0,true,0,11000),"missing output identity is inactive");
 check(!sampler.sample(3,true,0,-1),"invalid negative timestamp resets safely");
 check(!sampler.sample(3,true,0,0),"valid clock starts after invalid timestamp");
 auto huge=sampler.sample(3,true,std::numeric_limits<quint64>::max(),250);check(huge && std::isfinite(*huge) && *huge>0,"full 64-bit byte delta cannot overflow");
 check(!sampler.sample(3,true,0,500),"counter wrap starts a fresh measurement");
 RecordingSpaceEstimate space;
 check(!space.secondsRemaining(3600000000),"disk estimate waits for a recording rate");
 space.update(8000000);
 check(equal(space.secondsRemaining(3600000000),3600),"free recording bytes and measured bitrate produce a time estimate");
 check(equal(space.secondsRemaining(0),0) && !space.secondsRemaining(-1),"full and unavailable drives remain distinct");
 space.update(16000000);
 check(equal(space.secondsRemaining(3600000000),2880),"estimate smooths changes in recording throughput");
 space.update(0);
 check(!space.secondsRemaining(3600000000),"no recorded bytes means no time estimate");
 space.update(4000000);
 check(equal(space.secondsRemaining(3600000000),7200),"resumed recording uses fresh throughput");
 space.update(std::nullopt);
 check(!space.secondsRemaining(3600000000),"stopping or pausing clears the estimate");
 std::puts("PASS: actual output bitrate, identity changes, quick polls, activity, counter resets, and clock safety");
}
