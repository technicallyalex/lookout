// SPDX-License-Identifier: GPL-2.0-or-later
#include "lookout-brand.hpp"

#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace {
qreal validRatio(qreal ratio)
{
 return std::isfinite(ratio) && ratio > 0 ? std::clamp(ratio, qreal(0.5), qreal(8)) : 1.0;
}

QPixmap surface(QSize logicalSize, qreal ratio)
{
 QPixmap result(QSize(std::max(1, int(std::ceil(logicalSize.width() * ratio))),
                      std::max(1, int(std::ceil(logicalSize.height() * ratio)))));
 result.setDevicePixelRatio(ratio);
 result.fill(Qt::transparent);
 return result;
}

void paintBadge(QPainter &painter, QRectF destination, bool dark)
{
 // Render the exact reviewed SVG rather than maintaining a second drawing.
 QSvgRenderer renderer(dark ? QStringLiteral(":/lookout/brand/moon.svg") : QStringLiteral(":/lookout/brand/light.svg"));
 // Resolve subpixel edge coverage before reducing to the physical icon size.
 // This also avoids jagged diagonals on small, single-density toolbar icons.
 const QSizeF mapped=painter.deviceTransform().mapRect(destination).size();
 const QSize pixels(std::max(1,int(std::ceil(mapped.width()))),std::max(1,int(std::ceil(mapped.height()))));
 const int factor=std::max(1,std::min(3,4096/std::max(pixels.width(),pixels.height())));
 QImage artwork(pixels*factor,QImage::Format_ARGB32_Premultiplied);artwork.fill(Qt::transparent);
 {QPainter ink(&artwork);ink.setRenderHint(QPainter::Antialiasing);renderer.render(&ink,QRectF(QPointF(),artwork.size()));}
 if(factor>1)artwork=artwork.scaled(pixels,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
 painter.save();painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(destination,artwork);painter.restore();
}

}

QPixmap LookoutBrand::badge(bool dark, int logicalSize, qreal devicePixelRatio)
{
 logicalSize = std::clamp(logicalSize, 1, 4096);
 const qreal ratio = validRatio(devicePixelRatio);
 auto result = surface(QSize(logicalSize, logicalSize), ratio);
 QPainter painter(&result);
 paintBadge(painter, QRectF(0, 0, logicalSize, logicalSize), dark);
 return result;
}

QPixmap LookoutBrand::wordmark(bool dark, QSize logicalSize, qreal devicePixelRatio)
{
 logicalSize.setWidth(std::clamp(logicalSize.width(), 1, 8192));
 logicalSize.setHeight(std::clamp(logicalSize.height(), 1, 4096));
 auto result = surface(logicalSize, validRatio(devicePixelRatio));
 QPainter painter(&result);painter.setRenderHint(QPainter::Antialiasing);
 const qreal padding = std::min(logicalSize.width(), logicalSize.height()) * 0.025;
 const qreal badgeSide = std::min(logicalSize.height() - padding * 2, logicalSize.width() * 0.25);
 paintBadge(painter, QRectF(padding, (logicalSize.height() - badgeSide) / 2, badgeSide, badgeSide), dark);
 const qreal gap = badgeSide * 0.12;
 const QRectF target(padding + badgeSide + gap, logicalSize.height() * 0.18,
                     std::max(qreal(0.1), logicalSize.width() - padding * 2 - badgeSide - gap), logicalSize.height() * 0.64);
 QFont font(QStringLiteral("Georgia"));font.setStyleHint(QFont::Serif);font.setPixelSize(128);font.setWeight(QFont::Normal);
 QPainterPath lettering;lettering.addText(QPointF(0, 0), font, QStringLiteral("lookout"));
 const auto bounds = lettering.boundingRect();
 if (!bounds.isEmpty()) {
  const qreal scale = std::min(target.width() / bounds.width(), target.height() / bounds.height());
  painter.translate(target.left(), target.center().y() - bounds.height() * scale / 2);
  painter.scale(scale, scale);painter.translate(-bounds.left(), -bounds.top());
  painter.fillPath(lettering, QColor(dark ? "#fff0cf" : "#102e55"));
 }
 return result;
}

QIcon LookoutBrand::icon(bool dark, int logicalSize, qreal devicePixelRatio)
{
 return QIcon(badge(dark, logicalSize, devicePixelRatio));
}
