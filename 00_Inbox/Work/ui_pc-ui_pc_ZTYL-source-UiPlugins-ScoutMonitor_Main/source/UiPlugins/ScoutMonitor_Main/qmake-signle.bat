@echo off
set PATH = C:\Qt\Qt5.7.0\Tools\QtCreator\bin;%PATH%
set QTPATH=C:\Qt\Qt5.7.0\Tools\QtCreator\bin
set QMAKEPATH=C:\Qt\Qt5.7.0\5.7\msvc2015\bin
set VSTOOLS=C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\Tools
set CURPATH=%cd%
CALL "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64_x86
%QTPATH%\jom.exe clean
echo %QMAKEPATH%\qmake.exe
%QMAKEPATH%\qmake.exe ScoutMonitor_Main.pro -r -spec win32-msvc2015 && %QTPATH%\jom.exe qmake_all
%QTPATH%\jom.exe -f Makefile
cd ..
cd ..
pause
