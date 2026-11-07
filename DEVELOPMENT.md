# lookout for OBS

Independent Windows OBS 33+ plugin for private monitoring cards and alerts. It
has no dependency on the separate Recording Folder dock.

## Dock and settings

The icon-only dock defaults to Show panels, Hide panels, Cycle anchor corners,
Cycle saved positions, Display mode and Settings. Buttons keep equal spacing
as the dock wraps or stacks. In the Dock settings page, add, remove or reorder
buttons, or restore the defaults. The preset button becomes available when you
save a position.
The dock inherits the OBS theme. The dock's Settings button and OBS's
**Tools → lookout Settings** open the same Qt Quick window with Overlay,
Panels, Appearance, Placement, Alerts, Shortcuts and About pages.
Changes save immediately. Turn on Edit panel placement to move cards or grab
a lower-right resize grip. Precision Placement keeps pixel dimensions,
insets and snapping visible on that page. The window fits each page to the
available display space.

## Layout and presentation

- Panel visibility: Alerts only, Always visible or Disabled (including alerts).
- **Panels → Thumbnail → Show thumbnail** includes the live OBS program output
  in any display mode.
  Thumbnail placement is above, below, left or right. Narrow side-by-side
  layouts shrink the thumbnail or stack vertically to keep metrics readable.
- Preview beside **Show alert symbol on thumbnail** displays a sample storage
  warning icon on the live thumbnail for 10 seconds without changing either setting.
- Metrics contains universal Recording, Storage, Streaming, Encoder, Virtual
  camera, Network and Replay buffer choices. A choice applies to its text row,
  status icon, graph, and matching output series. With no selected metrics, no
  empty metric cards remain. Alert monitoring is configured separately in Alerts.
- CPU usage and memory usage report the OBS process. Disk full in estimates
  remaining recording time from free space and recent measured recording
  throughput. These metrics have individual toggles and icons.
- **Icons** shows the selected metric glyphs. **Active icons** shows running
  outputs and active alerts. **Custom** uses the individual stat and graph
  settings.
- Placement groups display, anchor, four size presets, one custom scale and saved
  positions. Individual card grips resize that card without scaling the others.
  Reset position restores the selected anchor. Position presets include card
  positions, widths, scale and snap settings.

Each thumbnail, stats group, graph and alert callout has its own rounded surface,
with consistent spacing and borders. There is no global panel background.

## Graphs and bitrate

Panels has separate Thumbnail, Stats, and Graphs controls. All enabled stats,
including bitrate and replay buffer, appear on a single card. Choose Off, Mini,
or Full for each graph independently. Both graph sizes can appear together.

The bitrate graph remains visible when enabled. Active recording and streaming
outputs share a dynamic scale, with one line per active output. The replay
graph plots estimated fill from 0 to 100%. A full rolling buffer isn't an alert.

OBS 33 exposes encoded-packet callbacks and replay limits, but no exact buffer
occupancy getter. lookout keeps bounded GOP metadata (sizes/timestamps only;
never packet payloads) to estimate retained duration and bytes. It waits for a
keyframe and starts tracking when attached, so a buffer already running at attach
time needs time to converge. The UI explicitly labels this as estimated fill.
Source: https://github.com/obsproject/obs-studio/blob/33.0.1/plugins/obs-ffmpeg/obs-ffmpeg-mux.c

Graph controls cover disk space, skipped frames, dropped frames, CPU usage, memory
usage, time until the recording disk fills, bitrate and replay buffer. Clear
history clears all of these histories, which are bounded to fifteen minutes.

Appearance separates Colors, Chart line design and Chart detail. Follow OBS
uses the active OBS palette and disables the separate Appearance selector.
Switching back to another color preset restores your saved appearance preference.
lookout uses a translucent glass finish in Light
mode. Line, filled area, stepped and grid designs can pair with any palette.
Full info and Minimal control chart labels and axes independently.
Panel effects separates card opacity from Gaussian backdrop blur. Set Blur
amount to 0% to turn it off. Adjustable backdrop blur requires Windows 11.

## Alerts

The Alerts page groups messages under Recording storage, Encoder, and Stream
connection. Each message has a description of its trigger and a named preview.
Output changes can reveal the panels when an output starts or stops.

Storage warning must be greater than zero. Warning color starts at the selected
free-space value and stays warning while any bytes remain. Zero free bytes is
critical. Encoder and network alerts activate at their configured thresholds.
Unavailable storage and stream reconnection are urgent.

Only affected stats and graph cards get alert colors/glow. In thumbnail-only
mode, a large centered offending icon can identify the issue. Glow colors,
intensity, spread and speed have live preview controls. Specific alert previews
do not alter measured values or generate real alerts.

Alerts can be retained until dismissed. Dismiss current alerts is available in
Alerts settings. Otherwise the configured duration applies to all alerts,
including critical alerts. Recording, streaming and virtual-camera lifecycle
changes can reveal the panel without a redundant session-update callout.
The plugin never stops an OBS output automatically.

Disk checks run on a worker every ten seconds. Output-health/bitrate samples run
every two seconds. Warnings rearm after recovery.

## Transitions and rendering

Choose Instant, Fade, Flash, Slide, Blur or Fall under Overlay → Transitions.
Use Preview beside the selector to play the selected show and hide transition.
Instant is the default. Fall drops the cards into their saved positions on show.
On hide, cards tumble, bounce against each other and the display edges, then
fade out. Cards stay on the same capture-protected window during transitions.
The live thumbnail uses its GPU texture on show and retains its final frame
through the exit. Placement editing and capture-privacy changes take effect
immediately. Native background blur returns when the cards settle.

## Native OBS hotkeys

Only these eight plugin actions are exposed, using OBS's built-in dispatcher:

1. Show Panel (persistent)
2. Hide Panel (alerts override)
3. Disable Panel (also disables alerts)
4. Toggle Thumbnail
5. Toggle Stats
6. Toggle Show in Recordings/Stream
7. Cycle Position (Defaults)
8. Cycle Position (Presets)

The Shortcuts page edits those native bindings directly. Core recording, streaming
and camera output shortcuts are not exposed or changed.

## Capture and game compatibility

**Include in recordings and streams** is off by default. Private mode applies
Windows capture exclusion to lookout’s panel and blur windows. lookout leaves OBS
sources, scenes, and capture methods as configured. Private panels pause when a
visible Display Capture source uses Automatic or Legacy (DXGI), or when Windows
cannot verify exclusion. Windows 10 2004+ is required for exclusion.

Use borderless fullscreen for the most reliable visibility. Windows Fullscreen
Optimizations can let compatible fullscreen games display desktop overlays.
When Windows reports exclusive fullscreen, lookout pauses the panels, effects,
and live thumbnail transfers until the desktop overlay can resume.

## About and artwork

About shows the build version and links to GitHub Releases for downloads and release notes.

## Build, installation and QA

Close OBS and run **install-lookout-1.0.0-rc.1.exe**. Start OBS, then open
**Docks > lookout**. The installer verifies and replaces only the lookout plugin
folder. Saved settings, hotkeys, presets, and the separate Recording Folder dock
stay in place. There is no separate Qt installation or download on first run.

For portable installation, extract the `obs-sentinel` folder from the ZIP and
place the whole folder under `C:/ProgramData/obs-studio/plugins/` with OBS closed.
Keep its `ui` folder beside `obs-sentinel.dll`. A DLL-only copy is incomplete.

Build the native integration and isolated QML renderer with their own SDKs:

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="PATH_TO_OBS_SDK;PATH_TO_OBS_QT"
cmake --build build --config RelWithDebInfo
cmake -S quick -B build-quick -A x64 -DCMAKE_PREFIX_PATH="PATH_TO_PRIVATE_QT_SDK"
cmake --build build-quick --config RelWithDebInfo
```

Use `installer/package.ps1` with CMake, the private Qt SDK, Visual C++ runtime
directory and Qt license directory to produce the installer and portable ZIP.
The private QML window uses the OBS Direct3D 11 adapter and a shared GPU texture
for the live thumbnail. The dock, OBS hotkeys and telemetry stay native. Settings and charts use Qt Quick.

From the lookout source directory, use the test runner after configuring the build
directories above:

```powershell
./tests/verify.ps1               # Incremental build and all screenless tests
./tests/verify.ps1 -Scope Quick  # QML settings, charts, and transitions
./tests/verify.ps1 -Scope Native # OBS integration, placement, and telemetry
```

These checks run without opening OBS. The QML suite loads the actual settings and
chart components with sample data and fails on runtime warnings. Use it for
routine control, layout, and chart changes. For visual design reviews, check only
the changed view at the sizes and themes relevant to the change.

Use `./tests/verify.ps1 -Scope Desktop` after packaging when a change
affects the live GPU thumbnail, capture exclusion, desktop blur, overlay input,
or native window behavior. It runs the isolated Windows test harness and saves
evidence under `build/desktop-qa/`. Game compatibility and live streaming remain
environment-specific checks. See BUILDINFO.json for the current package details.

Panel-style QA covers 270 style/mode/width/scale combinations under simulated
OBS global font/text rules, plus gradient contrast, rounded alpha and opacity.
QML panels receive an explicit palette, and dock and settings controls keep their
native OBS styling. Presentation controls never change the master visibility setting. Older
format, icon and graph preferences migrate to the unified controls.
