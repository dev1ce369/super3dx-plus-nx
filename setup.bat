@echo off
rem Double-click this to set up Super3DX+: it finds (or installs) Python, then runs the guided setup (build.py).
cd /d "%~dp0"
title Super3DX+ setup
set "PY="
python --version >nul 2>nul && set "PY=python"
if not defined PY py -3 --version >nul 2>nul && set "PY=py -3"
if not defined PY if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" set "PY=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
if defined PY goto run

echo Python is not installed on this PC. Super3DX+ needs it to build the game.
where winget >nul 2>nul
if errorlevel 1 (
  echo.
  echo Please install Python 3 from https://www.python.org/downloads/ ^(tick "Add python.exe to PATH"^) and double-click this file again.
  pause
  exit /b 1
)
choice /m "Install Python 3.12 now with winget (free, from the Microsoft/Python catalogue)"
if errorlevel 2 (
  echo Okay. Install Python 3 from https://www.python.org/downloads/ and double-click this file again.
  pause
  exit /b 1
)
winget install -e --id Python.Python.3.12 --scope user --accept-package-agreements --accept-source-agreements
if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" set "PY=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
if not defined PY (
  echo.
  echo Python was installed, but I cannot find it yet. Close this window and double-click setup.bat again.
  pause
  exit /b 1
)

:run
rem updater-safe: from here the rest of this file is read as one block, so the setup can replace this file (a new version) while it runs.
(
  if "%~1"=="" (%PY% build.py wizard) else (%PY% build.py %*)
  echo.
  pause
  exit /b
)
