# CONTENTRIUM Keys - third-party notices

Contentrium's original code and documentation are provided under the root
[MIT License](LICENSE). Third-party material retains its own copyright notices
and license terms; the root license does not replace those terms.

The license files linked below are included in both this repository and the
installed application. Keep the applicable notices with copies or substantial
portions of the software that you redistribute.

## Jamotong

- Project: https://github.com/rubidus-api/jamotong_ime
- Version: 0.73.1, commit `aa78c1a328bab5d193f5f9932c5f824d5fb222d5`.
- Copyright (c) 2026 rubidus-api.
- License: [original MIT text](licenses/jamotong/LICENSE).
- [Original copyright and third-party notices](licenses/jamotong/COPYRIGHT.md).

CONTENTRIUM Keys modifies the native TSF engine for its input ownership/routing,
inline composition, branding and installation. The upstream copyright and
license text are retained unchanged. The vendored source is under
`native-ime/third_party/jamotong`.

The retained upstream notices include the **Spleen 5x8** bitmap glyph attribution
and complete BSD 2-Clause terms, copyright (c) 2018-2026 Frederic Cambus.
Upstream dictionaries and their data files are not distributed in the current
CONTENTRIUM Keys installer; their descriptions remain in the unmodified
upstream notice for provenance.

## Native runtime components

The native input DLLs are built with the pinned LLVM-MinGW
`20261006-ucrt-x86_64` toolchain. The x64 build statically links the C++ runtime.
Runtime components retain their original terms:

- [LLVM license with LLVM exceptions](licenses/LLVM-LICENSE.txt), including the
  legacy notices present in the toolchain's original license file.
- [MinGW-w64 runtime copyright and license notices](licenses/MinGW-w64-runtime.txt).
- [Winpthreads license](licenses/Winpthreads.txt).

These are copies of the license texts supplied with that exact toolchain, not
new terms imposed on those components. The compiler toolchain itself is not
included in the installer. Microsoft Windows APIs, system fonts and runtime
components remain subject to their respective terms; Windows font files are
not bundled.

## Attribution example

An accompanying README or About page may credit the project as:

> CONTENTRIUM Keys by Contentrium - https://github.com/contentriumkorea/rium-keys

This short credit is helpful, but it does **not** replace the full copyright and
license notices required for redistribution. MIT does not require a permanent
on-screen logo, public advertising credit, source publication, or credits in
videos/documents created using the application. These explanations do not add
conditions to the MIT text in `LICENSE`.
