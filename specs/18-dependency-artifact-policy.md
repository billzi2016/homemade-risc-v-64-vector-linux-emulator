# Dependency, Download Artifact and Ignore Policy

## 1. Fundamental Principles

- **ART-REQ-001**: External firmware, kernel, rootfs, disk images, download caches, build outputs, and execution logs must not be committed to Git.
- **ART-REQ-002**: All external artifacts must originate from auditable sources, recording versions, licenses, and SHA-256 checksums.
- **ART-REQ-003**: Any network download, dependency installation, or build command execution requires explaining purpose and obtaining user confirmation beforehand.
- **ART-REQ-004**: Untraced pre-compiled images must not replace failed downloads, nor may verification results be faked.
- **ART-REQ-005**: Repository retains reproducible configurations, patches, scripts, and checksum manifests, but does not retain large binary outputs.

## 2. Directory Conventions

All internal repository paths are calculated from root:

```text
artifacts/
├── downloads/      # Raw download cache
├── firmware/       # Firmware build outputs (e.g., OpenSBI)
├── kernel/         # Linux kernel build outputs
├── rootfs/         # rootfs working directories or archives
├── disk/           # Disk images (e.g., rootfs.ext4)
└── logs/           # Build, test, UART, and network logs
```

Actual directory creation and `.gitignore` modifications belong to subsequent implementation task `BLD-005`, requiring separate explanations and confirmations.

## 3. Committable vs Non-Committable Content

### 3.1 Committable

- Download/build script source files.
- Linux `.config`, OpenSBI build parameters, and rootfs package manifests.
- Project-owned patches and patch origin explanations.
- Small plain-text SHA-256 manifests, version lockfiles, and license notices.
- Sanitized test templates and reproduction steps.

### 3.2 Non-Committable

- `opensbi.bin`, `fw_payload.bin` and other firmware binaries.
- `Image`, `vmlinux`, modules, and kernel build trees.
- rootfs archives, directory trees, and ext4 images.
- Compilation objects, executables, caches, packet captures, logs, and core dumps.
- Toolchain archives or installation directories.
- User credentials, private keys, network configurations, and machine-specific absolute paths.

## 4. Source Policy

Technical dependency priority:

1. Signed releases or fixed commits from official project release pages or official source repositories.
2. Trusted Linux distributions or toolchain release mirrors.
3. Mirror providers explicitly approved by user.

Each source records: name, purpose, homepage, exact version or commit, download URL, release date, license, SHA-256, and verification date. Floating-version URLs cannot serve as sole reproduction references.

## 5. Minimal Linux System

Minimal system must be genuinely downloaded or built from locked sources/package sets, including:

- Working init and Shell.
- Userspace matching ext4 support.
- `ls`, `pwd`, `cat`, and mountable `/proc` required for macOS non-networked acceptance.
- Contents required for `dhclient`, `ping`, IP configuration, and DNS resolution for Linux network profile.
- `/dev`, `/proc`, `/sys` mount procedures required by VirtIO devices.
- Free of default keys, external service credentials, or personal configs.

Selecting Buildroot, BusyBox plus distribution tools, or alternative solutions impacts `dhclient` availability, licensing, and reproducibility. Solutions must be compared and decided by user prior to implementation; faster solutions cannot replace command requirements independently.

## 6. Verification Procedure

1. Download to `artifacts/downloads/`.
2. Compute SHA-256 prior to extraction, build, or execution.
3. Compare against official checksums; if official checksums are unprovided, record project locked values and initial acquisition source.
4. Stop immediately upon verification failure, omitting further file usage.
5. Build outputs may separately calculate SHA-256 for local acceptance reproduction.

Pre-filling uncalculated hash values in documentation is prohibited.

## 7. `.gitignore` Specification

Implementation `.gitignore` should accurately cover at least:

```text
/artifacts/
/build/
/cmake-build-*/
*.o
*.a
*.so
*.d
*.bin
*.img
*.ext4
*.pcap
*.log
core
core.*
```

Final rules must be reviewed to avoid accidentally excluding source code, configs, licenses, or test fixtures. Wildcard rules that might exclude committable inputs should be converted to directory-level explicit rules.

## 8. Licenses and Educational Use

- Project itself adopts the MIT License in repository root `LICENSE`.
- Third-party dependencies retain original licenses; MIT does not overwrite third-party rights.
- README independent, non-official, educational disclaimers do not substitute for open-source license compliance.
- Releasing scripts, patches, or configs requires retaining necessary copyright and license notices.

## 9. Acceptance Criteria

- Repository state check proves no external binaries are tracked.
- Clean environment produces identical versions and checksums per documentation.
- Built Linux/rootfs includes tools required for final acceptance.
- All sources and licenses are complete, with no faked or unverified checksum logs.
