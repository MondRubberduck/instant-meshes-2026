#!/usr/bin/env python3
"""Package release archives for Instant Meshes 2026.

Produces (per platform, inside the build jobs):
  - InstantMeshes2026_Win64.zip   - Windows app: GUI executable, CLI, README.txt
  - InstantMeshes2026_Linux64.zip - Linux app:   GUI executable, CLI, README.txt

And (in the release job, after merging both platforms' binaries):
  - InstantMeshes2026_AddOn.zip   - Blender add-on: manifest, __init__.py and
                                    the pyretopo modules for every supported
                                    Python ABI on both platforms (Blender picks
                                    the matching one at import time), plus the
                                    CLI fallback executables
  - SHA256SUMS.txt                - checksums of every produced archive

The archives must stay CLEAN: no __pycache__, no .pyc, no stray files.
Shipping compiled bytecode inside an add-on zip is a common trigger for
antivirus/SmartScreen heuristics.

Usage:
  build job:  python tools/package_release.py --platform win64  --build-dir build --extra-pyd <cp311 module>
              python tools/package_release.py --platform linux64 --build-dir build --extra-pyd <cp311 module>
  release job (after downloading all 'bin-*' artifacts into one directory):
              python tools/package_release.py --addon --bin-dir bin --out-dir dist
              python tools/package_release.py --checksums-only dist
"""
import argparse
import glob
import hashlib
import os
import sys
import zipfile

# Fixed timestamp so archives are byte-reproducible when inputs are identical.
ZIP_DATE_TIME = (2026, 1, 1, 0, 0, 0)

EXCLUDE_NAMES = {"__pycache__", "SHA256SUMS.txt"}
EXCLUDE_SUFFIXES = (".pyc", ".pyo")

APP_README = """Instant Meshes 2026
==================

InstantMeshes2026{exe_suffix}      - The application. Double-click to open the
                              interactive GUI (flow brushes, contour sketching).
InstantMeshes2026CLI{exe_suffix}   - Command-line tool for batch / scripted
                              remeshing. It has no window on its own; run it
                              from a terminal:
                                  InstantMeshes2026CLI{exe_suffix} -h
                                  InstantMeshes2026CLI{exe_suffix} -f 2500 -i -o output.obj input.obj

If your browser or antivirus warned you about this download: these are
unsigned binaries built by public CI from public source code. Verify the
SHA-256 checksums in SHA256SUMS.txt next to this archive on the release page:
https://github.com/MondRubberduck/instant-meshes-2026/releases
"""

ADDON_README = """Instant Meshes 2026 - Blender Add-on
====================================

Installation:
  1. Blender > Edit > Preferences > Get Extensions
  2. Click the dropdown menu (top right) > Install from Disk...
  3. Select this zip file and enable the extension.

Usage:
  Open the 3D Viewport sidebar (N key) > "Retopo" tab with a mesh object
  selected. The add-on runs in-memory via the bundled pyretopo module; the
  compiled module matching your Blender's Python version is picked
  automatically (Blender 4.2-4.5 = Python 3.11, Blender 5.x = Python 3.13).

This archive contains modules for Windows and Linux, both Python ABIs.
"""

PLATFORMS = {
    "win64": {
        "app_zip": "InstantMeshes2026_Win64.zip",
        "gui_names": ["InstantMeshes2026.exe"],
        "cli_names": ["InstantMeshes2026CLI.exe"],
        "module_patterns": ["pyretopo*.pyd"],
        "exe_suffix": ".exe",
    },
    "linux64": {
        "app_zip": "InstantMeshes2026_Linux64.zip",
        "gui_names": ["InstantMeshes2026"],
        "cli_names": ["InstantMeshes2026CLI"],
        "module_patterns": ["pyretopo*.so"],
        "exe_suffix": "",
    },
}

ADDON_CLI_NAMES = ["InstantMeshes2026CLI.exe", "InstantMeshes2026CLI"]


def find_one(patterns, roots):
    for root in roots:
        for pattern in patterns:
            hits = sorted(glob.glob(os.path.join(root, pattern)))
            if hits:
                return hits[0]
    return None


def add_file(zf, src, arcname, executable=False):
    if os.path.basename(arcname) in EXCLUDE_NAMES or arcname.endswith(EXCLUDE_SUFFIXES):
        return
    zi = zipfile.ZipInfo(arcname, date_time=ZIP_DATE_TIME)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = (0o755 if executable else 0o644) << 16
    with open(src, "rb") as fh:
        zf.writestr(zi, fh.read())


def add_text(zf, arcname, text):
    zi = zipfile.ZipInfo(arcname, date_time=ZIP_DATE_TIME)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = (0o644) << 16
    zf.writestr(zi, text)


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_checksums(out_dir):
    sums_path = os.path.join(out_dir, "SHA256SUMS.txt")
    with open(sums_path, "w", newline="\n") as fh:
        for name in sorted(os.listdir(out_dir)):
            if name == "SHA256SUMS.txt":
                continue
            fh.write(f"{sha256_of(os.path.join(out_dir, name))}  {name}\n")
    print(f"[OK] {sums_path}")


def package_platform(plat, build, out):
    gui_bin = find_one(plat["gui_names"], [os.path.join(build, "Release"), build])
    if not gui_bin:
        print(f"ERROR: GUI executable ({plat['gui_names'][0]}) not found under {build}; "
              f"the release must be configured with INSTANT_MESHES_BUILD_GUI=ON", file=sys.stderr)
        return 1
    cli_bin = find_one(plat["cli_names"], [os.path.join(build, "Release"), build])

    app_zip = os.path.join(out, plat["app_zip"])
    with zipfile.ZipFile(app_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        add_file(zf, gui_bin, os.path.basename(gui_bin), executable=True)
        if cli_bin:
            add_file(zf, cli_bin, os.path.basename(cli_bin), executable=True)
        add_text(zf, "README.txt", APP_README.format(exe_suffix=plat["exe_suffix"]))
    print(f"[OK] {app_zip} ({os.path.getsize(app_zip)} bytes)")
    return 0


def package_addon(bin_dir, out):
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ext_src = os.path.join(root, "blender_extension")

    modules = []
    seen = set()
    for pattern in ("pyretopo*.pyd", "pyretopo*.so"):
        for p in sorted(glob.glob(os.path.join(bin_dir, pattern))):
            rp = os.path.realpath(p)
            if rp not in seen:
                seen.add(rp)
                modules.append(p)
    if not modules:
        print(f"ERROR: no pyretopo modules found in {bin_dir}", file=sys.stderr)
        return 1

    clis = []
    for name in ADDON_CLI_NAMES:
        p = os.path.join(bin_dir, name)
        if os.path.isfile(p):
            clis.append(p)

    addon_zip = os.path.join(out, "InstantMeshes2026_AddOn.zip")
    with zipfile.ZipFile(addon_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        add_file(zf, os.path.join(ext_src, "blender_manifest.toml"), "blender_manifest.toml")
        add_file(zf, os.path.join(ext_src, "__init__.py"), "__init__.py")
        for mod in modules:
            add_file(zf, mod, os.path.basename(mod))
        for cli in clis:
            add_file(zf, cli, os.path.basename(cli), executable=True)
        add_text(zf, "README.txt", ADDON_README)
    print(f"[OK] {addon_zip} ({os.path.getsize(addon_zip)} bytes)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default="build", help="CMake build directory")
    ap.add_argument("--out-dir", default="dist", help="Output directory for archives")
    ap.add_argument("--platform", choices=sorted(PLATFORMS),
                    help="Package the application zip for this platform")
    ap.add_argument("--addon", action="store_true",
                    help="Assemble InstantMeshes2026_AddOn.zip from binaries in --bin-dir")
    ap.add_argument("--bin-dir", default="bin",
                    help="Directory holding the collected binaries for --addon")
    ap.add_argument("--checksums-only", metavar="DIR",
                    help="Do not build archives; only (re)generate SHA256SUMS.txt for DIR")
    args = ap.parse_args()

    if args.checksums_only:
        write_checksums(args.checksums_only)
        return 0

    out = os.path.abspath(args.out_dir)
    os.makedirs(out, exist_ok=True)

    if args.addon:
        return package_addon(os.path.abspath(args.bin_dir), out)

    if args.platform:
        return package_platform(PLATFORMS[args.platform],
                                os.path.abspath(args.build_dir), out)

    ap.error("nothing to do: pass --platform, --addon, or --checksums-only")
    return 2


if __name__ == "__main__":
    sys.exit(main())
