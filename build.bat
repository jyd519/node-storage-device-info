rd /s /q %~dp0\bin
rd /s /q %~dp0\build\Release
electron-rebuild -v 2.0.13 -a ia32
