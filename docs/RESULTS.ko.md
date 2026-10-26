# RTX 3090 Windows 성공 사례 — 2026-10-20

[English](RESULTS.en.md) | **한국어**

## 측정 조건

- 같은 RTX 3090 24GiB, NVIDIA Windows 617.14, CUDA Runtime API `12060`, cuBLAS `120600`.
- 같은 소스/실행물의 4096×4096 GEMM. 각 상태에서 3개 프로세스, 각 프로세스의 sample 중앙값을 다시 중앙값으로 집계.
- 이전 측정: 2026-10-20 01:25 KST. 이번 측정: 2026-10-20 13:30 KST.
- 이번 측정 시작 시 GPU 사용률 0%, VRAM 0MiB, 49°C, 다른 GPU 프로세스 없음.
- 이번 3회 모두 종료 코드 0, stderr 빈 파일. 마지막 측정 후 snapshot: SM 1980MHz, 321.48W, 71°C, software thermal slowdown 비활성. snapshot은 전체 구간의 thermal trace가 아닙니다.

| 측정 | 이전 중앙값 TFLOPS | 이번 중앙값 TFLOPS | 이번 3회 범위 TFLOPS |
|---|---:|---:|---:|
| FP32 | 24.505702 | 24.890281 | 24.726922–24.928998 |
| FP16 Tensor / FP32 누산 | 70.548081 | 125.907816 | 124.969948–125.909659 |
| BF16 Tensor / FP32 누산 | 70.529545 | 128.699728 | 128.438012–129.304171 |

[기계 판독 가능한 원자료](../evidence/benchmark.json)에는 각 trial의 메타데이터·sample 통계와 summary를 제공합니다. 개인 경로·장치 UUID는 포함하지 않습니다. 측정한 실행물의 SHA-256도 기록합니다.

## 부팅 절차와 증거의 의미

사용자는 이전 언락 실행 뒤 BIOS에서 Save Changes & Reset을 선택했다고 보고했습니다. 이번에는 언락 성공 후 **Boot Override → Windows Boot Manager로 재부팅 없이 진입했다**고 확인했습니다. 드라이버 교체 없이 성능 향상이 나타났습니다.

성공 부팅의 UEFI 로그 RUN06은 `SS0=88888888`, `SS1=00000008`, `FLR=1`, `FINAL status=0 ... clean=1`을 기록합니다. [필요 구간 발췌](../evidence/uefi-success-excerpt.txt). 원본 PROM과 전체 USB 백업은 공개하지 않습니다.

이를 근거로 '언락 이후 Reset으로 상태가 소실된 것이 이전 실패의 원인'이라는 해석이 유력합니다. 다만 독립적으로 통제한 미적용 cold baseline이나 POST 전 구간 계측은 없으므로, 유일한 원인·모든 보드의 동일 동작·드라이버 일반 호환성을 확정하지 않습니다.

이 문서는 과거 USB 측정 기록입니다. 이후 같은 시스템에서 모든 모니터를 RTX에 연결한
내부 headless 언락·Windows 복귀와 3회 Tensor 측정을 확인했습니다.
[2026-10-24 근거](../evidence/headless-handoff/README.md)를 별도로 참고하세요.
장시간 안정성, 실제 ComfyUI 워크플로, 다른 카드/보드, 반복 자동 콜드부팅의 신뢰성은
여전히 검증하지 않았습니다.
