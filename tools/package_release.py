#!/usr/bin/env python3
"""Package release archives for Instant Meshes 2026.

Per platform (win64 / linux64) produces:
  1. instant_meshes_retopo[_linux64].zip - Blender extension (manifest,
     __init__.py, pyretopo modules for each supported Python ABI, optional
     CLI fallback binary)
  2. InstantMeshes2026CLI-{win64,linux64}.zip - standalone CLI binary
  3. SHA256SUMS.txt - checksums of every produced archive (only written by
     the final packaging invocation; use --checksums-only to regenerate it
     for a directory that already holds all platform archives)

The Blender extension zips must stay CLEAN: no __pycache__, no .pyc, no
stray files. Shipping compiled bytecode and unlisted files inside the
extension zip is a common trigger for antivirus/SmartScreen heuristics and
is rejected by Blender's extension validator on extensions.blender.org.

Usage (per-platform, inside the build job):
  python tools/package_release.py --platform win64  --build-dir build --extra-pyd <cp311 module>
  python tools/package_release.py --platform linux64 --build-dir build --extra-pyd <cp311 module>

Final step (release job, after merging all platform archives):
  python tools/package_release.py --checksums-only dist

Binaries are located automatically under --build-dir:
  - build/python/Release/pyretopo*.pyd / build/python/pyretopo*.so
  - build/Release/InstantMeshes2026CLI.exe / build/InstantMeshes2026CLI
"""
import argparse
import hashlib
import os
import sys
import zipfile

# Fixed timestamp so archives are byte-reproducible when inputs are identical.
ZIP_DATE_TIME = (2026, 1, 1, 0, 0, 0)

EXCLUDE_NAMES = {"__pycache__", "SHA256SUMS.txt"}
EXCLUDE_SUFFIXES = (".pyc", ".pyo")

PLATFORMS = {
    "win64": {
        "extension_zip": "instant_meshes_retopo.zip",
        "cli_zip": "InstantMeshes2026CLI-win64.zip",
        "cli_names": ["InstantMeshes2026CLI.exe"],
        "module_patterns": ["pyretopo*.pyd"],
    },
    "linux64": {
        "extension_zip": "instant_meshes_retopo_linux64.zip",
        "cli_zip": "InstantMeshes2026CLI-linux64.zip",
        "cli_names": ["InstantMeshes2026CLI"],
        "module_patterns": ["pyretopo*.so"],
    },
}


def find_one(patterns, roots):
    """Return the first existing file matching any glob pattern under the roots."""
    import glob
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default="build", help="CMake build directory")
    ap.add_argument("--out-dir", default="dist", help="Output directory for archives")
    ap.add_argument("--platform", choices=sorted(PLATFORMS), default="win64",
                    help="Which platform's binaries to package (default: win64)")
    ap.add_argument("--no-cli", action="store_true",
                    help="Do not bundle the CLI fallback binary in the extension zip")
    ap.add_argument("--extra-pyd", action="append", default=[],
                    help="Additional pyretopo module(s) built for another Python ABI "
                         "(e.g. the cp311 build for Blender 4.2-4.5); repeatable")
    ap.add_argument("--checksums-only", metavar="DIR",
                    help="Do not build archives; only (re)generate SHA256SUMS.txt for DIR")
    args = ap.parse_args()

    if args.checksums_only:
        write_checksums(args.checksums_only)
        return 0

    plat = PLATFORMS[args.platform]
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ext_src = os.path.join(root, "blender_extension")
    build = os.path.abspath(args.build_dir)
    out = os.path.abspath(args.out_dir)
    os.makedirs(out, exist_ok=True)

    # --- locate build outputs (multi-config and single-config layouts)
    import glob
    module_candidates = []
    for sub in ("python/Release", "python", "Release", "."):
        for pattern in plat["module_patterns"]:
            module_candidates += glob.glob(os.path.join(build, sub, pattern))
    seen = set()
    modules = []
    for p in module_candidates + list(args.extra_pyd):
        rp = os.path.realpath(p)
        if rp not in seen:
            seen.add(rp)
            modules.append(p)
    if not modules:
        print(f"ERROR: no pyretopo module found under {build}", file=sys.stderr)
        return 1

    cli_exe = find_one(plat["cli_names"], [os.path.join(build, "Release"), build])

    # --- 1. Blender extension zip
    ext_zip = os.path.join(out, plat["extension_zip"])
    with zipfile.ZipFile(ext_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        add_file(zf, os.path.join(ext_src, "blender_manifest.toml"), "blender_manifest.toml")
        add_file(zf, os.path.join(ext_src, "__init__.py"), "__init__.py")
        for mod in modules:
            add_file(zf, mod, os.path.basename(mod))
        if cli_exe and not args.no_cli:
            add_file(zf, cli_exe, os.path.basename(cli_exe), executable=True)
    print(f"[OK] {ext_zip} ({os.path.getsize(ext_zip)} bytes)")

    # --- 2. Standalone CLI zip
    if cli_exe:
        cli_zip = os.path.join(out, plat["cli_zip"])
        with zipfile.ZipFile(cli_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            add_file(zf, cli_exe, os.path.basename(cli_exe), executable=True)
        print(f"[OK] {cli_zip} ({os.path.getsize(cli_zip)} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
