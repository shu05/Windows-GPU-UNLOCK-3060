# Internal disk unlock → Windows boot

> This guide covers the legacy path that retains the AMD display. For the
> v0.3.0 RTX-display headless path, use the [headless guide](HEADLESS.en.md).
> Statements below about unverified automatic boot refer to the older AMD profile.

**English** | [한국어: detailed walkthrough](INTERNAL-BOOT.ko.md)

This experimental profile remains restricted to **ASUS TUF GAMING B650M-PLUS WIFI,
BIOS 3057, RTX 3090 10de:2204/subsystem 10de:1454, AMD iGPU 1002:164e/1043:8877**.
PROM, PCI location/BAR, driver code hashes and ownership are checked as well. A
different RTX 3090 or BIOS is not supported by changing constants or bypassing checks.

Hardware evidence on 2026-10-23: internal manual entry → one U press → Windows
without reset → FP16 **125.91**, BF16 **128.93 TFLOPS** (three-run medians, other
GPU activity present). [Measurements](../evidence/internal-handoff/summary.json).
**The no-U automatic variant has passed host tests and was installed locally, but
its first real firmware boot remains unverified.** The public installer has not
been registered/promoted on other hardware. Manual success does not prove automatic success.

The original root `unlock/` and `release/EFI/BOOT/BOOTX64.EFI` remain the legacy USB
manual release. The new implementation lives in **`internal-boot/`**. Build the new
EFI locally for your own partition GUIDs; there is no universal automatic EFI download.

## Prerequisites

- Exact profile above; Windows 11 x64 UEFI, Secure Boot disabled, C: fully decrypted
  and BitLocker protection off. Scripts never change encryption settings.
- Motherboard/iGPU display and keyboard connected throughout boot. Firmware cursor
  or mouse may pause during reviewed driver release.
- A **separate existing FAT32 EFI System Partition, at least 1 GiB**, with a drive
  letter such as U:. Never delete/format the Windows system ESP or recovery partition.
- Five locally owned [firmware assets](ASSETS.en.md), Python 3.9+, Git and
  [clang/lld-link 20.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8).
- MSVC/Windows SDK and private fixtures only if running host tests yourself.

If a dedicated ESP does not exist, create a new 1024 MiB FAT32 volume in Windows
Disk Management after saving/backing up your work. Select only that new volume for
the following type change. The scripts themselves never shrink, create or format partitions.

```powershell
Get-Partition | Format-Table DiskNumber,PartitionNumber,DriveLetter,Size,GptType,Guid,IsSystem
Get-Volume -DriveLetter U | Format-List DriveLetter,FileSystemLabel,FileSystem,Size
# Only for the verified NEW dedicated U: volume, never the system ESP:
Set-Partition -DriveLetter U -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
```

Record the GUID of the existing `IsSystem=True` Windows ESP separately.

## Configure and build

Run an administrator PowerShell. Replace the GUID placeholder and LLVM paths with
your own values. Configuration writes only ignored local files; no boot changes.

```powershell
git clone https://github.com/impacifist/Windows-GPU-UNLOCK.git
cd Windows-GPU-UNLOCK
.\internal-boot\Configure.ps1 -UnlockDriveLetter U -WindowsEspGuid 'WINDOWS-ESP-GUID-HERE'
.\internal-boot\Build.ps1 -Clang 'C:\TOOLS\LLVM20\bin\clang.exe' -LldLink 'C:\TOOLS\LLVM20\bin\lld-link.exe'
```

Do not publish `local-config.json`, `boot/platform-config.h`, generated headers,
installation state, raw firmware/PROM or BIOS files. Firmware is not redistributed.

## Stage, then register a manual test

Prepare the five exact assets in a local directory; `unlock/assets.json` lists
names, sizes and SHA256. Existing different files are refused instead of overwritten.

```powershell
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets'
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets' -RegisterManual
```

Windows stays first. BCD backups are under
`%ProgramData%\WindowsGPUUnlock\Backups\<timestamp>`. Keep `install/state.json`.
Save work, fully power off, start and use **ASUS F8 → Windows GPU UNLOCK Manual**.
Wait through the first five seconds; press **U once only when the unlock app opens**.
It should enter Windows directly. Verify FP16/BF16 using the [same benchmark](BENCHMARK.en.md).
Any failure: photograph the whole screen, fully power off, then choose Windows Boot
Manager. Never force continuation or repeat unlock in the same session.

## Optional automatic mode

Only after personally confirming the manual unlock, direct handoff and Tensor
performance, explicitly attest that result:

```powershell
.\internal-boot\Install.ps1 -Assets 'C:\LOCAL-FIRMWARE\assets' -EnableAutomaticBoot -ConfirmedManualSuccess
```

This switches the registered entry to AUTO.EFI, places it before Windows and sets
`HiberbootEnabled=0`, saving its prior value. Existing Windows and manual files remain.
No reboot is issued. On the next full boot: five-second Esc window → driver checks
and release → three-second Esc cancellation → one automatic unlock → Windows. No U.
Esc in the **first** window boots Windows without hardware changes; cancelling in
the second window stops safely because drivers have already been released.
Unexpected owners, input errors, failed checks and partial cleanup stop instead of
retrying, rebooting or forcing Windows. First automatic hardware verification is still required.

This covers full boots, not validated sleep/hibernate resume. If a restart fails
cold-state checks, fully power off before retrying.
[Microsoft distinguishes Fast Startup and hibernation](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/distinguishing-fast-startup-from-wake-from-hibernation).

## Restore Windows first

```powershell
.\internal-boot\Install.ps1 -RestoreWindowsFirst
```

Recovery does not require working automatic EFI/build files or a matching current
entry name. It restores Windows first and the saved Fast Startup setting, without
deleting EFI, assets or logs. If Windows cannot start, fully power off and select
**F8 → Windows Boot Manager**, or put Windows first in firmware setup.

Manual logs: `FULLxx.TXT`, `RUNxx/REPORT.TXT`. Automatic logs:
`BOOT000000.TXT`, `AUTO000000/REPORT.TXT`, all under `RTX3090-UNLOCK/` on the dedicated ESP.
Errors after files close may be screen-only. Review logs before publishing; never
upload raw PROM, BIOS/firmware or local configuration. Disk-full/log errors stop
execution; no automatic deletion is performed.

Developer tests are described at the end of the [Korean guide](INTERNAL-BOOT.ko.md).
Host mock success is not hardware validation or a long-term stability claim.
