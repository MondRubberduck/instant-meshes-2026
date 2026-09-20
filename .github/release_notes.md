## Download blocked or flagged by antivirus?

This release contains **unsigned native binaries** (CLI executables and Python
extension modules for Windows and Linux) built by [GitHub Actions from the
public source code](https://github.com/MondRubberduck/instant-meshes-2026/actions/workflows/release.yml) — you can inspect every build step yourself.

Because the project is new and the files have low download prevalence,
SmartScreen and some antivirus engines show heuristic false positives such as
*"This file may be dangerous"* or *"Win32/Wacatac.B!ml"*. The binaries contain
no malware; every byte is produced by the CI pipeline linked above.

### How to verify a download

1. Download `SHA256SUMS.txt` from this release.
2. Compare the hash of the archive you downloaded:
   - Windows (PowerShell): `Get-FileHash instant_meshes_retopo.zip -Algorithm SHA256`
   - Linux/macOS: `sha256sum instant_meshes_retopo.zip`
3. If the hashes match, the file is exactly the one CI built from the tagged
   commit — nothing was modified in transit.

### If your browser or antivirus still blocks it

- **Microsoft Edge / Defender**: click *More info* → *Run anyway* / *Allow*,
  or restore the file from *Protection history*. You can report the false
  positive to Microsoft at <https://www.microsoft.com/wdsi/filesubmission>
  (select "Software developer").
- **Google Chrome**: click *Keep* on the download, or report it via
  *safebrowsing.google.com/safebrowsing/report_error/*.
- **Prefer to trust no one?** Build from source:
  `cmake -B build -DINSTANT_MESHES_BUILD_GUI=OFF && cmake --build build --config Release`
