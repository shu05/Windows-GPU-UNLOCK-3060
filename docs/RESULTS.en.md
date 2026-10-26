# RTX 3090 Windows case — October 20, 2026

**English** | [한국어](RESULTS.ko.md)

## Measurement conditions

- Same RTX 3090 24 GiB, NVIDIA Windows 617.14, CUDA Runtime API `12060`, cuBLAS `120600`.
- Same source/executable, 4096×4096 GEMM. Three processes per path; the summary is the median of each process's sample median.
- Earlier measurement: October 20, 2026, 01:25 KST (October 19, 16:25 UTC). Later measurement: October 20, 13:30 KST (04:30 UTC).
- Later measurement started at 0% GPU utilization, 0 MiB VRAM, 49°C and no other GPU processes.
- All three later runs exited with code 0 and empty stderr. Snapshot after the final run: SM 1980 MHz, 321.48 W, 71°C, software thermal slowdown inactive. This snapshot is not a thermal trace over the full run.

| Measurement | Earlier median, TFLOPS | Later median, TFLOPS | Later three-run range, TFLOPS |
|---|---:|---:|---:|
| FP32 | 24.505702 | 24.890281 | 24.726922–24.928998 |
| FP16 Tensor / FP32 accumulation | 70.548081 | 125.907816 | 124.969948–125.909659 |
| BF16 Tensor / FP32 accumulation | 70.529545 | 128.699728 | 128.438012–129.304171 |

[Machine-readable evidence](../evidence/benchmark.json) includes trial metadata, sample statistics, summaries and the measured executable's SHA-256. Personal paths and device UUIDs are excluded.

## Boot procedure and interpretation

The operator reported choosing Save Changes & Reset after the earlier unlock. For the later run, they confirmed **Boot Override → Windows Boot Manager without resetting** after success. Performance improved without replacing the driver.

The successful UEFI run, RUN06, records `SS0=88888888`, `SS1=00000008`, `FLR=1` and `FINAL status=0 ... clean=1`. See the [selected log excerpt](../evidence/uefi-success-excerpt.txt). The original PROM and complete USB backup are not public.

Loss of state after reset is a plausible explanation for the earlier result. There is no independently controlled untreated cold baseline or instrumentation across the full POST path. These results therefore do not establish a unique cause, identical behavior on every board or general driver compatibility.

These are historical USB results. The later internal headless path returned to
Windows with all monitors on the RTX and completed three Tensor benchmark
trials on the same system; see the [2026-10-24 evidence](../evidence/headless-handoff/README.md).
Long-term stability, real ComfyUI workflows, other cards/boards and repeated
automatic cold boots remain unverified.
