# Internal manual handoff evidence — 2026-10-23

One RTX 3090 / ASUS TUF GAMING B650M-PLUS WIFI BIOS 3057 / AMD iGPU system.
The operator reported pressing U once and entering Windows directly. Local FULL04
and RUN07 records showed preparation success, original unlock return 0, pre-close
verification success, SS0=88888888 / SS1=8, transaction=0 and clean=1.
Raw boot graphs/PROM/BIOS and machine partition identifiers are not distributed.

The unchanged CUDA 12.6 / cuBLAS 12.6 benchmark ran three times while other GPU
activity was present (about 10–11% before measurement). `summary.json` and trial
JSON files preserve the measured values; this is not a controlled idle baseline.
FP16 median: 125.907816 TFLOPS. BF16 median: 128.931534 TFLOPS.

The following local-source builds were reproduced byte-for-byte from the new
public tree using the same private local partition configuration. They are not
portable download hashes: changing partition GUIDs changes the generated EFI.

| Artifact | SHA256 | Hardware evidence |
| --- | --- | --- |
| Manual unlock child | `5d45abff1cffff1aeaf329725b94ae5f33d7b7c3647afe37812e60f713bce963` | Success in manual handoff run |
| Manual internal wrapper | `9729e61a9b91f883f826e8f2bfb4f71290378dbdeffce42b4ca55a25cfac1cb8` | Windows handoff and Tensor measurement verified |
| Automatic unlock child | `b512bd7f17dd8dc37e94dffe88c2646693e86f95852214002ca7eb88b3b06507` | Host tests/local install only; automatic firmware boot unverified |
| Automatic internal wrapper | `6b6beb8608b7e70d7b424149b948bab14f4a0e80c6d73ac920e9788d3a589e87` | Host tests/local install only; automatic firmware boot unverified |

Software checks: actual app/core automatic transaction and cancellation/error
paths; original manual entry-latched integration; full release 9,842 checks;
full flow 39; console 291; PE positive and malformed machine/subsystem/import
rejections for both automatic EFI files. Transaction policy tests inject each
partial mutation and verification failure; public installer copy attribution is
also tested without firmware access. The public registration/promotion adapter
itself has not been exercised against real firmware.

These observations do not prove arbitrary RTX3090/BIOS compatibility, automatic
firmware operation, application speedups or long-term stability.
