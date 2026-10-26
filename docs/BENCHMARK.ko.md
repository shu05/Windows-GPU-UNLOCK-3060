# 같은 조건으로 성능 비교하기

[English](BENCHMARK.en.md) | **한국어**

측정 소스는 상위 프로젝트의 4096×4096 cuBLAS GEMM을 사용합니다. FP32 pedantic, FP16/BF16 입력과 FP32 누산·출력, 정밀도별 2초 warmup과 30 sample입니다. 빌드와 실행에 `-Card 3090|3090ti|3070ti`를 지정해 RTX 3090(기본값)·3090 Ti·3070 Ti를 선택합니다. 각 실행물은 해당 모델이 PCI `0000:01:00.0`에 있어야 합니다. 이는 측정 대상 선택이며 EFI 호환성 판정이 아닙니다. 결과 검증은 원본처럼 출력 첫·마지막 원소 확인에 한정됩니다.

CUDA 12.6 redist는 NVIDIA 공식 서버에서 해시·크기를 확인해 프로젝트 폴더에만 준비합니다. 드라이버·ComfyUI·시스템 PATH를 변경하지 않습니다. 다운로드 용량과 디스크 공간이 필요합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\run-benchmark.ps1 -PreflightOnly
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\run-benchmark.ps1
```

실측 빌드는 VS2019 BuildTools MSVC14.29를 사용했습니다. 다른 위치는 `build.ps1 -VcVars <vcvars64.bat>`로 지정합니다. CUDA 12.6과 호환되는 MSVC를 사용하세요. setup은 기존 packages 폴더를 덮어쓰지 않으므로 새 디렉터리에서 처음 한 번 실행합니다.

다른 지원 측정 모델은 빌드·메타데이터 확인·측정에 같은 값을 지정합니다.

```powershell
.\benchmark\build.ps1 -Card 3090ti
.\benchmark\run-benchmark.ps1 -Card 3090ti -PreflightOnly
.\benchmark\run-benchmark.ps1 -Card 3090ti
```

runner는 메타데이터 확인을 실행하기 전부터 선택한 실행물의 hash가 build manifest에
정확히 한 번 포함됐는지 검사합니다. 다른 모델의 manifest만으로 남아 있는 실행물을
신뢰하지 않습니다. 필요한 경우 해당 모델로 다시 빌드하세요.
manifest에는 선택한 모델의 소스·실행물·링크 산출물과 공유 DLL만 기록합니다.
다른 모델을 빌드하면 활성 manifest가 교체되므로 이전 모델로 돌아갈 때도 다시 빌드합니다.

ComfyUI·영상·녹화·다른 GPU 작업을 저장하고 종료한 뒤 측정합니다. 시작 조건은 GPU 사용률 5% 이하, VRAM 2048MiB 이하, 온도 65°C 이하입니다. 스크립트는 3회 실행을 새 `benchmark/results/시각/` 폴더에 저장합니다. 다른 프로세스를 강제 종료하지 않습니다.

언락 이전과 이후에 동일 실행물·드라이버·전력 제한·오버클럭·온도 조건을 사용하세요. 이 도구는 GPU 설정을 바꾸지 않습니다. FP16/BF16 Tensor 향상이 게임·영상·ComfyUI 전체 시간의 같은 비율 향상을 의미하지 않습니다.

## 저장된 전후 결과 비교

새 runner는 `comparison-context.json`에 빌드 파일 해시, CUDA/cuBLAS 정보,
GPU 모델·PCI 위치·드라이버·전력 제한·최대 SM/메모리 클럭을 보존합니다.
매 trial 뒤 GPU 설정을 다시 확인하며, 모두 정상 종료한 경우에만 context와
세 raw trial의 SHA256을 담은 `comparison-complete.json`을 기록합니다.
해시는 실수로 바뀐 파일을 감지하며 서명된 실기기 증거는 아닙니다.

Python 3.9 이상으로 이 버전 runner의 두 완료 결과를 비교합니다.

```powershell
python .\benchmark\compare_results.py .\benchmark\results\BEFORE .\benchmark\results\AFTER --same-machine-confirmed
```

`--same-machine-confirmed`는 같은 실제 PC·GPU·OS를 사용했고 수동 튜닝이
변하지 않았으며 부하·온도 조건도 비교 가능함을 사용자가 확인했다는 뜻입니다.
모델명과 PCI 위치만으로 같은 장비임을 증명할 수는 없습니다. 최대 클럭은 실제
동작 클럭이나 모든 오버클럭 설정을 나타내지 않으므로 telemetry도 확인하세요.
비교 명령은 기존 파일만 읽고 JSON을 출력하며 GPU 실행이나 파일 쓰기를 하지 않습니다.

실행물·설정·라이브러리 정보, 4096 행렬/30 sample/2초 warmup 조건과 결과 확인,
세 trial이 일치해야 합니다. provenance 누락, 부분 실패, 파일 변경, 전력·클럭 정보
부재, 무한대/NaN, 측정 조건 차이는 종료 코드 2로 거부합니다. 과거 결과에 context나
완료 기록을 소급해 만들어 기존 증거를 승격하지 마세요.

오프라인 비교도 같은 세 GPU 모델과 해당 모델의 실행물 hash를 확인합니다.
서로 다른 모델 간 비교는 거부하며, 다른 보드·BIOS·화면 경로의 EFI 지원을 추가하지 않습니다.

출력은 세 trial의 중앙값 TFLOPS, 개별 trial, 전후 배율과 FP32 대조군 변화를
함께 제공합니다. locked/unlocked 상태나 언락에 의한 인과관계를 판정하지 않습니다.
수치 정확성 검사는 기존처럼 첫·마지막 원소만 확인하며 안정성·앱 성능을 증명하지 않습니다.
