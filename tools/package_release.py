#!/usr/bin/env python3
"""Package release archives for Instant Meshes 2026.

Produces:
  1. instant_meshes_retopo.zip      - Blender extension (manifest, __init__.py,
                                      pyretopo .pyd modules for each supported
                                      Python ABI, optional CLI fallback exe)
  2. InstantMeshes2026CLI-win64.zip - standalone CLI executable
  3. SHA256SUMS.txt                 - checksums of every produced archive

The Blender extension zip must stay CLEAN: no __pycache__, no .pyc, no stray
files. Shipping compiled bytecode and unlisted files inside the extension zip
is a common trigger for antivirus/SmartScreen heuristics and is rejected by
Blender's extension validator on extensions.blender.org.

Usage:
  python tools/package_release.py --build-dir build --out-dir dist [--no-cli]

Binaries are located automatically under --build-dir:
  - build/python/Release/pyretopo*.pyd   (Windows multi-config)
  - build/python/pyretopo*.pyd           (single-config generators)
  - build/Release/InstantMeshes2026CLI.exe
"""
import argparse
import hashlib
import os
import sys
import zipfile

# Fixed timestamp so archives are byte-reproducible when inputs are identical.
ZIP_DATE_TIME = (2026, 1, 1, 0, 0, 0)

EXCLUDE_NAMES = {"__pycache__", "instant_meshes_retopo.zip", "SHA256SUMS.txt"}
EXCLUDE_SUFFIXES = (".pyc", ".pyo")


def find_one(patterns, roots):
    """Return the first existing file matching any glob pattern under the roots."""
    import glob
    for root in roots:
        for pattern in patterns:
            hits = sorted(glob.glob(os.path.join(root, pattern)))
            if hits:
                return hits[0]
    return None


def add_file(zf, src, arcname):
    if os.path.basename(arcname) in EXCLUDE_NAMES or arcname.endswith(EXCLUDE_SUFFIXES):
        return
    zi = zipfile.ZipInfo(arcname, date_time=ZIP_DATE_TIME)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    with open(src, "rb") as fh:
        zf.writestr(zi, fh.read())


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default="build", help="CMake build directory")
    ap.add_argument("--out-dir", default="dist", help="Output directory for archives")
    ap.add_argument("--no-cli", action="store_true", help="Do not bundle the CLI fallback exe in the extension zip")
    ap.add_argument("--extra-pyd", action="append", default=[],
                    help="Additional pyretopo module(s) built for another Python ABI "
                         "(e.g. the cp311 build for Blender 4.2-4.5); repeatable")
    args = ap.parse_args()

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ext_src = os.path.join(root, "blender_extension")
    build = os.path.abspath(args.build_dir)
    out = os.path.abspath(args.out_dir)
    os.makedirs(out, exist_ok=True)

    # --- locate build outputs (Windows multi-config and single-config layouts)
    pyd_candidates = []
    import glob
    for sub in ("python/Release", "python", "Release", "."):
        pyd_candidates += glob.glob(os.path.join(build, sub, "pyretopo*.pyd"))
        pyd_candidates += glob.glob(os.path.join(build, sub, "pyretopo*.so"))
    seen = set()
    pyds = []
    for p in pyd_candidates + list(args.extra_pyd):
        rp = os.path.realpath(p)
        if rp not in seen:
            seen.add(rp)
            pyds.append(p)
    if not pyds:
        print("ERROR: no pyretopo module found under", build, file=sys.stderr)
        return 1

    cli_exe = find_one(["InstantMeshes2026CLI.exe"], [os.path.join(build, "Release"), build])

    # --- 1. Blender extension zip
    ext_zip = os.path.join(out, "instant_meshes_retopo.zip")
    with zipfile.ZipFile(ext_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        add_file(zf, os.path.join(ext_src, "blender_manifest.toml"), "blender_manifest.toml")
        add_file(zf, os.path.join(ext_src, "__init__.py"), "__init__.py")
        for pyd in pyds:
            add_file(zf, pyd, os.path.basename(pyd))
        if cli_exe and not args.no_cli:
            add_file(zf, cli_exe, os.path.basename(cli_exe))
    print(f"[OK] {ext_zip} ({os.path.getsize(ext_zip)} bytes)")

    # --- 2. Standalone CLI zip
    if cli_exe:
        cli_zip = os.path.join(out, "InstantMeshes2026CLI-win64.zip")
        with zipfile.ZipFile(cli_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            add_file(zf, cli_exe, os.path.basename(cli_exe))
        print(f"[OK] {cli_zip} ({os.path.getsize(cli_zip)} bytes)")

    # --- 3. Checksums
    sums_path = os.path.join(out, "SHA256SUMS.txt")
    with open(sums_path, "w", newline="\n") as fh:
        for name in sorted(os.listdir(out)):
            if name == "SHA256SUMS.txt":
                continue
            fh.write(f"{sha256_of(os.path.join(out, name))}  {name}\n")
    print(f"[OK] {sums_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
