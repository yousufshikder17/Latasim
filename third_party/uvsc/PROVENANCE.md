# third_party/uvsc — provenance

| | |
|---|---|
| **What** | Keil Application Note 198, *Using the uVision Socket Interface* (KAN198 v1.1) |
| **Publisher** | Arm (document service), published 2025-01-16; content revised July 2018 |
| **Metadata** | `https://documentation-service.arm.com/documentation/kan198/latest` |
| **Download** | `https://documentation-service.arm.com/static/678911ba3f2a9a07789e22e7` → `apnt_198.zip` |
| **sha256** | `04b06be4a08e1f8ccdfb7b664f17af216292eaed05309e5eb0927908176478c7` |
| **Size** | 14,466,524 bytes |
| **Downloaded** | 2026-09-25 (Phase 0, Task 5) |

## Extracted from the zip

- `src/UVSC/UVSC_DLL/UVSC_C.h`: the UVSC API header. At build time, `scripts/gen-uvsc-min.ps1` generates the spike's declarations from it into `build/generated/uvsc_min.h`. It reads `apnt_198.zip` directly, so extraction is optional.
- `doc/html/`: API documentation.

## Missing from the official zip

- `UVSOCK.h`, which `UVSC_C.h` includes. Its struct documentation pages are empty. See `docs/phase0/uvsc-results.md`.

## Licence

The header states it "may only be used under the terms of a valid, current, end user licence from KEIL", and Keil's `license_terms/redistributables.txt` does not list UVSC files. So nothing from this package is committed: `.gitignore` excludes everything here except this note, and the spike's declarations are generated locally.

## Setup for a new clone

1. Download `apnt_198.zip` from the URL above and check its sha256.
2. Put it at `third_party/uvsc/apnt_198.zip`, or pass `-DUVSC_SOURCE=<path to the zip or UVSC_C.h>` to CMake.
