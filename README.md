# lookout

An OBS plugin with customizable overlays to show the Preview, statics, graphs, and alerts while gaming.  Primarily designed for folks using OBS with a single monitor setup, lookout gives you a real-time look at what OBS is doing when it's hidden behind a game or other full screen applications.  It features rich customization, including the option to hide it from your OBS preview, recordings, and streams.

## Features

- **Live OBS thumbnail.** See what OBS is sending to your recording or stream. Show the thumbnail alongside any display mode and place it above, below, or beside your metrics.
- **Stats, graphs, and compact icons.** Track recording, streaming, virtual camera, OBS CPU and memory usage, available disk space, estimated time until the recording drive fills, skipped frames due to encoding lag, dropped frames over the network, bitrate, and estimated replay buffer fill. Custom mode puts all selected stats in one card and lets you choose Off, Mini, or Full for each graph. Icons and Active icons give you a compact view.
- **Alerts you can act on.** Set thresholds for low recording storage, encoder overload, and dropped stream frames. Get notified about connection interruptions and when recording, streaming, or the virtual camera starts or stops. Preview alerts in settings, tune the glow on the affected card, and choose whether alerts stay until dismissed.
- **Make it fit your setup.** Move and resize cards, snap them to each other or the screen edges, and save layouts as presets. Follow your OBS theme or pick a color palette, then adjust chart styles, opacity, and blur. Pick Instant, Fade, Flash, Slide, Blur, or Fall for show and hide transitions. In Fall mode, cards bounce off each other and the display edges as they exit.
- **Control it from OBS.** Use the compact icon dock to show or hide panels, cycle positions and presets, or change the display mode. Assign lookout shortcuts through OBS’s built-in hotkey system.
- **Designed for low overhead.** The thumbnail uses a shared GPU texture and pauses live frame transfers when hidden. The overlay runs outside the game process.

## Installation

**Requirements**

Windows x64 and OBS Studio 33 or newer.

1. Download the Windows installer or portable ZIP from [GitHub Releases](https://github.com/technicallyalex/lookout/releases).
2. Close OBS.
3. Choose an installation method.
   - **Installer.** Run `install-lookout-<version>.exe`. The package includes the runtime it needs.
   - **Manual.** Extract the entire `lookout` folder into `%ProgramData%\obs-studio\plugins\`. Keep `lookout.dll` and the `ui` folder together inside `lookout`.
4. Start OBS.

Use the installer when upgrading from Release Candidate 1. It replaces the earlier plugin folder, and lookout imports the saved settings on first launch. Reopen the dock from **Docks → lookout** if OBS has saved its previous placement.

## Use

1. Open **Tools → lookout Settings** to choose what to show and when.
2. Open **Docks → lookout** for quick access to the panel controls.
3. In **Placement**, turn on **Edit panel placement** to drag or resize the cards. Save the layout as a preset if you want to switch back to it later.

## Game and capture compatibility

Use **borderless fullscreen** for the most reliable overlay visibility. Games set to fullscreen can also work when Windows **Fullscreen Optimizations** are enabled and Windows presents the game in borderless mode. When Windows reports true exclusive fullscreen, lookout pauses its panels and live thumbnail. While the overlay is visible, the thumbnail and effects use some GPU resources. [Microsoft’s guide to fullscreen modes](https://devblogs.microsoft.com/directx/demystifying-full-screen-optimizations/)

**Include in recordings and streams** is off by default. This applies Windows capture exclusion to lookout’s own windows. lookout leaves your OBS sources, scenes, and capture methods as configured. If a visible Display Capture source uses **Automatic** or **Legacy (DXGI)**, or if Windows cannot verify exclusion, lookout hides its panels. Turning the setting on makes the panels available to sources that capture the desktop.

Capture exclusion requires Windows 10 version 2004 or later. [Microsoft’s capture exclusion documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)

Background blur requires Windows 11. The live thumbnail uses Direct3D 11.

See [Development](DEVELOPMENT.md) for build and test instructions.
