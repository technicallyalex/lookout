// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QtGlobal>
#include <optional>

namespace LookoutTelemetry {
// Measures actual output throughput from OBS's cumulative byte counter. The
// caller supplies a stable output identity and a monotonic timestamp in ms.
class BitrateSampler {
public:
 std::optional<double> sample(quintptr outputIdentity,bool active,quint64 totalBytes,
                              qint64 monotonicMs);
 void reset();
private:
 void baseline(quintptr identity,quint64 bytes,qint64 ms);
 quintptr identity_=0;
 quint64 baselineBytes_=0,lastSeenBytes_=0;
 qint64 baselineMs_=0,lastSeenMs_=0;
 bool initialized_=false;
 std::optional<double> lastRate_;
};

class RecordingSpaceEstimate {
public:
 void update(std::optional<double> bitrate);
 std::optional<double> secondsRemaining(qint64 freeBytes) const;
private:
 std::optional<double> smoothedBitrate_;
};
}
