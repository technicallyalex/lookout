// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPalette>
#include <atomic>
#include <mutex>
#include <graphics/graphics.h>
class SentinelDock;

// No Quick/QML runtime is loaded into OBS. The private child receives bounded,
// versioned local messages and a keyed-mutex GPU texture, never video pixels.
class QuickPanelBridge final:public QObject {
 friend struct SentinelTestAccess;
public:
 explicit QuickPanelBridge(SentinelDock *dock,const QString &executable);
 ~QuickPanelBridge() override;
 void publish();
 void stop();
 void openSettings();
 bool capturingShortcut() const { return capturingShortcut_; }
 QPalette panelPalette() const;
 QString panelDesign() const;
 QString chartLines() const;
 QString panelTransition() const;
 int chartDetail() const;
 bool connected() const {return authenticated_;}
 bool overlayBlocked() const {return overlayBlocked_;}
 bool transitionPreviewActive() const;
 QJsonObject snapshot(const QString &blockReason={}) const;
private:
 SentinelDock *dock_;
 QLocalServer server_;
 QPointer<QLocalSocket> socket_;
 QProcess process_;
 QTimer timer_;
 QTimer pointerTimer_;
 enum class PreviewPhase { Idle, Prepare, Show, Hide, Restore };
 PreviewPhase transitionPreview_=PreviewPhase::Idle;
 QTimer transitionPreviewTimer_;
 QPoint pointerPosition_;
 QElapsedTimer heartbeat_;
 QString executable_,token_,endpoint_,frameEventName_;
 QByteArray input_,lastScene_;
 QByteArray diagnostic_;
 QByteArray lastSettings_;
 bool settingsOpen_=false, capturingShortcut_=false;
 bool authenticated_=false,stopping_=false;
 bool overlayBlocked_=false;
 int attempts_=0;
 void launch();
 void receive();
 void send(const QJsonObject &message);
 void command(const QJsonObject &message);
 void trackPointer();
 void settingsCommand(const QJsonObject &message);
 void previewTransition();
 void advanceTransitionPreview();
 void finishTransitionPreview();
 void publishSettings();
 static void render(void*);
 std::atomic<bool> thumbnail_{false};
 mutable std::mutex gpuMutex_;
 gs_texture_t *shared_=nullptr;
 uint32_t handle_=0,luidLow_=0;
 int32_t luidHigh_=0;
 bool graphicsReady_=false,thumbnailSupported_=false;
 int generation_=0;
 uint64_t lastVideoFrame_=0;
 void *frameEvent_=nullptr;
 void freeTexture();
};
