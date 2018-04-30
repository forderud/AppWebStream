@echo off

mkdir x64\Debug
copy /y %FFMPEG_ROOT%\bin\*.dll x64\Debug

mkdir x64\Release
copy /y %FFMPEG_ROOT%\bin\*.dll x64\Release

::pause
