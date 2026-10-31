// Copyright (C) Windows GPU UNLOCK contributors. GPL-2.0-only.
/*
 * Reviewed single-machine profiles. Every write-enabled build compiles for
 * exactly one profile: all hardware identities, display topology and driver
 * fingerprints below are immutable, build-time selected and never read from
 * a user-editable file. Select with -DGU_PROFILE_B650M_3090FE or
 * -DGU_PROFILE_B660M_3070TI; the default is the originally verified machine.
 * Adding a profile here is never sufficient for support: each entry needs
 * its own hardware evidence (see docs/PORTABILITY.en.md stage 4).
 */
#pragma once

#if (defined(GU_PROFILE_B650M_3090FE) + defined(GU_PROFILE_B660M_3070TI) + defined(GU_PROFILE_X870_3090TI) + defined(GU_PROFILE_MSI_Z390_3060)) > 1
#error Select exactly one machine profile
#endif
#if !defined(GU_PROFILE_B650M_3090FE) && !defined(GU_PROFILE_B660M_3070TI) && !defined(GU_PROFILE_X870_3090TI) && !defined(GU_PROFILE_MSI_Z390_3060)
#define GU_PROFILE_B650M_3090FE 1
#endif

/* 1 = the AMI combo splitter owns the console on top of the target GPU
 * (aggregate GOP + text-out driver chain; verified on AMI boards without a
 * second display GPU). 0 = nvgop owns the console GOP on the target directly
 * (original verified B650M headless topology). */
#if defined(GU_PROFILE_B660M_3070TI)
#define GU_CONSOLE_AGGREGATE 1
#else
#define GU_CONSOLE_AGGREGATE 0
#endif

/* 1 = the console GOP is the AMI CSM BIOS[INT10] video driver chain: the
 * driver is a BIOS firmware-volume image (PIWG file path, firmware code
 * type), it records child relationships on PciIo instead of the parent
 * device path, no EDID/mouse runtime driver consumes the child, and the
 * whole console chain (ConOut/ConIn/StdErr) is destroyed when the driver
 * is released. Experimental MSI Z390 port only; 0 elsewhere. */
#if defined(GU_PROFILE_MSI_Z390_3060)
#define GU_MSI_INT10_CONSOLE 1
#else
#define GU_MSI_INT10_CONSOLE 0
#endif

/* Whether a separate EDID/mouse runtime driver consumes each display
 * child (stage-3 detach). The MSI INT10 topology installs EDID on the
 * child but has no consumer agent. */
#if defined(GU_PROFILE_MSI_Z390_3060)
#define GU_EDID_AGENT_PRESENT 0
#else
#define GU_EDID_AGENT_PRESENT 1
#endif

#if defined(GU_PROFILE_X870_3090TI)

/* Gigabyte X870 AORUS ELITE WIFI7 BIOS F13d, Ryzen 7 9800X3D (Granite
 * Ridge iGPU 1002:13c0 present, no GOP), ASUS RTX 3090 Ti 24GB (GA102,
 * VBIOS 94.02.a0.00.2c): hardware-verified headless USB port. Single-RTX
 * topology like the B650M one (non-aggregate console). Values come from
 * three identical read-only probe captures. The card reports 0x5ff4 MiB
 * usable, not the 3090 FE's 0x6000. */
#define GU_PROFILE_NAME "GIGABYTE-X870-AORUS-ELITE-WIFI7-F13d-RTX3090TI"
#define GU_BOARD_ID 0x220310deU
#define GU_BOARD_SUBSYSTEM 0x88701043U
#define GU_BAR0_BASE 0xdd000000ULL
#define GU_BAR0_END 0xddffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x5ff4U
#define GU_CHIP_ID 0xb72000a1U
#define GU_PROM_SHA256 "943565975885ee71a7e861265984c785f564f35272c32395ee7410b353d9bbc4"
#define GU_ROOT_PORT_DEVICE 1U
#define GU_ROOT_PORT_FUNCTION 1U
/* 36-byte display device path: ACPI root, PciRoot(0), Pci(1,1), Pci(0,0), ADR. */
#define GU_RTX_PATH_BYTES \
  2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,1,1,1,1,6,0,0,0,2,3,8,0,0,3,1,128,127,255,4,0
/* AMI console splitter text-out driver (BIOS F13d image fingerprint). */
#define GU_CONSOLE_TEXT_SIZE 0xb000U
#define GU_CONSOLE_TEXT_STOP 0x20e0U
#define GU_CONSOLE_TEXT_VERSION 0x10U
/* AMI GOP splitter (BIOS F13d image fingerprint). */
#define GU_CONSOLE_GFX_SIZE 0x9000U
#define GU_CONSOLE_GFX_STOP 0x1cc0U
#define GU_CONSOLE_GFX_VERSION 0x10U
/* AMI EDID/mouse runtime driver (BIOS F13d image fingerprint). */
#define GU_EDID_IMAGE_SIZE 0x9000U
#define GU_EDID_STOP 0x14d0U
#define GU_EDID_TEXT_BYTES 0x37beU
#define GU_EDID_TEXT_SHA256 "9a08c48e3a09965d1dda48b05a400311d71160d727cc9250630976cf4057eca9"
/* nvgop-ga1xx 0x60010: byte-identical .text to the B660M/3070 Ti entry. */
#define GU_NVGOP_VERSION 0x60010U
#define GU_NVGOP_IMAGE_SIZE 0x26ae0U
#define GU_NVGOP_STOP 0x678U
#define GU_NVGOP_TEXT_BYTES 0x20d33U
#define GU_NVGOP_TEXT_SHA256 "615b37a708b1894e18d7fa60cfcdacaa4d8b598863a10b8103d231a595847b2b"

#elif defined(GU_PROFILE_MSI_Z390_3060)

/* Micro-Star MPG Z390 GAMING PLUS (MS-7B51) BIOS 1.70, i5-9600K (iGPU
 * disabled), MSI RTX 3060 12GB GA106 (subsystem 40741458, rev a1):
 * experimental port. Values come from one read-only diagnostic probe
 * capture on this exact machine. The console GOP is the AMI CSM
 * BIOS[INT10] Video Driver (firmware-volume image, PIWG file GUID
 * 29cf55f8-...), not the OptionROM nvgop-ga1xx. The firmware leaves
 * IO+memory+bus-master decode enabled at entry and reports
 * firmware-volume driver code type 3. GA106 reports the GA104-style PMC
 * id 0xb74000a1 and uses the shared ga10x GSP assets. Hardware untested. */
#define GU_PROFILE_NAME "MSI-MPG-Z390-GAMING-PLUS-MS-7B51-1.70-RTX3060-GA106"
#define GU_BOARD_ID 0x248710deU
#define GU_BOARD_SUBSYSTEM 0x40741458U
#define GU_BAR0_BASE 0xa2000000ULL
#define GU_BAR0_END 0xa2ffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x3000U
#define GU_CHIP_ID 0xb74000a1U
#define GU_PROM_SHA256 "2f8a2ce67ff2b77ced9db4f229f1fae21f2ff8732d3420bbf9ee0d8653d7d7fc"
#define GU_ROOT_PORT_DEVICE 1U
#define GU_ROOT_PORT_FUNCTION 0U
/* 36-byte display device path: ACPI root, PciRoot(0), Pci(1,0), Pci(0,0),
 * endpoint ADR 0x80010100 (measured from the single RTX GOP handle). */
#define GU_RTX_PATH_BYTES \
  2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,0,1,1,128,127,255,4,0
/* AMI console text-out driver (BIOS 1.70 image fingerprint, PIWG
 * 628a497d-2bf6-4264-8741-069dbd3399d6; binding stop 0x1280). */
#define GU_CONSOLE_TEXT_SIZE 0x6500U
#define GU_CONSOLE_TEXT_STOP 0x12ccU
#define GU_CONSOLE_TEXT_VERSION 0x10U
/* AMI GOP consumer driver bound to the RTX child (BIOS 1.70 image
 * fingerprint, PIWG 43e7abdd-e352-4cfb-a230-4cdc1d350e5c). */
#define GU_CONSOLE_GFX_SIZE 0x54c0U
#define GU_CONSOLE_GFX_STOP 0x9dcU
#define GU_CONSOLE_GFX_VERSION 0x10U
/* No EDID/mouse runtime driver consumes the display child here. */
#define GU_EDID_IMAGE_SIZE 0U
#define GU_EDID_STOP 0U
#define GU_EDID_TEXT_BYTES 0U
#define GU_EDID_TEXT_SHA256 ""
/* BIOS[INT10] Video Driver (BIOS 1.70 image fingerprint, PIWG
 * 29cf55f8-b675-4f5d-8f2f-b87a3ecfd063, binding version 0x24). */
#define GU_NVGOP_VERSION 0x24U
#define GU_NVGOP_IMAGE_SIZE 0x48a0U
#define GU_NVGOP_STOP 0x7c4U
#define GU_NVGOP_TEXT_RVA 0x2a0U
#define GU_NVGOP_TEXT_BYTES 0x379dU
#define GU_NVGOP_TEXT_SHA256 "35ca33bfe63ef4ef3d347c79ce753d341e1aa258294ad84c2057530586194007"
#define GU_DRIVER_CODE_TYPE 3
#define GU_NVGOP_PATH_PIWG 1
#define GU_NVGOP_FILE_GUID {0x29cf55f8,0xb675,0x4f5d,{0x8f,0x2f,0xb8,0x7a,0x3e,0xcf,0xd0,0x63}}

#elif defined(GU_PROFILE_B660M_3070TI)

/* ASUSTeK TUF GAMING B660M-E D4 BIOS 4003, i7-12700F (no iGPU),
 * MSI RTX 3070 Ti (GA104): hardware-verified headless port. */
#define GU_PROFILE_NAME "ASUS-TUF-GAMING-B660M-E-D4-4003-RTX3070TI"
#define GU_BOARD_ID 0x248210deU
#define GU_BOARD_SUBSYSTEM 0x50521462U
#define GU_BAR0_BASE 0x82000000ULL
#define GU_BAR0_END 0x82ffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x2000U
#define GU_CHIP_ID 0xb74000a1U
#define GU_PROM_SHA256 "f429aa1d5f0f18d11ed9a19503683a674fe3541ae77e959df8e732652c2b4df0"
#define GU_ROOT_PORT_DEVICE 1U
#define GU_ROOT_PORT_FUNCTION 0U
/* 36-byte display device path: ACPI root, PciRoot(0), Pci(1,0), endpoint. */
#define GU_RTX_PATH_BYTES \
  2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,32,3,1,128,127,255,4,0
/* AMI console splitter text-out driver (BIOS 4003 image fingerprint). */
#define GU_CONSOLE_TEXT_SIZE 0x6a40U
#define GU_CONSOLE_TEXT_STOP 0x104cU
#define GU_CONSOLE_TEXT_VERSION 0x10U
/* AMI GOP combo splitter C1 GOP (BIOS 4003 image fingerprint). */
#define GU_CONSOLE_GFX_SIZE 0x3bc0U
#define GU_CONSOLE_GFX_STOP 0xb2cU
#define GU_CONSOLE_GFX_VERSION 0x10U
/* AMI GOP combo aggregate driver present only in this topology. */
#define GU_COMBO_SIZE 0x2160U
#define GU_COMBO_STOP 0x88cU
#define GU_COMBO_VERSION 0x11U
/* AMI EDID/mouse runtime driver (BIOS 4003 image fingerprint). */
#define GU_EDID_IMAGE_SIZE 0x9000U
#define GU_EDID_STOP 0x14d4U
#define GU_EDID_TEXT_BYTES 0x37daU
#define GU_EDID_TEXT_SHA256 "a015d52f55fa6441414458e2b699ba2edcb53f03b001ac6367e45bcd908c418e"
/* nvgop-ga1xx driver shipped for GA104 (binding 0x60010). */
#define GU_NVGOP_VERSION 0x60010U
#define GU_NVGOP_IMAGE_SIZE 0x26ae0U
#define GU_NVGOP_STOP 0x678U
#define GU_NVGOP_TEXT_BYTES 0x20d33U
#define GU_NVGOP_TEXT_SHA256 "615b37a708b1894e18d7fa60cfcdacaa4d8b598863a10b8103d231a595847b2b"

#else

/* ASUSTeK TUF GAMING B650M-PLUS WIFI BIOS 3057, Ryzen (Raphael iGPU
 * present but not the console in the headless profile),
 * RTX 3090 FE (GA102): originally verified machine. */
#define GU_PROFILE_NAME "ASUS-TUF-B650M-PLUS-WIFI-3057-RTX3090-FE"
#define GU_BOARD_ID 0x220410deU
#define GU_BOARD_SUBSYSTEM 0x145410deU
#define GU_BAR0_BASE 0xf5000000ULL
#define GU_BAR0_END 0xf5ffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x6000U
#define GU_CHIP_ID 0xb72000a1U
#define GU_PROM_SHA256 "8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4"
#define GU_ROOT_PORT_DEVICE 1U
#define GU_ROOT_PORT_FUNCTION 1U
#define GU_RTX_PATH_BYTES \
  2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,1,1,1,1,6,0,0,0,2,3,8,0,112,3,1,128,127,255,4,0
#define GU_CONSOLE_TEXT_SIZE 0x7820U
#define GU_CONSOLE_TEXT_STOP 0x1494U
#define GU_CONSOLE_TEXT_VERSION 0x10U
#define GU_CONSOLE_GFX_SIZE 0x48a0U
#define GU_CONSOLE_GFX_STOP 0xf04U
#define GU_CONSOLE_GFX_VERSION 0x10U
#define GU_EDID_IMAGE_SIZE 0x9000U
#define GU_EDID_STOP 0x157cU
#define GU_EDID_TEXT_BYTES 0x38f8U
#define GU_EDID_TEXT_SHA256 "53ef1716fa7a8cf866c986e85dd8412185a9fd58dd89e90202f97cb0997c96a6"
#define GU_NVGOP_VERSION 0x60008U
#define GU_NVGOP_IMAGE_SIZE 0x2b6e0U
#define GU_NVGOP_STOP 0x698U
#define GU_NVGOP_TEXT_BYTES 0x23d6aU
#define GU_NVGOP_TEXT_SHA256 "6aad26e6909c9791dd0caba92d6658e9dfd8009aee80676002bf24ce2ad24d08"

#endif

/* Firmware-volume driver ImageCodeType as reported by this firmware.
 * Standard AMI Aptio V reports EfiBootServicesCode (1); the MSI Z390
 * Aptio V reports 3 for every firmware-volume driver. */
#ifndef GU_DRIVER_CODE_TYPE
#define GU_DRIVER_CODE_TYPE 1
#endif

/* Loaded-image RVA where the pinned .text hash region starts. NVIDIA's
 * OptionROM nvgop-ga1xx images place .text at 0x280; the MSI firmware
 * INT10 video driver image places it at 0x2a0. */
#ifndef GU_NVGOP_TEXT_RVA
#define GU_NVGOP_TEXT_RVA 0x280U
#endif

/* 1 = the RTX firmware driver is identified by its PIWG firmware-volume
 * file GUID (BIOS-resident driver); 0 = by the OptionROM FILEPATH_DP name
 * "nvgop-ga1xx". */
#ifndef GU_NVGOP_PATH_PIWG
#define GU_NVGOP_PATH_PIWG 0
#endif
