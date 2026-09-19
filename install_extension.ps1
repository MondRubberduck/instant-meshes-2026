$appDataBlender = Join-Path $env:APPDATA "Blender Foundation\Blender"
if (Test-Path $appDataBlender) {
    $versions = Get-ChildItem $appDataBlender -Directory
    foreach ($ver in $versions) {
        $oldDest = Join-Path $ver.FullName "extensions\user_default\instant_meshes_retopo"
        if (Test-Path $oldDest) { Remove-Item -Path $oldDest -Recurse -Force -ErrorAction SilentlyContinue }
        $dest = Join-Path $ver.FullName "extensions\user_default\instant_meshes_2026"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Path "blender_extension\*" -Destination $dest -Recurse -Force
        Write-Host "[OK] Successfully installed InstantMeshes 2026 Extension to Blender $($ver.Name)" -ForegroundColor Green
    }
} else {
    Write-Host "Blender AppData folder not found at $appDataBlender" -ForegroundColor Yellow
}
