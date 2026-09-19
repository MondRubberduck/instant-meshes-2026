@echo off
echo ===================================================
echo   Installing InstantMeshes 2026 Extension to Blender
echo ===================================================

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_extension.ps1"

echo.
echo ===================================================
echo   Done! You can now open Blender and enable the
echo   'InstantMeshes2026 Retopology' extension in:
echo   Edit ^> Preferences ^> Add-ons / Extensions
echo ===================================================
pause
