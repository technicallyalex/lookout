// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QIcon>
#include <deque>
#include <mutex>
namespace SentinelTelemetry {
struct ReplayFill {double seconds=0,limitSeconds=0,percent=0;quint64 bytes=0,limitBytes=0;bool known=false;};
// Metadata only. No packet payloads or encoder ownership are retained.
class ReplayTracker {
public:
 void reset(double seconds=0,quint64 bytes=0);
 void append(qint64 dtsUsec,quint64 bytes,bool keyframe);
 ReplayFill sample() const;
private:
 struct Group {qint64 first,last;quint64 bytes;};
 mutable std::mutex mutex_;std::deque<Group> groups_;double seconds_=0;quint64 limit_=0,bytes_=0;
};
}
class ReplayCard final:public QWidget {
 friend class QuickPanelBridge;
public:
 explicit ReplayCard(QWidget *parent=nullptr);
 void setSample(bool active,SentinelTelemetry::ReplayFill fill,qint64 timeMs);
 void setPresentation(int design,bool compact,int seconds,QIcon icon,double scale);
 void clearHistory();
 void setGraphsEnabled(bool enabled){if(graphs_!=enabled){graphs_=enabled;updateGeometry();update();}}
 size_t sampleCount() const {return history_.size();}
 SentinelTelemetry::ReplayFill fill() const {return fill_;}
 int heightForWidth(int width) const override;
 QSize sizeHint() const override {return {320,heightForWidth(320)};}
protected:void paintEvent(QPaintEvent*) override;
private:
 struct Point {qint64 time;double value;};std::deque<Point> history_;
 SentinelTelemetry::ReplayFill fill_;bool active_=false,compact_=false;int design_=0,seconds_=60;double scale_=1;QIcon icon_;qint64 now_=0;
 bool graphs_=true;
};
