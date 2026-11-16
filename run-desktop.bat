@echo off
setlocal
echo ===================================================
echo   LTM FLIGHT DESKTOP (GTK4 + C + GStreamer)
echo ===================================================

REM Tim MSYS2 UCRT64 de nap DLL GTK4 va GStreamer
if exist "C:\msys64\ucrt64\bin\libgtk-4-1.dll" (
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
) else if exist "E:\msys2\ucrt64\bin\libgtk-4-1.dll" (
    set "PATH=E:\msys2\ucrt64\bin;%PATH%"
) else (
    echo [LOI] Khong tim thay MSYS2 UCRT64. Hay cai MSYS2 hoac them ucrt64\bin vao PATH.
    pause
    exit /b 1
)

REM Thiet lap bien moi truong chi dinh chinh xac file video.mp4
set "LTM_VIDEO=%~dp0frontend\public\video.mp4"

cd /d "%~dp0client\gtk4\build"
if not exist "ltm-flight.exe" (
    echo [LOI] Khong tim thay file ltm-flight.exe!
    pause
    exit /b 1
)

echo Dang mo ung dung Desktop (LTM Flight)...
start "" "%~dp0client\gtk4\build\ltm-flight.exe"
endlocal
