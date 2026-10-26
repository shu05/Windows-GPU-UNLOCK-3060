# From USB boot to Windows measurement

**English** | [한국어](GUIDE.ko.md)

## 1. Scope and preparation

Check [compatibility](COMPATIBILITY.en.md) first. The current executable is tied to the tested PC. Other machines require a separate port covering PCI/MMIO/DMA, GOP, PROM and memory-layout validation. A compatibility report is not permission to run the unlock.

Save your work before proceeding. Before changing Secure Boot settings on a PC using BitLocker/device encryption, make sure you have its recovery key. Do not modify the Windows EFI or recovery partitions.

- Connect the monitor to the motherboard's integrated graphics output.
- **Keep the RTX 3090 installed and its auxiliary power cables connected.** Disconnecting the RTX monitor cable does not mean disconnecting GPU power.
- Confirm that both firmware and Windows display normally through integrated graphics.
- The verified configuration uses x64 UEFI boot with Secure Boot disabled. Finish any required settings changes and saves before unlock.
- Prepare a FAT32 USB with sufficient space and back up its existing contents. The packaging tool does not format USBs or shrink disks.

## 2. Prepare the executable and assets

Successful-case EFI SHA-256:

```text
f5cbf7ae2884d2707ee888589f725b18bbc8b0f9672a511e3467ada02af6b0f5
```

Prepare the [five firmware files](ASSETS.en.md) in a local `assets/` directory, then run from the repository root:

```powershell
python .\tools\prepare_usb.py --assets .\assets --output .\usb-package
```

After size and hash validation, the tool creates a **new local folder**. Manually copy the **contents of** `usb-package` to the FAT32 USB root. Back up and inspect existing destination files before replacing anything.

```text
USB:/
  EFI/BOOT/BOOTX64.EFI
  RTX3090-UNLOCK/
    ASSETS/
      booter_load_ga102.bin
      v67-ga102-610.43.03.bin
      gsp_ga10x.bin
      gsp_boot_ga102.bin
      gsp_boot_desc_ga102.bin
    LICENSES/
    SOURCE.zip
    docs/
  USB-README.md
```

Copying only `BOOTX64.EFI` is insufficient. `RTX3090-UNLOCK` must be writable for logs. See [build instructions](BUILD.en.md) if building from source.

## 3. Run the unlock on the verified system

1. Save workloads such as ComfyUI and their outputs. Fully shut down Windows, for example by holding Shift while selecting Shut down, then power on. Preflight can reject a warm state already used by a driver.
2. On the tested ASUS system, press **F8 → UEFI USB**.
3. Check that the profile is `entryLatchedSs1Full` and preflight passes.
4. Press **U once**. Esc or waiting 30 seconds without confirmation cancels.
5. Do not remove the USB or reboot during execution.
6. Confirm `UEFI transaction completed and cleaned up`. The final log must show `status=0` and `clean=1`.

## 4. Continue to Windows without reset

After the app returns to firmware, select:

**F7 → Advanced Mode → Boot → Boot Override → Windows Boot Manager**

This is the path confirmed by the operator on the tested ASUS PC. Menu locations and reset behavior on other boards or firmware versions are unverified.

**Do not press F10 or select Save Changes & Reset.** Another POST/reset may discard the state. If a reset happens, do not assume Windows performance improved; measure again. If the USB app opens again, exit with Esc. Do not repeat U in the same boot.

The EFI cannot launch Windows directly. An alternative handoff for boards without Boot Override, or where Boot Override resets the machine, has not been verified.

## 5. Verify results and handle failure

Keep the display on integrated graphics after entering Windows and run the [benchmark](BENCHMARK.en.md). Use the same executable, libraries, power/clock settings and workload conditions. The driver version in `nvidia-smi` does not prove unlock success.

- `STOP / POWER OFF`, a hang or cleanup failure: fully power off; do not continue to Windows.
- Rejected compatibility checks: inspect `RTX3090-UNLOCK/RUNxx/REPORT.TXT`; do not bypass them.
- UEFI success or selector readback alone does not establish a Windows performance gain.
- To return to normal boot, power off, remove the USB and boot Windows Boot Manager. No Windows EFI modification is needed.
- Do not assume persistence across subsequent boots. Apply the procedure again and measure as needed.

This guide covers the legacy manual USB version. Follow the [separate internal-disk guide](INTERNAL-BOOT.en.md) for the new manual handoff and automatic variant awaiting firmware validation. Never delete the existing Windows ESP.
