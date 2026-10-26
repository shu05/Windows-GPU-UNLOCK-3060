# Verified hardware and route-specific requirements

**English** | [한국어](COMPATIBILITY.ko.md)

The current source contains three exact-machine profiles with recorded hardware
results: two internal headless configurations and one headless USB configuration.
The original B650M run was performed by the maintainer; B660M and X870 evidence
comes from contributors ([PR #5](https://github.com/impacifist/Windows-GPU-UNLOCK/pull/5),
[PR #10](https://github.com/impacifist/Windows-GPU-UNLOCK/pull/10)). Builds compile
for exactly one reviewed profile selected at build time in
[internal-boot/unlock/machine-profile.h](../internal-boot/unlock/machine-profile.h);
other GPUs or platforms remain unsupported.

| Build profile | Exact platform / CPU | Recorded headless route |
|---|---|---|
| `b650m-3090fe` | ASUS TUF GAMING B650M-PLUS WIFI BIOS 3057 / Ryzen 7 7800X3D / RTX 3090 FE | Internal; original maintainer's run |
| `b660m-3070ti` | ASUS TUF GAMING B660M-E D4 BIOS 4003 / i7-12700F (no iGPU) / MSI RTX 3070 Ti | Internal; contributor report |
| `x870-3090ti` | Gigabyte X870 AORUS ELITE WIFI7 BIOS F13d / Ryzen 7 9800X3D / ASUS RTX 3090 Ti | USB only; contributor report |

The latest tag is the source-only pre-release v0.3.1; this source tree includes
later work, including the USB route and X870 profile. Hardware reports describe
their recorded builds. Current cold-precheck hardening has host/build validation
but no new physical boot/recovery result; compiling a profile is not a new
hardware verification. See each report's provenance and limitations below.

## Profile 1: ASUS TUF B650M / RTX 3090 FE (original)

| Common GPU/platform item | Validated value |
|---|---|
| Card | NVIDIA RTX 3090 Founders Edition, 24 GiB |
| PCI identity | `10de:2204`, subsystem `10de:1454` |
| GPU BDF | `0000:01:00.0` |
| Chip / VRAM register | `0xb72000a1` / `0x6000` MiB |
| BAR0 | `0xf5000000`, 16 MiB |
| PROM SHA256 | `8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4` |
| Motherboard / BIOS | ASUS TUF GAMING B650M-PLUS WIFI / 3057 |
| CPU | AMD Ryzen 7 7800X3D |
| Recorded Windows / driver | Windows 11 Home 26200 / NVIDIA 617.14 |
| UEFI / Secure Boot | x64 / disabled |
| Selector profile | `entryLatchedSs1Full`; targets SS0 `0x88888888`, SS1 `0x8` |

These are documented successful values, not an authorization list. Code also
checks configuration/capabilities, PROM, cold state, driver code, ownership,
DMA cleanup and failure containment. Internal EFI builds additionally contain
the user's own Windows/dedicated ESP GUID configuration.

| Route | Display policy and validation |
|---|---|
| Legacy USB / ordinary internal builds | Preserve AMD physical GOP, PCI `1002:164e`, subsystem `1043:8877`, BDF `0000:0c:00.0` and validated display ancestry |
| Internal headless profile | One RTX physical GOP before release, zero physical GOP afterward; stable console/input interfaces, protected ownership graph, file-only child logging |
| Headless USB (`GU_USB_MEDIA`) | Buildable for this profile; its recorded B650M headless run used the internal route. The contributor's USB hardware evidence is for Profile 3 below |

The headless test had all monitors on RTX and no active AMD GOP. The 7800X3D
still contains an iGPU; BIOS disablement is not established by a missing GOP.
The B650M profile was not re-verified after the profile split; its code path
is preserved behind the same build-time checks.

## Profile 2: ASUS TUF B660M-E D4 / RTX 3070 Ti (aggregate console)

A contributor-reported successful internal headless profile
([PR #5](https://github.com/impacifist/Windows-GPU-UNLOCK/pull/5)): Intel host **without an iGPU**,
so the NVIDIA driver owns the console GOP and the AMI GOP combo splitter
serves the system console on top of it. The wrapper releases the nvgop/EDID/
splitter ownership chain, runs one unlock transaction and chain-loads Windows
Boot Manager without reset.

| Common GPU/platform item | Validated value |
|---|---|
| Card | MSI RTX 3070 Ti, 8 GiB (GA104) |
| PCI identity | `10de:2482`, subsystem `1462:5052` |
| GPU BDF | `0000:01:00.0` |
| Chip / VRAM register | `0xb74000a1` / `0x2000` MiB |
| BAR0 | `0x82000000`, 16 MiB |
| PROM SHA256 | `f429aa1d5f0f18d11ed9a19503683a674fe3541ae77e959df8e732652c2b4df0` |
| Motherboard / BIOS | ASUS TUF GAMING B660M-E D4 / 4003 |
| CPU | Intel Core i7-12700F (no iGPU) |
| Recorded Windows / driver | Windows 11 Pro 22000 / NVIDIA 616.92 |
| UEFI / Secure Boot | x64 / disabled |
| Selector profile | `entryLatchedSs1Full`; targets SS0 `0x88888888`, SS1 `0x8` |

Measured on the same cuBLAS GEMM n=4096 protocol: FP16 88.7 TFLOPS and
BF16 88.6 TFLOPS medians (locked baseline ~43.7). S3 sleep invalidates the
unlock on this profile as well; there is no resume path.

Select this profile with `research/headless/build-unlock.ps1 -HardwareProfile b660m-3070ti`
and `internal-boot/Configure.ps1 -HardwareProfile b660m-3070ti`; the default
`b650m-3090fe` profile is unchanged.

## Profile 3: Gigabyte X870 AORUS ELITE WIFI7 / RTX 3090 Ti (headless USB)

A contributor-reported successful headless profile and the first using the USB route
([headless USB guide](HEADLESS-USB.en.md)). All monitors on the RTX; the Ryzen
iGPU is present on PCI without an active GOP, like the B650M case. Console
topology is the non-aggregate single-RTX one (`GU_CONSOLE_AGGREGATE 0`).

| Common GPU/platform item | Validated value |
|---|---|
| Card | ASUS RTX 3090 Ti, 24 GiB (GA102), VBIOS 94.02.a0.00.2c |
| PCI identity | `10de:2203`, subsystem `1043:8870` |
| GPU BDF | `0000:01:00.0` |
| Chip / VRAM register | `0xb72000a1` / `0x5ff4` MiB |
| BAR0 | `0xdd000000`, 16 MiB |
| PROM SHA256 | `943565975885ee71a7e861265984c785f564f35272c32395ee7410b353d9bbc4` |
| Motherboard / BIOS | Gigabyte X870 AORUS ELITE WIFI7 / F13d |
| CPU | AMD Ryzen 7 9800X3D (iGPU `1002:13c0`, no GOP) |
| Recorded Windows / driver | Windows 11 Pro 26300 / NVIDIA 621.01 |
| UEFI / Secure Boot | x64 / disabled |
| Selector profile | `entryLatchedSs1Full`; targets SS0 `0x88888888`, SS1 `0x8` |

The contributor reports three clean transactions (two full shutdowns, one Windows restart; this board
resets the GPU on restart). cuBLAS GEMM n=4096 medians: BF16 81.0 → 150.2 and
FP16 80.4 → 149.3 TFLOPS. Entry selectors differ per power-on (`0x40123070/3`,
`0x44123070/7`, `0/0`). [Evidence](../evidence/x870-3090ti-usb/README.md).

Build with `research/headless/build-usb.ps1 -HardwareProfile x870-3090ti`. The
internal-ESP installer (`Configure.ps1`, `Headless.ps1`) is not wired for this
profile.

The X870 evidence and byte-identical build statement refer to original PR #10
revision `7b578247a28ff66f2707bea5af0e3943aede7974`, before the current main's
cold-precheck hardening. The old wrapper/child hashes are historical provenance,
not expected hashes for a new build. The updated build has no new X870 physical
boot report. [Evidence provenance](../evidence/x870-3090ti-usb/README.md).

## Scope beyond the recorded configurations

| Other configuration | Status |
|---|---|
| Original B650M, F8-selected internal headless automatic child | One unlock + Windows display recovery + Tensor measurement verified on 2026-10-24 |
| Default automatic startup across repeated cold boots | Not established by the limited supervised runs |
| Another RTX 3090, even a Founders Edition in another PC | Unverified; exact checks may reject |
| Another ASUS BIOS, AMD board/CPU or Intel host | Unverified; separate profile and validation required |
| CPU without iGPU (Intel host, aggregate console) | One contributor-reported profile above; still not generalized |
| Multiple discrete GPUs, laptop/hybrid setup | Unverified |
| Other NVIDIA models/architectures | Separate GPU port and validation required |
| AMD Radeon / Intel discrete unlock target | No unlock implementation here |
| Games, ComfyUI, sleep/hibernate resume, long-term stability | Not established by GEMM/boot evidence |

See [internal headless instructions](HEADLESS.en.md),
[headless USB instructions](HEADLESS-USB.en.md), [legacy USB instructions](GUIDE.en.md)
and the [original B650M evidence](../evidence/headless-handoff/README.md).
The ordinary source guards remain in [root unlock](../unlock/app.c); the separate
headless implementation is in [internal unlock](../internal-boot/unlock/app.c),
[headless policy](../internal-boot/unlock/headless-policy.h) and
[wrapper release policy](../internal-boot/boot/full-release.c).

Changing constants, replacing a PROM hash or deleting a display check is not a
validated port. New support requires memory/WPR layout, firmware compatibility,
ownership, DMA cleanup and failure-path evidence.
[Portability criteria](PORTABILITY.en.md) / [compatibility reporting](CONTRIBUTING.en.md).
