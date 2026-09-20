$ErrorActionPreference = "Stop"
$extSrc = Join-Path $PSScriptRoot "blender_extension"

# The compiled pyretopo module is no longer committed to the repository.
# Installing from a fresh clone therefore only works if binaries were built
# locally (cmake) or copied from a GitHub Release into blender_extension\.
$pyd = Get-ChildItem -Path $extSrc -Filter "pyretopo*.pyd" -File -ErrorAction SilentlyContinue
if (-not $pyd) {
    Write-Host "[WARN] No pyretopo .pyd found in blender_extension\." -ForegroundColor Yellow
    Write-Host "        Build it:  cmake -B build -DINSTANT_MESHES_BUILD_GUI=OFF; cmake --build build --config Release" -ForegroundColor Yellow
    Write-Host "        Then copy build\python\Release\pyretopo*.pyd into blender_extension\ and re-run." -ForegroundColor Yellow
    Write-Host "        (Or install the prebuilt instant_meshes_retopo.zip from the GitHub Releases page instead.)" -ForegroundColor Yellow
    exit 1
}

$appDataBlender = Join-Path $env:APPDATA "Blender Foundation\Blender"
if (Test-Path $appDataBlender) {
    $versions = Get-ChildItem $appDataBlender -Directory
    foreach ($ver in $versions) {
        $oldDest = Join-Path $ver.FullName "extensions\user_default\instant_meshes_retopo"
        if (Test-Path $oldDest) { Remove-Item -Path $oldDest -Recurse -Force -ErrorAction SilentlyContinue }
        $dest = Join-Path $ver.FullName "extensions\user_default\instant_meshes_2026"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        # Copy sources and binaries, but never stale bytecode caches
        Copy-Item -Path "$extSrc\*" -Destination $dest -Recurse -Force -Exclude "__pycache__"
        if (Test-Path (Join-Path $dest "__pycache__")) {
            Remove-Item -Path (Join-Path $dest "__pycache__") -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "[OK] Successfully installed InstantMeshes 2026 Extension to Blender $($ver.Name)" -ForegroundColor Green
    }
} else {
    Write-Host "Blender AppData folder not found at $appDataBlender" -ForegroundColor Yellow
}
