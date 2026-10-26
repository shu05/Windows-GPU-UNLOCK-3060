# 내부 디스크에서 언락 → Windows 부팅하기

> 이 문서는 **AMD 화면을 유지하는 기존 경로**입니다. RTX 화면으로 시작해 화면 없이
> 자동 언락하는 v0.3.0 경로는 [Headless 안내](HEADLESS.ko.md)를 따르세요.
> 아래 자동판 미검증 설명은 기존 AMD 프로필에 관한 기록입니다.

[English](INTERNAL-BOOT.en.md) | **한국어**

## 이 가이드의 범위

`internal-boot/`는 ASUS TUF GAMING B650M-PLUS WIFI **BIOS 3057**, RTX 3090
`10de:2204 / subsystem 10de:1454`, AMD 내장그래픽 `1002:164e / 1043:8877`의
검증된 조합을 위한 실험 코드입니다. 다른 3090·BIOS·보드로 지원을 확장하지 않았습니다.
GPU 신원뿐 아니라 PROM·BAR·PCI 위치·펌웨어 드라이버 코드까지 검사합니다.
오류가 나온다고 검사를 지우거나 값을 바꾸지 마세요.

2026-10-23 실제 확인: **내부 EFI → U 한 번 → Windows 직접 진입 → FP16 125.91,
BF16 128.93 TFLOPS**. 별도 재부팅이 없었고 3090 출력 케이블을 연결한 상태였습니다.
측정 중 약 10~11%의 다른 GPU 활동이 있었으며 각 3회 중앙값입니다.
[원자료](../evidence/internal-handoff/summary.json).

**U 없는 자동 빌드는 host 모의 검증과 로컬 설치까지 완료했지만 첫 실제 자동 부팅은
아직 미검증입니다.** 공개 `Install.ps1`의 다른 PC에서의 실제 등록·승격도 미검증입니다.
수동 인계 성공을 먼저 확인하고 자동 기능을 선택하세요. 장기 안정성·절전 복귀·다른
GPU/보드 성공을 입증한 릴리스가 아닙니다.

루트 `unlock/`와 `release/EFI/BOOT/BOOTX64.EFI`는 기존 USB 수동판입니다.
새 코드는 **`internal-boot/` 아래**에 있습니다. 자동 EFI는 파티션에 맞춰 직접 빌드합니다.
릴리스 ZIP의 기존 USB EFI를 자동 부팅판으로 사용하지 마세요.

## 1. 준비물

- 위 하드웨어/BIOS 조합, Windows 11 x64 UEFI, Secure Boot 꺼짐.
- 내장그래픽에 연결한 화면과 키보드. 부팅 중 펌웨어 마우스/커서가 잠시 멈출 수 있습니다.
- Windows C:가 완전히 복호화되고 BitLocker 보호가 꺼진 환경. 스크립트가 암호화
  설정을 변경하지는 않습니다. 복구 키·기존 파일·부팅 복구 수단은 사용자가 보관합니다.
- 기존 Windows EFI·복구 파티션과 **별개인 1GiB 이상 FAT32 EFI 파티션**.
- [펌웨어 5개](ASSETS.ko.md), Python 3.9+, Git,
  [LLVM 20.1.8의 clang/lld-link](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8).
- 모의 검사를 직접 실행할 개발자에게는 MSVC/Windows SDK와 비공개 로컬 fixture도 필요합니다.

펌웨어·원본 GPU PROM·BIOS 이미지·CUDA DLL은 릴리스에 포함하지 않습니다.
VBIOS를 플래싱하거나 드라이버를 다운그레이드하는 절차가 아닙니다.

## 2. 별도 파티션 준비

이미 별도 FAT32 ESP가 있으면 새로 만들지 않습니다. **Windows가 쓰는 100MB/기존
EFI나 복구 파티션을 삭제하거나 포맷하지 않습니다.** 이 저장소 스크립트는 파티션을
축소·생성·포맷하지 않습니다.

필요한 경우 Windows 디스크 관리에서 선택한 데이터/Windows 볼륨을 1024MB 축소하고,
그 새 빈 공간에 FAT32 단순 볼륨을 만들어 사용하지 않는 문자(예: U:)를 부여합니다.
표시 이름을 `GPUUNLOCK`으로 지정하면 구분하기 쉽습니다. 기존 데이터 볼륨을
포맷하는 작업과 혼동하지 마세요.

관리자 PowerShell에서 먼저 **읽기 전용으로** 확인합니다.

```powershell
Get-Partition | Format-Table DiskNumber,PartitionNumber,DriveLetter,Size,GptType,Guid,IsSystem
Get-Volume -DriveLetter U | Format-List DriveLetter,FileSystemLabel,FileSystem,Size
```

U:가 방금 만든 1GiB FAT32 전용 볼륨이고 `IsSystem=False`임을 확인한 뒤에만,
아직 EFI 형식이 아니라면 해당 새 파티션의 GPT 형식을 ESP로 설정합니다.

```powershell
Set-Partition -DriveLetter U -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
```

기존 시스템 ESP는 `IsSystem=True`인 항목입니다. 그 **Guid를 따로 기록**합니다.
파티션 번호/디스크 번호는 사람마다 다릅니다. 예시 번호를 추측해 사용하지 마세요.

## 3. 소스 받기와 로컬 설정

관리자 PowerShell에서 실행합니다. 릴리스 소스 ZIP을 풀었다면 clone 단계는 생략합니다.

```powershell
git clone https://github.com/impacifist/Windows-GPU-UNLOCK.git
cd Windows-GPU-UNLOCK
```

아래 `WINDOWS-ESP-GUID-HERE`를 2단계에서 기록한 **기존 Windows ESP Guid**로 바꿉니다.
UnlockDriveLetter는 새 전용 ESP의 문자입니다. 둘이 같으면 거부됩니다.

```powershell
.\internal-boot\Configure.ps1 -UnlockDriveLetter U -WindowsEspGuid 'WINDOWS-ESP-GUID-HERE'
```

이 명령은 모델/BIOS/파티션을 검사하고 무시되는 로컬 설정 파일만 만듭니다.
파티션 구조·부팅 순서·EFI 파일은 바꾸지 않습니다. 생성된 `local-config.json`과
`boot/platform-config.h`는 개인 파티션 식별자를 포함하므로 GitHub에 올리지 마세요.

## 4. 수동·자동 EFI 빌드

설치한 LLVM의 실제 경로를 지정합니다. Python은 `python`으로 실행할 수 있어야 합니다.

```powershell
.\internal-boot\Build.ps1 `
  -Clang 'C:\TOOLS\LLVM20\bin\clang.exe' `
  -LldLink 'C:\TOOLS\LLVM20\bin\lld-link.exe'
```

수동 child·수동 wrapper·자동 child·자동 wrapper 네 개가 만들어지고 PE 형식과 해시를
검증합니다. 빌드는 GPU 언락이나 부팅 항목 설치를 실행하지 않습니다.

## 5. 파일 설치와 수동 항목 등록

소유한 펌웨어 5개를 예를 들어 `C:\LOCAL-FIRMWARE\assets`에 준비합니다.
`unlock/assets.json`과 파일명·크기·해시가 같아야 합니다.

먼저 파일만 배치합니다. 기존 파일이 다르면 덮어쓰지 않고 거부합니다.

```powershell
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets'
```

전용 ESP의 새 파일:

```text
EFI/WindowsGPUUnlock/MANUAL.EFI
EFI/WindowsGPUUnlock/UNLOCK.EFI
EFI/WindowsGPUUnlock/AUTO.EFI
EFI/WindowsGPUUnlock/UNLOCK-AUTO.EFI
RTX3090-UNLOCK/ASSETS/<5 files>
```

수동 시험 항목을 등록합니다. Windows는 계속 첫 번째입니다.

```powershell
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets' -RegisterManual
```

BCD 백업 위치는 `%ProgramData%\WindowsGPUUnlock\Backups\<timestamp>`입니다.
`internal-boot/install/state.json`을 보존하세요. 이 공개 설치 스크립트는 host 정책 검사와
정적 검토를 거쳤지만 다른 PC에서 실제 실행한 것으로 표현하지 않습니다.

## 6. 먼저 수동으로 성공 확인

1. 모든 작업을 저장하고 Windows를 완전히 종료한 뒤 전원을 켭니다.
2. ASUS F8에서 **Windows GPU UNLOCK Manual**을 선택합니다.
3. 처음 5초는 기다립니다. 이때 Esc를 누르면 하드웨어 변경 없이 Windows만 부팅합니다.
4. 준비 검사가 끝나고 **원본 언락 앱이 나온 뒤 U를 한 번** 누릅니다.
5. 성공하면 BIOS를 거치지 않고 Windows로 직접 넘어갑니다. 중간에 Reset을 하지 않습니다.
6. [같은 벤치마크](BENCHMARK.ko.md)로 FP16/BF16을 확인합니다. 로그상 성공만으로
   성능 향상까지 성공했다고 판단하지 않습니다.

정지하면 마지막 화면 전체를 찍고 전원을 완전히 끈 뒤 F8 → Windows Boot Manager로
복귀합니다. 같은 부팅에서 언락을 반복하거나 실패 검사를 우회하지 않습니다.

## 7. 선택 사항: U 없이 매번 자동 부팅

6단계를 실제로 통과한 사람만 실행합니다. `-ConfirmedManualSuccess`는 그 사실에 대한
명시적 확인이며 스크립트가 실제 GPU 시험을 대신했다는 뜻은 아닙니다.

```powershell
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets' `
  -EnableAutomaticBoot -ConfirmedManualSuccess
```

이 명령은 기존 수동 항목을 자동 EFI에 연결하고 **언락 → Windows** 순서로 변경하며,
Windows Fast Startup의 `HiberbootEnabled`를 0으로 설정하고 이전 값을 보관합니다.
Windows의 원래 항목과 기존 EFI는 보존하고 자동 재부팅하지 않습니다.

다음 전체 부팅부터 처음 5초 Esc로 Windows만 진입할 수 있습니다. 기다리면 준비 검사,
추가 3초 Esc 취소 구간, 자동 언락 1회, Windows 직접 진입으로 이어집니다. **U 입력은 없습니다.**
추가 3초 취소는 드라이버 해제 후이므로 언락이 취소되면 Windows로 강행하지 않고 정지합니다.
어느 단계든 실패하면 자동 재시작·재시도·Windows 강행 없이 멈춥니다.

첫 자동 부팅의 로그·성능을 직접 확인하세요. 재시작이 cold-state 검사에 걸리면 전원을
완전히 끈 뒤 다시 켭니다. **절전·최대 절전에서 돌아오는 경로는 지원 검증 범위가 아닙니다.**
Fast Startup과 최대 절전은 서로 다른 시작 경로입니다.
[Microsoft 설명](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/distinguishing-fast-startup-from-wake-from-hibernation).

## 8. 자동 부팅 원복

부팅할 수 있으면 관리자 PowerShell에서 다음 명령을 실행합니다. EFI 파일·빌드 결과가
없거나 자동 항목 이름이 달라도 Windows 첫 순서 복구를 먼저 시도합니다.

```powershell
.\internal-boot\Install.ps1 -RestoreWindowsFirst
```

Windows를 첫 번째로 돌리고 기록한 Fast Startup 값을 복원합니다. 생성한 EFI·자산·로그를
자동 삭제하지 않습니다. Windows에 못 들어가면 전원을 완전히 끄고 **F8 → Windows Boot
Manager**를 고르거나 BIOS 부팅 순서에서 Windows를 첫 번째로 설정합니다.

`state.json`이나 백업이 손실됐다면 GUID를 추측해 BCD 항목을 삭제하지 마세요.
부분 복구 오류에는 출력된 백업 위치와 현재 firmware 목록을 함께 확인합니다.

## 로그와 흔한 중단

| 표시 | 의미 / 다음 행동 |
|---|---|
| `console-prepare-preflight` | U 이전 화면/드라이버 준비 검사. 입력을 놓친 문제가 아님 |
| `verify-pci-config-diff` | Windows 진입 전 PCI 차이. offset/expected/observed와 화면 보관 |
| `original-unlock-return ...12` | 수동 앱에서 U 대기 시간 만료. 새 전체 부팅에서 시험 |
| `HANDOFF STOPPED` | 실패로 인계 중단. 전원 종료 후 Windows Boot Manager로 복구 |
| `EFI_VOLUME_FULL` / 파일 오류 | 새 로그를 보존할 공간/파일 상태 확인. 자동 삭제는 하지 않음 |

수동 로그는 `RTX3090-UNLOCK/FULLxx.TXT`, `RUNxx/REPORT.TXT`입니다.
자동 로그는 `BOOT000000.TXT`, `AUTO000000/REPORT.TXT` 형식으로 별도 보존합니다.
파일을 닫은 뒤 오류는 화면에만 남을 수 있습니다. 전체 로그에는 부팅 연결 정보가 있어
공개 전에 검토하고, 원본 PROM·BIOS·펌웨어·로컬 설정 파일은 첨부하지 마세요.

## 개발자 검증

`internal-boot/install/test-automatic-policy.ps1`은 BCD/레지스트리 접근 없이 순차 변경과
실패 복구·새 entry 귀속 검사를 실행합니다. `unlock/test-automatic.ps1`은 `-Prom`,
`-ColdCapture`, `-Assets`, `-Clang` 인자로 소유한 fixture를 받아 실제 앱/core를 메모리에서
검사합니다. `-ManualRegression`은 수동 경로 회귀 검사입니다. `boot/test-full.ps1`의
진단 캡처·전체 PCI 캡처·AMI/NV PE·PROM 인자는 개발자의 비공개 로컬 자료입니다.
모의 검사는 실제 펌웨어 성공이나 장기 안정성의 증거가 아닙니다.
