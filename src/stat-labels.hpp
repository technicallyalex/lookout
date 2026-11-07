// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace LookoutStats {
struct Label {
 const char *full;
 const char *compact = full;
};

inline constexpr Label recording{"Recording"};
inline constexpr Label storage{"Disk space available", "Disk space"};
inline constexpr Label streaming{"Stream"};
inline constexpr Label encoder{"Skipped frames due to encoding lag", "Skipped frames"};
inline constexpr Label camera{"Virtual Camera"};
inline constexpr Label network{"Dropped Frames (Network)", "Dropped frames"};
inline constexpr Label cpu{"CPU Usage"};
inline constexpr Label memory{"Memory Usage"};
inline constexpr Label diskTime{"Disk full in (approx.)", "Disk full in"};
inline constexpr Label bitrate{"Bitrate"};
inline constexpr Label replay{"Replay Buffer"};
}
