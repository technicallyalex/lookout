// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QSettings>
#include <QTimer>
#include <QLabel>
#include <QPointer>
#include <QThreadPool>
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <mutex>
#include <QMap>
#include <QBoxLayout>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QColor>
#include "history-graphs.hpp"
#include "output-bitrate.hpp"
#include "bitrate-card.hpp"
#include "status-icons.hpp"
#include "replay-buffer.hpp"
#include <QVariantAnimation>
class QuickPanelBridge;
class QPushButton;
struct os_cpu_usage_info;

class SentinelDock final : public QWidget {
public:
 explicit SentinelDock(const QString &settings);
 ~SentinelDock() override;
 void openSettings();
 void enableQuickRenderer(const QString &executable);
 QSize sizeHint() const override;
protected:
 void resizeEvent(QResizeEvent *) override;
private:
 friend struct SentinelTestAccess;
 friend class QuickPanelBridge;
 QuickPanelBridge *quick_=nullptr;
 bool quickVisible_=false;
 enum DockAction { DockShowPanels, DockHidePanels, DockDisablePanels, DockThumbnail, DockMetrics,
                   DockCapture, DockCorners, DockPresets, DockDismissAlerts, DockResetPosition,
                   DockSettings, DockDisplayMode };
 struct DockButton { const char *id,*label,*icon; DockAction action; bool defaultVisible; };
 static constexpr DockButton dockButtonOptions_[] = {
  {"showPanels","Show panels","show",DockShowPanels,true},
  {"hidePanels","Hide panels","hide",DockHidePanels,true},
  {"cycleDefaultPositions","Cycle anchor corners","layout",DockCorners,true},
  {"cyclePositionPresets","Cycle saved positions","replay",DockPresets,true},
  {"displayMode","Display mode","stats",DockDisplayMode,true},
  {"lookoutSettings","Settings","settings",DockSettings,true},
  {"toggleThumbnail","Toggle thumbnail","thumbnail",DockThumbnail,false},
  {"toggleMetrics","Toggle Stats","graphs",DockMetrics,false},
  {"toggleCaptureVisibility","Toggle visibility in recordings and streams","capture",DockCapture,false},
  {"dismissAlerts","Dismiss alerts","dismiss-alerts",DockDismissAlerts,false},
  {"resetPanelPosition","Reset panel position","reset-position",DockResetPosition,false},
  {"disablePanels","Disable panels and alerts","power",DockDisablePanels,false}
 };
 QMap<QString,QPushButton*> dockButtons_;
 QStringList dockOrder_;
 static QStringList defaultDockOrder();
 void applyDockOrder(const QStringList &order);
 void editDockOrder(const QString &id,const QString &operation);
 void layoutDockActions();
 static void event(obs_frontend_event, void *);
 static void hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool);
 void tick();
 void notify(const QString &, const QString &, bool critical = false);
 void place(QWidget *window, const QString &prefix);
 bool showProtected(QWidget *window);
 QString overlayBlockReason() const;
 void saveKeys();
 void refreshPanel();
 void updateIcons();
 bool darkLogo() const;
 void updateBranding(bool force=false);
 bool brandingInitialized_=false,brandingDark_=false;
 qreal brandingDpr_=0,brandingDialogDpr_=0;
 bool iconsInitialized_=false, iconsDark_=false;
 void sampleNetwork(int total, int dropped, bool reconnecting, bool sameOutput, bool active);
 void dismissAlert();
 void updateStats();
 void updateStatusIcons();
 bool metricEnabled(const QString &id) const;
 bool graphsEnabled() const;
 StatusIconsCard *activeIcons_;
 QVariantAnimation *iconPop_;
 QString iconFingerprint_;
 void applyPanelAppearance();
 WId backdropHandle_=0;
 void configureGraphs();
 HistoryGraphs *graphs_;
 BitrateCard *bitrates_;
 ReplayCard *replay_;
 SentinelTelemetry::ReplayTracker replayTracker_;
 obs_output_t *replayOutput_=nullptr;
 bool replayActive_=false;
 void updateReplay();
 void detachReplay();
 static void replayPacket(obs_output_t*,encoder_packet*,encoder_packet_time*,void*);
 SentinelTelemetry::BitrateSampler recordingSampler_,streamingSampler_;
 std::optional<double> recordingBitrate_,streamingBitrate_;
 void updateBitrateCard();
 bool thumbnailEnabled() const;
 bool thumbnailDisplayed() const;
 HistoryGraphs *graphViews_[HistoryGraphs::Count]{};
 bool independentLayout_=false;
 QString dragCard_;
 QRect dragCardStart_;
 QMap<QString,QRect> cardGeometry_;
 void setIndependentLayout(bool enabled);
 void placeIndependent();
 QMap<QString,QWidget*> movableCards() const;
 double panelScale() const;
 int displayMode() const;
 struct DisplayModeOption { int id; const char *label; };
 static constexpr DisplayModeOption displayModes_[] = {
  {5,"Icons"}, {4,"Active icons"}, {0,"Custom"}
 };
 enum GraphMode { GraphOff, GraphMini, GraphFull };
 static constexpr const char *outputGraphIds_[] = {"bitrate","replay"};
 GraphMode graphMode(const QString &id) const;
 bool hasReading(int stat) const;
 std::optional<double> diskMinutesRemaining() const;
 void setDisplayMode(int mode);
 bool iconStripMode() const;
 void applyScalePreset(int preset);
 void applyCustomSize();
 void rememberCustomSize();
 QVector<QRectF> glowTargets() const;
 QMap<QString,QRectF> visibleCardRects() const;
 void presentPanel();
 void hidePanel();
 QElapsedTimer historyClock_;
 bool encoderSampleValid_=false,networkSampleValid_=false;
 void updateGlow();
 int glowPadding() const;
 int overlayPadding() const;
 double glowPeriod(double risk) const;
 void previewGlow(double risk);
 void previewAlert(int type);
 void previewThumbnailAlert();
 QString previewAlertTitle_,previewAlertBody_;
 void applyMetricColors(double pulse = -1);
 int glowPreviewMetric_=-1;
 double glowPreviewRisk_=0;
 double metricRisk(int index) const;
 double glowRisk() const;
 QColor warningColor(double risk) const;
 QWidget *glow_;
 QTimer glowTimer_;
 QElapsedTimer glowClock_;
 qint64 glowPreviewUntil_=0, glowLastFrame_=0;
 double glowPhase_=0, encoderPercent_=0, networkPercent_=0;
 void layoutStats(int width);
 void setEditMode(bool enabled);
 void saveTransform();
 void defaultPosition();
 void syncGeometryControls();
 void savePreset(const QString &name);
 bool loadPreset(const QString &name);
 void removePreset(const QString &name);
 bool eventFilter(QObject *,QEvent *) override;
 QWidget *stats_;
 QGridLayout *statsLayout_;
 enum Stat { RecordingStat, StorageStat, StreamingStat, EncoderStat, CameraStat, NetworkStat,
             CpuStat, MemoryStat, DiskTimeStat, BitrateStat, ReplayStat, StatCount };
 struct StatDefinition { const char *id; LookoutStats::Label label; const char *icon; };
 static constexpr StatDefinition statDefinitions_[StatCount] = {
  {"recording",LookoutStats::recording,"recording"}, {"storage",LookoutStats::storage,"storage"},
  {"streaming",LookoutStats::streaming,"streaming"}, {"encoder",LookoutStats::encoder,"encoder"},
  {"camera",LookoutStats::camera,"virtual-camera"}, {"network",LookoutStats::network,"network"},
  {"cpu",LookoutStats::cpu,"cpu"}, {"memory",LookoutStats::memory,"memory"}, {"disk-time",LookoutStats::diskTime,"disk-time"},
  {"bitrate",LookoutStats::bitrate,"graphs"}, {"replay",LookoutStats::replay,"replay"}
 };
 QLabel *statIcons_[StatCount], *statNames_[StatCount], *statValues_[StatCount];
 os_cpu_usage_info *cpuSampler_=nullptr;
 std::optional<double> cpuUsage_;
 quint64 residentBytes_=0;
 bool recordingPaused_=false;
 SentinelTelemetry::RecordingSpaceEstimate recordingSpace_;
 QTimer transformTimer_,cardLayoutTimer_;
 bool editHidden_=false;
 bool editMode_=false, placing_=false, dragging_=false,resizing_=false,transformChanged_=false;
 double resizeScaleStart_=1;
 QSize resizePanelStart_;
 int resizeThumbnailStart_=320;
 QPoint dragOrigin_, windowOrigin_;
 QSettings settings_;
 QTimer timer_, dismiss_, thumbnailAlertPreview_;
 QWidget *badge_;

 QLabel *badgeText_, *alertIcon_;
 QString lastAlertTitle_,lastAlertBody_;
 int panelMode() const;
 void activateAction(int action);
 void syncVisibilityControls();
 void updateAlertIcon();
 QString storageText_, encoderText_, networkText_, captureStatus_;
 void setCaptureStatus(const QString &text);
 void setHotkeyBinding(int index,obs_key_combination_t combination);
 void refreshHotkeyEditors();
 QString hotkeyFingerprint_;
 QPointer<QDialog> dialog_;
 QWidget *preview_, *previewCard_;
 QWidget *information_;
 QBoxLayout *panelLayout_;
 bool recordingActive_=false, streamingActive_=false, cameraActive_=false;
 static constexpr int HotkeyCount=8;
 obs_hotkey_id keys_[HotkeyCount];
 struct Key { SentinelDock *self; int action; } keyData_[HotkeyCount];
 QThreadPool pool_;
 std::mutex mutex_;
 bool checking_ = false, resultReady_ = false;
 qint64 freeBytes_ = -1, resultBytes_ = -1;
 bool storageAlarm_ = false, storageFull_=false, encoderAlarm_ = false, storageUnavailable_ = false;
 bool ready_ = false, alertActive_ = false, sessionNotice_=false, metricsVisible_=true;
 QMap<QString,QString> alerts_;
 obs_output_t *lastStream_ = nullptr;
 int networkTotal_ = 0, networkDropped_ = 0;
 bool networkAlarm_ = false, reconnectAlarm_ = false;
 obs_output_t *lastOutput_ = nullptr;
 int lastTotal_ = 0, lastSkipped_ = 0;
 int probeTicks_ = 0;
 QString storagePath_, resultPath_;
};
