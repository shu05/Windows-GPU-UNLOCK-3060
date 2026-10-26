# Issue #21 — Doug Sharp 인계 자료 검토와 첫 개선

- 검토일: 2026-10-26
- 원문: https://github.com/impacifist/Windows-GPU-UNLOCK/issues/21
- 개발 기준: main `e3f03cd9389195adcabda4743d8fc2922b32498c`
- 작성자의 문서·소스 설명과 로컬 소스 검토에 근거한다. 실기기 성공은 작성자
  보고이며 유지관리자의 독립 재현이 아니다.

## 다운로드와 보존

| 파일 | 크기 | SHA256 |
|---|---:|---|
| [인계 문서](https://github.com/user-attachments/files/33932589/RTX3090-Unlock-Windows-Port-Handoff.md) | 9354 bytes | `cb3742e5f30fb7f8efb503649902942354120704b37e5d1a334fe8095742690f` |
| [stage-3 소스](https://github.com/user-attachments/files/33932595/rtx3090-stage3-src.zip) | 40139 bytes | `6b1087e4ec42835475bfb3c5781f2d9a9b52865614bb75f27a9cdba7321624cc` |

원본과 압축 해제 소스는 ignored `research/results/issue21-handoff/`에 로컬 보존한다.
ZIP 엔트리의 추출 대상 경계를 확인했다. archive는 빌드·실행하지 않았다.
소스에는 `probe3.c`, `shim.c`, `memlib.c`, GNU-EFI Makefile, 호환 헤더와
`mod/` 코어가 있으며, 문서에서 언급한 `gpu-unlock-esp/src` 런처는 없다.
`probe3.c`와 `shim.c`는 GPL-2.0-only SPDX를 명시한다. 향후 코드 재사용 시
파일별 고지·원본 provenance를 확인하고 기존 고지를 보존해야 한다.
이번 변경은 해당 EFI 코드를 복사하거나 새 의존성을 추가하지 않는다.

## 작성자가 보고한 실험

- RTX 3090 Founders Edition, subsystem `10de:147d` (config dword `0x147d10de`).
- Ryzen 9 8945HS 미니PC, 화면은 iGPU에 연결. 정확한 보드 모델·BIOS는 미기재.
- Windows 11, NVIDIA 591.86, VBIOS `94.02.4B.00.0B`, 350 W.
- PyTorch 2.12.1/CUDA 13.0, n=8192, best-of-five, reduced-precision reduction 비활성.
- BF16 74.5 → 123.0 TFLOPS, FP32 26.4 → 26.1 TFLOPS (USB 실행 보고).
- ESP 런처 실행 BF16 123.5 TFLOPS. warm restart·resume·TDR 등은 미검증.

현재 프로젝트의 n=4096/30 sample/cuBLAS/중앙값 결과와 직접 순위 비교하지 않는다.
작성자의 카드와 기존 프로젝트 FE 프로필은 subsystem도 다르다. 신규 지원 프로필로
간주하거나 이름만 맞춰 기존 검사를 통과시키지 않는다.

## 소스에서 확인한 통합 전 검토점

1. `probe3.c:rd`, `shim.c:readl/writel/shim_memcpy_fromio`는 EFI MMIO 상태를
   확인하지 않거나 상위에 전파하지 않는다. 호출 성공·실패를 구분하는 기존 checked I/O가 필요하다.
2. `probe3.c:efi_main`은 첫 3090을 선택하고 복수 대상 모호성을 거부하지 않는다.
   `pci_request_region`과 aperture helper는 no-op이며, 화면 소유권·드라이버 코드
   fingerprint를 검증하는 현재 프로젝트의 모델을 대체하지 못한다.
3. `shim.c:pool_init`은 Map 실패/부분 매핑/주소 범위 거부 시 FreeBuffer를 호출한다.
   유효한 매핑이 남는 경우의 Unmap·실패 처리를 분리해 검증해야 한다.
4. `shim_dma_teardown`은 Unmap/FreeBuffer 반환 상태를 확인하지 않는다.
   `efi_main`은 오류 경로에서도 최종적으로 EFI_SUCCESS를 반환한다. 실패 후
   펌웨어/OS 복귀가 안전한지는 별도의 containment·cleanup 증거가 필요하다.
5. firmware request는 파일을 읽지만 현재 프로젝트와 같은 고정 hash 계약은 없다.
   문서 자체가 610.43.03 다섯 파일의 정확한 추출 provenance를 미정리로 명시한다.
6. NTFS 설치에서는 로그 기록이 실패해도 계속 진행한다고 문서가 설명한다.
   런처 소스가 없어 로드 대상 결합·실패 후 Windows 진입을 소스 검토할 수 없다.

위 내용은 정적 검토 결과다. 실제 손상·hang을 재현했다는 의미가 아니다.
현재 EFI 실행 경로·프로필·selector·부팅 설정은 수정하지 않는다.

## 첫 반영: 동일 조건 벤치마크 비교

기여의 유용한 부분인 전후 측정과 FP32 대조군을 기존 도구에 반영한다.

- `benchmark/run-benchmark.ps1`: build manifest/runtime/GPU 설정 snapshot과
  매 trial 뒤 설정 재검사, 정상 완료 시 raw 파일 hash record.
- `benchmark/compare_results.py`: 오프라인 raw 비교, 동일 장비 사용자 확인,
  라이브러리·실행물·protocol 일치 확인, FP32 변화와 median-of-three 배율.
- 불완전/조건 불일치/수정된 결과를 거부한다. 과거 보고서를 새 증거로 소급 변환하지 않는다.
- 합성 테스트는 소프트웨어 거부 경계 검증이며 실기기 언락·성능 증거가 아니다.

사용법과 한계는 [한국어 벤치마크 문서](../BENCHMARK.ko.md)를 참고한다.

## 이번 변경의 검증 근거

| 기준 | 결과 | 근거 | 한계 |
|---|---|---|---|
| 다운로드 원본 | pass | 원문/ZIP 크기와 SHA256 기록, archive 경계 검사, ignored 경로 확인 | 첨부 EFI 성공을 독립 검증하지 않음 |
| 비교와 거부 경계 | pass | `python -m unittest discover -s benchmark -p test_compare_results.py -v`, 17 tests, exit 0 | 모든 측정은 합성 데이터 |
| runner 연계 | pass | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\test-runner.ps1`, 4 scenarios, exit 0 | managed stub + mock nvidia-smi, 실제 CUDA 없음 |
| 기존 Python 도구 | pass | `python -m unittest discover -s tools -p 'test_*.py'`, 14 tests, exit 0 | Python 도구 범위 |
| 기존 headless Python | pass | `python -m unittest discover -s research/headless -p 'test_*.py'`, 13 tests, exit 0 | EFI 실행·C suite 검증 아님 |
| PowerShell 구문 | pass | 변경 runner와 test-runner AST 오류 0, 실제 runner test는 Windows PowerShell 5.1 실행 | 실기 NVIDIA telemetry 미확인 |
| diff 검사 | pass | `git diff --check`, exit 0 | commit/push/배포하지 않음 |

runner 통합 테스트는 preflight 무작업, 정상 결과의 Python reader 수용,
telemetry 예외와 설정 변화 시 완료 기록 부재를 확인한다. 테스트 harness의
초기 scope/mock 오류를 수정한 후 위 네 시나리오가 통과했다.
`benchmark/build/runner-tests-*/`에는 ignored 합성 실행 파일·결과만 남긴다.
원본 archive와 firmware source를 빌드하거나 실행하지 않았고, 실제 GEMM·실주문·
부팅 설정 변경·ESP staging을 수행하지 않았다. 새로운 GPU 지원을 추가한 것이 아니다.

## 별도 승인 후 실제 벤치마크 확인 — 2026-10-26

위 합성 검증 이후, 사용자가 현재 언락 상태에서 실행을 요청했다. 첫 실행은 GPU
부하 기준에 걸려 GEMM 전에 중단됐다. 사용자의 재실행 요청 후 기존 CUDA 12.6
패키지를 사용해 현재 benchmark 소스를 빌드하고, 새 runner가 세 trial을 완료했다.
EFI·언락 코드는 실행하지 않았고 GPU 설정이나 부팅 설정을 바꾸지 않았다.

- 로컬 결과: `benchmark/results/20261026-045953-675/` (ignored).
- CUDA runtime 12060, cuBLAS 120600, NVIDIA driver 617.14, RTX 3090 `0000:01:00.0`.
- 기록된 기존 power limit은 370 W. 비교 설정은 세 trial 동안 유지됐다.
- FP32 23.600927 / FP16 126.454602 / BF16 129.179720 TFLOPS (세 trial 중앙값).
- 실제 PowerShell JSON/BOM 파일과 completion hash를 Python `load_run`이 수용했다.
  CUDA 빌드, runner 및 실제 evidence reader 확인은 모두 exit 0.
- 측정 전 64°C, 마지막 telemetry는 80°C와 software thermal slowdown Active.
  부하·열에 따른 변동이 있으므로 성능 비교나 최대 처리량의 근거로 사용하지 않는다.
- 이 실행의 locked baseline은 없으며 향상률·신규 GPU 지원·안정성을 주장하지 않는다.

이 결과는 도구의 실기기 측정·저장 연계 확인이다. 원시 결과나 시스템별 바이너리는
공개 commit에 포함하지 않는다.

## 이후 작성자에게 확인하면 좋은 질문 — 아직 전송하지 않음

- 인계 문서의 `gpuload` 런처 소스와 정확한 빌드 revision을 제공할 수 있는가?
- 정확한 미니PC/보드 모델과 BIOS 버전, 기존 sanitized transaction·cleanup 로그가 있는가?
- 다섯 펌웨어 파일의 package origin, 추출 과정과 SHA256이 남아 있는가?
- GNU-EFI 및 mod 소스의 upstream revision과 파일별 수정 이력을 확인할 수 있는가?

기존 자료만 요청하는 질문이며 새 하드웨어 실행이나 보안 설정 변경은 필요하지 않다.
