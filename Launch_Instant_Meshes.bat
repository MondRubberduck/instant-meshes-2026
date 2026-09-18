@echo off
title Instant Meshes Standalone
echo ============================================================
echo Starting Instant Meshes Standalone GUI App...
echo ============================================================
cd /d "%~dp0InstantMeshes_App"
"Instant Meshes.exe" %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ============================================================
    echo [ERROR] Instant Meshes exited with error code %ERRORLEVEL%.
    echo ============================================================
    pause
)
