# Port: MSI MPG Z390 GAMING PLUS + RTX 3060 12 GB (GA106)

**Status: working, measured.** Unlock transaction completes from a USB stick and
from an internal ESP partition; Windows boots normally afterwards; tensor
throughput measured **+88–90 % BF16** (see Benchmark below). This profile
(`msi-z390-3060`) is an **experimental GA106 port** of the upstream V67/PLM
unlock, which was recorded on GA102/GA103 hardware. It is knowingly unvalidated
upstream; the evidence in this file is from one specific machine.

## Target machine (exact identity)

| Item | Value |
|---|---|
| Motherboard | MSI MPG Z390 GAMING PLUS (MS-7B51), BIOS 1.70 (AMI Aptio V) |
| CPU | Intel Core i5-9600K (Coffe Lake, iGPU disabled) |
| GPU | NVIDIA GeForce RTX 3060 12 GB, GA106, PCI `0000:01:00.0` (root port `8086:1901`) |
| OS | Windows 11, driver 595.79, CUDA 12.6, Secure Boot **off**, BitLocker off |
| Storage | Samsung 970 EVO Plus 500 GB (GPT: recovery, ESP, MSR, C:, recovery) |

## What the port adds over upstream

1. **`msi-z390-3060` hardware profile** (`-HardwareProfile msi-z390-3060` in
   every build script): board/BIOS identity pins, GA106 register offsets, and
   `GU_MSI_INT10_CONSOLE=1` (this firmware keeps a usable INT10 text console
   through the wrapper; upstream Z390 boards hang the console without it).
2. **BAR / extended-config probing in the wrapper** (`GuProbeBars`,
   `GuCaptureExt`, `GuDiffExt` in `internal-boot/boot/full-release.c`).
   This AMI `PciBusDxe` returns `EFI_UNSUPPORTED` for config reads beyond
   256 bytes, so extended config (0x100–0xFFF) is read through **ECAM MMIO**
   (base discovered by probing `0xE0000000 + 0x100000` for the board ID).
   The diff proved the unlock changes only four NVIDIA vendor dwords and
   creates **no SR-IOV capability**.
3. **The code-12 fix — `GuSilenceDevice()`** (`internal-boot/boot/handoff.c`).
   After the unlock the re-modelled device made Windows reuse the
   UEFI-programmed BARs and fail resource assignment
   (`CM_PROB_NORMAL_CONFLICT`, Device Manager code 12). The fix unconfigures
   the device right before `StartImage(bootmgfw.efi)` — via ECAM it zeroes
   `PCI_COMMAND`, BAR0, BAR1, BAR1 high and the ROM BAR — so `pci.sys`
   performs a fresh assignment. **Do not remove this step.**
4. **No-logs wrapper build (`-DGU_NO_FILE_LOGS`)**: the wrapper's log layer
   lands on an in-memory `EFI_FILE_PROTOCOL` stub and `Early()` is skipped, so
   an ESP-installed unlock writes **nothing** to disk. The child still writes
   one fixed-size, in-place-overwritten `RTX3090-UNLOCK\AUTO000000\REPORT.TXT`.
5. **Installer tolerance for small ESPs**: the 1 GiB minimum in
   `Configure.ps1` / `Headless.ps1` / `install/headless-package.ps1` is
   lowered to 512 MiB, and a `Get-VolumeInfoFallback` (kernel32
   `GetVolumeInformationW`) was added because a freshly formatted hidden ESP
   mounts fine but stays invisible to the storage CIM provider until reboot.

## Prerequisites (Windows side)

- LLVM/Clang + LLD (`C:\Program Files\LLVM\bin`), Python 3.
- For the benchmark only: CUDA Toolkit 12.6 + VS2022 BuildTools (`nvcc` must
  run inside `cmd /c "vcvars64.bat && nvcc ..."`).
- Firmware assets (NVIDIA PROM/GSP blobs, not committed — license): five
  `.bin` files validated against `internal-boot/unlock/assets.json`. Place
  them in `.\assets\` (see upstream `ASSETS.en.md`).
- Secure Boot disabled; C: fully decrypted (installer verifies both).

## Route A — USB stick (bring-up / recovery)

```powershell
# build wrapper + child for the stick (writes its own platform-config.h)
.\research\headless\build-usb.ps1 -WindowsEspGuid <ESP-GUID> `
    -Clang 'C:\Program Files\LLVM\bin\clang.exe' `
    -LldLink 'C:\Program Files\LLVM\bin\lld-link.exe' -Python python `
    -HardwareProfile msi-z390-3060
# stage: \EFI\BOOT\BOOTX64.EFI + UNLOCK-HEADLESS.EFI + RTX3090-UNLOCK\ASSETS
.\research\headless\stage-usb.ps1 ...
```

Cold-boot the machine (see *Power-cycle* below), press **F8** at POST, pick the
USB stick. The screen goes dark while GSP re-inits (no UEFI GOP in that mode)
and returns when Windows starts.

## Route B — internal ESP auto-unlock (daily use)

Layout used here: the former 529 MB junk recovery partition at disk offset 1 MB
was repurposed as a second ESP (label `GPU-UNLOCK`, drive letter `V:`).
The Windows ESP is untouched.

```powershell
# 1. one-time partition prep (diskpart):
#    Set-Partition -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
#    format quick fs=fat32 label=GPU-UNLOCK
#    gpt attributes=0x0000000000000000     # 0x-prefixed form is mandatory
#    reboot, then persistently:  mountvol V: "\\?\Volume{<vol-guid>}\"
# 2. configure + build + register:
.\internal-boot\Configure.ps1 -UnlockDriveLetter V `
    -WindowsEspGuid <windows-esp-guid> -HardwareProfile msi-z390-3060
.\research\headless\build-unlock.ps1 -HardwareProfile msi-z390-3060 ...
.\internal-boot\Headless.ps1 -Assets .\assets -Register
# 3. also expose the wrapper on the removable path so the firmware's
#    device-level boot option finds it:
copy .\internal-boot\boot\build\headless\HEADLESS.EFI V:\EFI\BOOT\BOOTX64.EFI
```

`-Register` appends **GPU Headless UNLOCK - EXPERIMENTAL** to the firmware
boot order *behind* Windows Boot Manager and exports a BCD backup to
`C:\ProgramData\WindowsGPUUnlock\HeadlessBackups\`.

### Choosing what to boot (MSI specifics)

The MSI **F8 boot-device menu lists devices, not Boot#### entries**:

| F8 menu entry | Result |
|---|---|
| **Windows Boot Manager (Samsung SSD 97…)** | normal Windows, GPU locked (factory default order) |
| **Samsung SSD 970 EVO Plus 500GB** | unlock from the first ESP on the disk, then Windows |

The registered `Boot####` entry itself is visible/reorderable in **BIOS Setup
(Del) → Boot → Boot Option #1…N**. Making it #1 gives fully automatic unlock
on every boot; leaving Windows first keeps the manual choice.

## Recovery

- Any gate failure is fail-closed: the GPU is left untouched, the wrapper
  prints `HANDOFF STOPPED …` and you boot normally via Windows Boot Manager.
- `.\internal-boot\Headless.ps1 -RestoreWindowsFirst` restores Windows-first
  boot order; the BCD backup above covers worse cases.
- The unlock is **volatile**: it survives warm reboots but is lost on full
  power removal. A power button press does **not** reset the GSP latch
  (standby power); only PSU switch / cord removal for ~60 s restores stock.
  Always cold-cycle before re-running the unlock, otherwise the wrapper's
  already-unlocked gate stops the boot.

## Benchmark (cuBLAS n=4096, 30 samples, median; `benchmark/compute-benchmark-3060.cu`)

| State | FP32 | FP16 tensor | BF16 tensor |
|---|---|---|---|
| Locked (stock) | 8.09 TF | 27.11 TF | 27.02 TF |
| Unlocked | 7.59 TF | **50.03 TF** | **50.92 TF** |
| Gain | −6 % (boost noise) | **+84.5 %** | **+88.5 %** |

A later ESP auto-unlock boot measured 51.29 TF BF16 (+90 %). Full JSON:
`benchmark/results/msi-z390-3060-2026-10-26.json`.

## Troubleshooting notes (learned the hard way)

- `nvcc` cannot find `cl.exe` → always wrap in `vcvars64.bat`.
- `LNK1104 … bin\bench3060.exe` → create `bin\` first.
- Wrapper `StartImage(child)` returning `0x7` with no `REPORT.TXT` right after
  staging: the firmware fallback loaded the image from the wrong ESP handle;
  retry, or boot the registered `Boot####` entry instead.
- `diskpart assign letter=` from a remote/SSH session may not persist across
  reboots; `mountvol` writes the persistent mount table.
- `bcdedit /delete '{guid}'` in PowerShell needs the braces single-quoted.
- The vendored edk2 `EFI_FILE_PROTOCOL` field order is
  Revision, Open, Close, Delete, Read, Write, GetPosition, SetPosition,
  GetInfo, SetInfo, Flush, OpenEx, ReadEx, WriteEx, FlushEx — and the `*Ex`
  members take only `(This, Token)` in this snapshot.

## License

GPL-2.0-only, same as upstream (see `LICENSE`, `THIRD_PARTY_NOTICES.md`).
Firmware `.bin` assets are NVIDIA-derived and are not redistributed here.
