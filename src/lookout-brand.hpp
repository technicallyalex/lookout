// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QIcon>
#include <QPixmap>
#include <QSize>

namespace LookoutBrand {
// Logical dimensions; the returned pixmap carries the requested device-pixel ratio.
QPixmap badge(bool dark, int logicalSize, qreal devicePixelRatio = 1.0);
QPixmap wordmark(bool dark, QSize logicalSize, qreal devicePixelRatio = 1.0);
QIcon icon(bool dark, int logicalSize = 64, qreal devicePixelRatio = 1.0);
}
