@echo off
setlocal
echo ===================================================
echo   LTM FLIGHT DESKTOP (GTK4 + C + GStreamer)
echo ===================================================

REM Them duong dan MSYS2 UCRT64 vao PATH de nap day du DLL GTK4 va GStreamer
set "PATH=E:\msys2\ucrt64\bin;%PATH%"

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
