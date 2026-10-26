# Headless unlock from a USB stick

**English** | [README](../README.md) | [Internal headless guide](HEADLESS.en.md)

The headless wrapper/child pair can run from an ordinary FAT32 USB stick instead
of a dedicated internal ESP. No partition is created, resized or retyped, and no
firmware boot entry is registered. The build is still exact-profile: the same
PCI/PROM/cold-state/driver-fingerprint checks apply, and the wrapper only runs the
child whose SHA256 it was built with.

`-DGU_USB_MEDIA` changes one thing: instead of pinning an internal ESP GUID, the
wrapper and child accept only the **USB** device they were loaded from. The Windows
ESP GUID is still pinned, and the Windows Boot Manager is chain-loaded from it.

## Pre-release checks and fallback

Two behaviours limit failures before driver release:

- **Read-only cold pre-check before any release.** After the 5-second Esc window,
  the wrapper first requires memory decoding, disabled bus mastering, an exact
  raw BAR0 and the existing resource descriptor/end-tag/checksum checks. It binds
  these checks to one target handle/PciIo and rechecks its BDF and complete PCI
  snapshot after resource allocation/free, before reading the nine cold-state
  registers through the PciIo that the firmware NVIDIA driver still owns.
  If those prerequisites cannot be verified, or the GPU is not in its power-on state
  (Windows restart, already unlocked, initialized WPR2, wrong memory size), the
  existing skip path starts Windows with nothing released or written. Before this,
  that state was only detected by `VerifyCold` *after* driver release, leaving a
  black screen and a stop. This pre-check applies to every full-unlock build,
  including the internal ones; `VerifyCold` after release is unchanged.
- **Return to the firmware boot order when nothing was touched.** In a USB build,
  a failure before driver release (Windows ESP not found, log volume error)
  returns `EFI_NOT_STARTED` to the firmware, which continues with the next boot
  option, normally Windows Boot Manager. Failures after release still stop
  without reset or retry, exactly as before.

These checks observe service-return boundaries; they cannot make firmware calls
atomic. `GetBarAttributes` provides no allocation length, so descriptor parsing
still trusts the UEFI service to return a buffer large enough for its descriptor
and end tag. Host tests validate declared fields and simulated failures; they do
not establish safety for an arbitrary truncated pointer or actual vendor callback.
Existing successful hardware reports concern earlier builds and do not validate
this hardening. No new physical boot or recovery result is claimed here.

## Build and stage

```powershell
# Windows ESP GUID: Get-Partition | Where-Object IsSystem | Select Guid
.\research\headless\build-usb.ps1 -WindowsEspGuid 'YOUR-WINDOWS-ESP-GUID' `
  -HardwareProfile b650m-3090fe -Clang 'C:\TOOLS\LLVM20\bin\clang.exe' -LldLink 'C:\TOOLS\LLVM20\bin\lld-link.exe'
.\research\headless\stage-usb.ps1 -Drive D -Assets 'C:\LOCAL-FIRMWARE\assets'
```

`build-usb.ps1` writes `platform-config.h` only if there is none, or if the existing
one was written by `build-usb.ps1`; it refuses to overwrite an internal `Configure.ps1`
setup. `stage-usb.ps1` refuses non-USB or non-FAT32 volumes, keeps any previous
`EFI\BOOT\BOOTX64.EFI` in `RTX3090-UNLOCK\backup\`, and verifies every copied file
against the package manifest and `unlock/assets.json`.

Stick layout:

| Path | Content |
|---|---|
| `EFI\BOOT\BOOTX64.EFI` | headless wrapper (removable-media boot path) |
| `EFI\WindowsGPUUnlock\UNLOCK-HEADLESS.EFI` | child, loaded only if its SHA256 matches |
| `RTX3090-UNLOCK\ASSETS\` | the five local firmware assets (never redistribute) |
| `RTX3090-UNLOCK\HLSnnnnnn.TXT`, `AUTOnnnnnn\REPORT.TXT` | per-boot logs |

## Use

Shut down fully, power on and choose the stick from the firmware boot menu. Do
not press keys. Expected sequence: 5 s Esc window (Esc boots Windows unchanged) →
read-only cold check → display goes black during driver release → 3 s Esc window
(Esc stops) → one unlock → Windows Boot Manager without reset. A Windows
**restart** may leave the GPU initialized or may reset it, depending on the
platform. A non-cold or otherwise unverifiable state takes the skip path; do not
assume a restart preserves or re-applies unlock. Follow the tested route for the
exact profile and use a full shutdown for a controlled cold-boot experiment.

Only after one supervised success should the stick become a default boot device.
If a run stops after the display went black, power off and boot Windows Boot
Manager directly. Never retry in the same power session.

Host tests: `internal-boot\boot\test-cold-precheck.ps1 -HardwareProfile <profile>`
(no fixtures) and the headless app test with `-DGU_USB_MEDIA`, which also checks that
a USB build loaded from an internal partition refuses before any hardware effect.
