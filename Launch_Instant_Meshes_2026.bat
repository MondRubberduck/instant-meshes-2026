@echo off
title InstantMeshes 2026 Standalone
echo ============================================================
echo Starting InstantMeshes 2026 Standalone GUI App...
echo ============================================================
cd /d %~dp0InstantMeshes_App
if exist InstantMeshes2026.exe (
    InstantMeshes2026.exe %*
) else if exist Instant Meshes 2026.exe (
    Instant Meshes 2026.exe %*
) else (
    Instant Meshes.exe %*
)
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ============================================================
    echo [ERROR] InstantMeshes 2026 exited with error code %ERRORLEVEL%.
    echo ============================================================
    pause
)
