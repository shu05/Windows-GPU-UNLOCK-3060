# Windows GPU UNLOCK v0.2.0 — internal handoff / experimental automatic boot

**[한국어 설치·원복 안내](docs/INTERNAL-BOOT.ko.md) | [English walkthrough](docs/INTERNAL-BOOT.en.md)**

- 같은 ASUS BIOS 3057 / RTX 3090 조합에서 내부 EFI → U 한 번 → Windows 직접 진입과
  FP16 125.91 / BF16 128.93 TFLOPS를 확인했습니다. 각 3회 중앙값이며 다른 GPU 활동이 있었습니다.
- 출력 경로 자동 식별, 제한된 PCI 오류 상태 비교, 인계 단계 오류 표시를 추가했습니다.
- 별도 자동판은 U 없이 한 번 실행하며 Esc 취소·기존 hardware guard·실패 시 정지를 유지합니다.
- **자동판 첫 실제 부팅은 아직 미검증입니다.** 공개 설치 adapter의 등록/승격도 실기 미검증입니다.
  수동 성공부터 확인하는 실험용 pre-release입니다. 다른 카드/보드/BIOS 지원을 추가하지 않았습니다.
- 소스 ZIP에는 GPL 소스·EDK2 헤더·안내·검사와 기존 USB 수동 EFI가 있습니다.
  새 내부 EFI는 자신의 파티션 GUID로 직접 빌드합니다. 펌웨어 5개·PROM·BIOS·로컬 식별자는 제외합니다.
- Public builds reproduced all four local EFI hashes. Host tests cover the actual automatic app/core,
  9,842 full-release checks, 39 handoff-flow checks, 291 console checks, transaction rollback and copy attribution.
  These are software tests, not proof of automatic firmware success or long-term stability.

Do not use the legacy `release/EFI/BOOT/BOOTX64.EFI` as an automatic boot image.
Follow `internal-boot/Configure.ps1` → `Build.ps1` → manual registration/testing → optional automatic mode.
Recovery: F8 → Windows Boot Manager, then `Install.ps1 -RestoreWindowsFirst`.

The source archive's corresponding-source manifest and SHA256 are included. Firmware and private fixtures
are not redistributed. The v0.1.0 USB source/EFI and provenance remain unchanged.
