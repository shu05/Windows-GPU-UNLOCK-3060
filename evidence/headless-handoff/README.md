# Headless internal handoff: one verified RTX 3090 system

On 2026-10-24 (KST), the user selected the separate headless EFI on an internal
dedicated ESP. No U/T/Enter confirmation was required. The machine returned to
Windows and completed three CUDA 12.6 / cuBLAS 12.6 benchmark trials.

| Measurement | Median TFLOPS | Minimum | Maximum |
|---|---:|---:|---:|
| FP32 pedantic | 22.845572 | 22.839739 | 24.615816 |
| FP16 Tensor / FP32 accumulate | 124.969948 | 122.797562 | 127.765571 |
| BF16 Tensor / FP32 accumulate | 128.931534 | 127.644062 | 129.867183 |

The board was ASUS TUF GAMING B650M-PLUS WIFI BIOS 3057, CPU Ryzen 7 7800X3D,
GPU RTX 3090 FE 24 GiB (`10de:2204 / 10de:1454`), Windows 11 Home 26200,
NVIDIA driver 617.14. All monitors were connected to the RTX. The CPU still
contains an iGPU: this is evidence of **no active iGPU display/GOP**, not
validation on CPUs without an iGPU or on arbitrary boards.

The private wrapper capture shows one physical RTX GOP before release and zero
physical/virtual GOPs after release. System TextOut/Input protocols remained.
Both captures completed, all preparation/child return/pre-Windows verification
statuses were zero. The child report recorded one transaction with
`SS0=88888888`, `SS1=8`, `BME=0`, `pending=0`, `maps=0`, `FLR=1`, `quiesced=1`,
`fault=0`, `retained=0`, `clean=1`.

The wrapper's final line is before Windows StartImage. Evidence of completed
Windows handoff therefore also includes the observed Windows boot time
03:04:25 KST, the user's return to Windows, and the Windows benchmark runs.

- Wrapper log SHA256: `460f77d96d188145d3b70d57a5f7f716e05a27994cc9791cda5ef1e4482e6f77`.
- Child report SHA256: `d576e5ee33a0e762a661e4518b0385b2d04741c162e9a23924211b22454e0e13`.
- Successful wrapper EFI SHA256: `6839bbc9987fa26b794dc6e2ffe98791642ac6e21ef01c388da9fffef3a878a3`.
- Successful child EFI SHA256: `4dce4c2d06e4950af39cd20bdb267f888df8e3563cbc7011be5aeffcfd3ca469`.

## v0.3.1 follow-up

The v0.3.0 wrapper rejected the local firmware graph because it exposed three
RTX display children while the preflight required exactly two. v0.3.1 discovers
and validates up to four unique display children. On 2026-10-24, the rebuilt
wrapper log recorded successful preparation, automatic child return, final
verification and Windows start (`HLS000004.TXT`); the user observed the display
turn off during release and return in Windows. The installed wrapper SHA256 was
`bf8d7e52462e354a6258a4c2b1d76fc946e6310392c67725dc89ee1f8684623d`; the child
was unchanged. This confirms the updated boot path on the same machine. No new
performance benchmark or detailed child transaction report was collected for
this follow-up, so the earlier TFLOPS table is not a measurement of v0.3.1.

Raw boot captures, addresses, partition/boot-entry GUIDs, machine configuration,
PROM, firmware and generated EFI images are not published. The JSON trial
records contain only benchmark metadata and numerical results. The parser's
offline result alone does not authenticate the captures or authorize execution.

Each GEMM used matrix size 4096, 30 samples and two seconds of warmup; all
reference checks passed. The benchmark runner's initial idle/memory/temperature
guard passed. This is a single boot with three performance trials, not three
independent boots. There is no contemporaneous controlled locked baseline, so
these numbers do not establish an unlock speedup percentage, game/ComfyUI
improvement, long-term stability, sleep/resume behavior, or USB headless support.

See [summary.json](summary.json) and [trial 1](trial-1.json),
[trial 2](trial-2.json), [trial 3](trial-3.json).
