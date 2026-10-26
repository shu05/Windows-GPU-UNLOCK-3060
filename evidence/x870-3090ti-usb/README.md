# X870 / RTX 3090 Ti headless USB evidence

## Provenance and build scope

These are contributor-reported results from
[PR #10](https://github.com/impacifist/Windows-GPU-UNLOCK/pull/10), original revision
`7b578247a28ff66f2707bea5af0e3943aede7974`. They are not a new maintainer-operated
hardware test. The contributor's byte-identical rebuild claim and the EFI hashes
in `summary.json` refer to that original revision and its recorded builds.

The current source integrates later cold-precheck hardening from main. That
updated code has host/build validation, but no new X870 physical boot or recovery
report. Do not interpret the historical hashes, or the phrase "on this branch"
preserved in `summary.json`, as expected hashes or hardware validation of current
main. Raw excerpt and summary files remain the contributor's historical record.

## Recorded hardware runs

Sanitized results from one machine on 2026-10-25: Gigabyte X870 AORUS ELITE WIFI7 (BIOS F13d),
Ryzen 7 9800X3D, ASUS RTX 3090 Ti 24 GiB (GA102, `10de:2203` / `1043:8870`, VBIOS 94.02.a0.00.2c),
all monitors on the RTX. Route: headless wrapper/child from a FAT32 USB stick (`GU_USB_MEDIA`).

| Run | Boot | Pre-check | Entry SS0 / SS1 | Result |
|---|---|---|---|---|
| AUTO000000 | full shutdown | (wrapper before pre-check) | `0x40123070` / `0x3` | `FINAL status=0 clean=1`, one FLR, Windows direct |
| AUTO000001 | full shutdown | passed (cold) | `0x44123070` / `0x7` | same |
| AUTO000002 | Windows restart | passed (cold: this board resets the GPU) | `0` / `0` | same |

Every run ended with `SS0=0x88888888 SS1=0x8`, WPR2 cleared after FLR, BME off and no retained mappings.

| cuBLAS GEMM n=4096, TFLOPS (median of 3) | FP32 | FP16 tensor | BF16 tensor |
|---|---|---|---|
| Locked baseline | 32.30 | 80.35 | 80.98 |
| After AUTO000000 | 32.25 | 149.30 | 150.22 |
| After AUTO000002 (restart boot) | 32.20 | 149.05 | 150.22 |

Same driver (621.01), CUDA 12.6 / cuBLAS 12.6.0, power limit and executable before and after.
The board/card difference from the B650M profile that mattered: the card reports `0x5ff4` MiB
usable (not `0x6000`), so the layout moves down (`frts=0x5ff200000`) and FWSEC-FRTS lives at a
different PROM offset (`0x41758`, `0xea00` bytes). See [report-excerpt.txt](report-excerpt.txt)
and [summary.json](summary.json).

Excluded: PROM image, raw probe/graph logs, driver dumps, partition identifiers. Not established:
the pre-check skip branch and the BootOrder fallback on hardware, sleep/hibernate resume, long-term
stability and application performance, the internal-ESP route for this profile.
