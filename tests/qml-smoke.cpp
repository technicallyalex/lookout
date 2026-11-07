// SPDX-License-Identifier: GPL-2.0-or-later
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>

class FixtureGlyphs final : public QQuickImageProvider {
public:
 FixtureGlyphs() : QQuickImageProvider(Image) {}

 QImage requestImage(const QString &, QSize *size, const QSize &requested) override
 {
  QImage image(requested.isValid() ? requested : QSize(32, 32), QImage::Format_ARGB32);
  image.fill(Qt::white);
  if (size)
   *size = image.size();
  return image;
 }
};

class SmokeSetup final : public QObject {
 Q_OBJECT
public slots:
 void applicationAvailable() { QQuickStyle::setStyle("Fusion"); }

 void qmlEngineAvailable(QQmlEngine *engine)
 {
  // The desktop test checks the real glyph provider and Windows rendering.
  engine->addImageProvider("glyph", new FixtureGlyphs);
 }
};

QUICK_TEST_MAIN_WITH_SETUP(lookout_qml, SmokeSetup)

#include "qml-smoke.moc"
