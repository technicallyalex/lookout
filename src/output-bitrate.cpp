// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-bitrate.hpp"
#include <cmath>

namespace SentinelTelemetry {
void BitrateSampler::reset(){
 identity_=0;baselineBytes_=lastSeenBytes_=0;baselineMs_=lastSeenMs_=0;
 initialized_=false;lastRate_.reset();
}
void BitrateSampler::baseline(quintptr identity,quint64 bytes,qint64 ms){
 identity_=identity;baselineBytes_=lastSeenBytes_=bytes;baselineMs_=lastSeenMs_=ms;
 initialized_=true;lastRate_.reset();
}
std::optional<double> BitrateSampler::sample(quintptr identity,bool active,quint64 bytes,qint64 ms){
 if(!active || !identity || ms<0){reset();return std::nullopt;}
 if(!initialized_ || identity_!=identity){baseline(identity,bytes,ms);return std::nullopt;}
 // Inspect every observation, including quick polls, so a reset cannot hide
 // between two samples while the measurement baseline remains unchanged.
 if(ms<lastSeenMs_ || bytes<lastSeenBytes_){baseline(identity,bytes,ms);return std::nullopt;}
 const bool sameTime=ms==lastSeenMs_;lastSeenBytes_=bytes;lastSeenMs_=ms;
 if(sameTime)return std::nullopt;
 const qint64 elapsed=ms-baselineMs_;
 if(elapsed<250)return lastRate_;
 const quint64 delta=bytes-baselineBytes_;
 // Convert before multiplication: even a full 64-bit counter delta cannot
 // overflow integer arithmetic, and zero byte growth is a valid zero bitrate.
 lastRate_=double(delta)*8000.0/double(elapsed);
 baselineBytes_=bytes;baselineMs_=ms;
 return lastRate_;
}

void RecordingSpaceEstimate::update(std::optional<double> bitrate){
 if(!bitrate || !std::isfinite(*bitrate) || *bitrate<=0){
  smoothedBitrate_.reset();
  return;
 }
 const double previous=smoothedBitrate_.value_or(*bitrate);
 smoothedBitrate_=previous+(*bitrate-previous)*.25;
}

std::optional<double> RecordingSpaceEstimate::secondsRemaining(qint64 freeBytes) const {
 if(freeBytes<0 || !smoothedBitrate_)return std::nullopt;
 return double(freeBytes)*8 / *smoothedBitrate_;
}
}
