// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QQuickItem>
#include <QTimer>
#include <QWinEventNotifier>
class SharedPreview:public QQuickItem {
 Q_OBJECT
 Q_PROPERTY(QVariantMap descriptor READ descriptor WRITE setDescriptor NOTIFY descriptorChanged)
public:
 SharedPreview();
 ~SharedPreview() override;
 QVariantMap descriptor() const {return descriptor_;}
 void setDescriptor(const QVariantMap &value);
signals:
 void descriptorChanged();
 void frameCopied();
 void importFailed(const QString &message);
protected:
 QSGNode *updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
 void requestFrame();
 void closeFrameEvent();
 QVariantMap descriptor_;
 QWinEventNotifier frameNotifier_;
 QTimer fallbackTimer_;
 bool framePending_=false;
};
