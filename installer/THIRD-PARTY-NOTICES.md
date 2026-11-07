# lookout bundled runtime

The private lookout UI includes dynamically linked Qt 6.8.3 libraries and QML
modules from Qt Base, Qt Declarative and Qt SVG. The Qt files are under `ui` and
may be replaced with compatible libraries. OBS's installed Qt libraries are not
changed. Qt license texts are in `ui/licenses`.

- [Qt Base 6.8.3 source](https://github.com/qt/qtbase/tree/v6.8.3)
- [Qt Declarative 6.8.3 source](https://github.com/qt/qtdeclarative/tree/v6.8.3)
- [Qt SVG 6.8.3 source](https://github.com/qt/qtsvg/tree/v6.8.3)
- [Official Qt 6.8.3 source archives](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/)

The Microsoft Visual C++ runtime is deployed app-local from the Microsoft Visual
Studio redistributable files. Windows supplies Direct3D 11 and the desktop
compositor. The installer uses the Windows PowerShell included in Windows.
