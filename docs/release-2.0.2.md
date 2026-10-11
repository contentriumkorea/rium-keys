# CONTENTRIUM Keys 2.0.2 verification

Date: 2026-10-11. Environment: Windows 11 x64, ordinary user session.

## Change

The repository previously retained Jamotong's upstream license but did not have
a root license for Contentrium's own work. This release makes the intended
permission explicit in both the repository and the installer.

- The root `LICENSE` is the standard MIT text, with Contentrium and the project
  URL in its copyright notice. It permits use, modification, redistribution and
  commercial use while requiring copyright and permission notices to be kept.
- The Korean README explains attribution without adding an advertising,
  on-screen logo, source-publication or created-content credit requirement.
- Setup displays and installs that same root license. It also installs
  `THIRD_PARTY_NOTICES.md` and the original Jamotong, LLVM, MinGW-w64 runtime and
  Winpthreads notices under `licenses/`.
- The Jamotong notice retains the Spleen glyph copyright and BSD terms. External
  copyright notices are preserved; the root MIT license does not replace them.
- Version/resource metadata is updated to 2.0.2. Input routing, composition,
  icons and installation transaction code are unchanged from 2.0.1. The
  supported direct upgrade is 2.0.1 to 2.0.2; see [older-version installation
  paths](installation.md#기존-버전에서-설치).

## Verification

- Rebuilt x64 and x86 installable DLLs passed **25 native suites, 1,506
  assertions, zero failures**. The existing unused-variable compiler warning in
  `code_input.c` remains. Unchanged controller binaries were retained.
- **34 installer contract checks** passed under both PowerShell 7 and Windows
  PowerShell 5.1. These tests use disposable HKCU values, not machine input
  profile changes.
- All **18 package payload hashes** match the manifest. All seven distributed
  license/notice files match their repository copies; the five external notice
  copies also match their upstream/toolchain originals.
- The root MIT permission/disclaimer body matches the standard upstream MIT
  text; only the copyright attribution differs.
- The exact Setup EXE completed `/S /PREFLIGHT` with exit **0** against this PC's
  existing 2.0.1 installation. Its extracted package integrity, previous install
  and original fallback state passed the read-only checks. This did not install
  2.0.2 or request elevation.
- Independent code review found no blocking issue in the license, attribution,
  packaging or upgrade configuration changes. Markdown local links and
  `git diff --check` were checked before publication.
- The legacy `update.json` remains byte-for-byte identical to the 2.0.1 release
  asset. Its RSA signature verifies against the repository's public key and its
  payload still targets legacy 1.1.1.

## Distributed file hashes

| File | SHA-256 |
| --- | --- |
| CONTENTRIUM-Keys-Setup.exe | `651429eda83e33232ed25fca0e7d2aebfaa8098ff81dbd91d7ba70f970f5e03e` |
| x64/RiumKeysInput.dll | `3a6c055bd51198fbcf3fd0537b743b4374d6686287c0f8f98612d0533d6da443` |
| x86/RiumKeysInput.dll | `a231665751fb11f2fa099587e31df16218571264eab85c7fddeb59a357d4f8d4` |
| update.json (legacy compatibility) | `dfb65c69eca116306a9f325618b3e3fe1fdc1168e09d23bc3ec91bffae11d8f0` |

## Scope

This is a licensing and packaging patch. No new full installation, reboot,
clean-PC or physical application-transition trial was performed for 2.0.2.
The unchanged installer transaction was exercised for [2.0.1](release-2.0.1.md).
Application evidence remains in [input-owner-validation.md](input-owner-validation.md);
this release makes no new application-compatibility claim. Native automatic
updates and code signing are not included.

## Public download policy update — 2026-10-11

At the owner's request, only the latest installer remains publicly downloadable.
The six earlier releases are retained as unpublished drafts with their assets.
The legacy `update.json` asset was removed from the public 2.0.2 release; its
unchanged copies remain with the archived releases. The hashes and legacy-feed
checks above record the original publication, not the current public asset list.
The 2.0.2 installer and checksum file are unchanged. Current installation
instructions link to the latest release and explain removing older native
versions before a fresh installation, without intermediate installer downloads.
