"""Print a Windows hardware inventory for review; never run or authorize unlock."""

import argparse
import base64
import json
import os
from pathlib import Path
import re
import subprocess
import sys


# Query selected model/version fields only: no hostname, user, UUID or serial.
# No GPU utilities, raw PCI/MMIO, firmware reads, privilege elevation or network.
INVENTORY_SCRIPT = r"""
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$inventory = [ordered]@{}
$failures = @()
$queries = [ordered]@{
    board = @('Win32_BaseBoard', @('Manufacturer', 'Product'))
    cpu = @('Win32_Processor', @('Name', 'Manufacturer'))
    os = @('Win32_OperatingSystem', @('Caption', 'Version', 'BuildNumber', 'OSArchitecture'))
    bios = @('Win32_BIOS', @('Manufacturer', 'SMBIOSBIOSVersion'))
    video = @('Win32_VideoController', @('Name', 'PNPDeviceID', 'DriverVersion'))
}
foreach ($section in $queries.Keys) {
    try {
        $query = $queries[$section]
        $inventory[$section] = @(Get-CimInstance -ClassName $query[0] -Property $query[1] |
            Select-Object -Property $query[1])
        if (!$inventory[$section].Count) { $failures += $section }
    } catch {
        $inventory[$section] = @()
        $failures += $section
    }
}
$inventory['failed_sections'] = $failures
$inventory | ConvertTo-Json -Depth 6 -Compress
"""

FIELDS = {
    'board': ('Manufacturer', 'Product'),
    'cpu': ('Name', 'Manufacturer'),
    'os': ('Caption', 'Version', 'BuildNumber', 'OSArchitecture'),
    'bios': ('Manufacturer', 'SMBIOSBIOSVersion'),
    'video': ('Name', 'DriverVersion'),
}


def pci_identity(value):
    """Extract model IDs only, dropping the entire device-instance suffix."""
    if not isinstance(value, str):
        return None
    match = re.fullmatch(
        r'PCI\\VEN_([0-9a-f]{4})&DEV_([0-9a-f]{4})'
        r'(?:&SUBSYS_([0-9a-f]{8}))?(?:&REV_[0-9a-f]{2})?'
        r'(?:\\[^\\]*)?', value, flags=re.IGNORECASE,
    )
    if not match:
        return None
    vendor, device, subsystem = match.groups()
    return {
        'vendor': vendor.lower(), 'device': device.lower(),
        # Windows SUBSYS is device first, vendor last.
        'subsystem_vendor': subsystem[4:].lower() if subsystem else None,
        'subsystem_device': subsystem[:4].lower() if subsystem else None,
    }


def make_report(inventory):
    if not isinstance(inventory, dict):
        raise ValueError('Invalid inventory')
    failures = inventory.get('failed_sections', [])
    if not isinstance(failures, list):
        raise ValueError('Invalid failed_sections')
    missing = {name for name in failures if isinstance(name, str) and name in FIELDS}
    hardware = {}
    for section, fields in FIELDS.items():
        rows = inventory.get(section)
        if not isinstance(rows, list):
            rows = []
            missing.add(section)
        sanitized = []
        for row in rows:
            if not isinstance(row, dict):
                missing.add(section)
                continue
            selected = {}
            for field in fields:
                value = row.get(field)
                if not isinstance(value, str) or not value.strip():
                    selected[field] = None
                    missing.add(section)
                else:
                    selected[field] = value.strip()
            if section == 'video':
                identity = row.get('PNPDeviceID')
                selected['pci'] = pci_identity(identity)
                if selected['pci']:
                    selected['pci_status'] = 'parsed'
                elif not isinstance(identity, str) or not identity.strip() or identity.upper().startswith('PCI\\'):
                    selected['pci_status'] = 'unavailable'
                    missing.add(section)
                else:
                    selected['pci_status'] = 'non-pci-or-unrecognized'
            sanitized.append(selected)
        if not sanitized:
            missing.add(section)
        hardware[section] = sanitized
    return {
        'schema_version': 1,
        'purpose': 'compatibility-research-inventory',
        'collection_complete': not missing,
        'missing_sections': sorted(missing),
        'hardware': hardware,
        'assessment': {
            'status': 'not-assessed', 'can_unlock': False,
            'reason': 'Windows inventory cannot validate the exact pre-boot safety checks.',
        },
        'not_collected': [
            'serials, UUIDs, hostname, username and PNP device-instance suffixes',
            'Secure Boot state, active physical display route, PCI BDF and BAR layout',
            'PROM, firmware, cold state, ownership, DMA and cleanup readiness',
        ],
    }


def collect_inventory():
    if sys.platform != 'win32':
        raise RuntimeError('Collection requires Windows; use the manual issue form elsewhere.')
    # Use the OS PowerShell path rather than a PATH-resolved executable.
    windows = os.environ.get('SystemRoot', r'C:\Windows')
    powershell = Path(windows) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    encoded = base64.b64encode(INVENTORY_SCRIPT.encode('utf-16-le')).decode('ascii')
    try:
        result = subprocess.run(
            [str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive',
             '-EncodedCommand', encoded],
            capture_output=True, encoding='utf-8-sig', timeout=45, check=False,
        )
    except (OSError, subprocess.TimeoutExpired, UnicodeError):
        raise RuntimeError('Windows inventory collection failed or timed out.') from None
    if result.returncode:
        # Raw stderr can include local paths; never forward it into reports.
        raise RuntimeError('PowerShell inventory failed; use the manual issue form.')
    try:
        inventory = json.loads(result.stdout)
        return make_report(inventory)
    except (ValueError, TypeError):
        raise RuntimeError('Invalid inventory response; use the manual issue form.') from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        report = collect_inventory()
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1
    # ASCII JSON also works when redirected from Windows legacy code pages.
    print(json.dumps(report, indent=2, ensure_ascii=True))
    return 0 if report['collection_complete'] else 2


if __name__ == '__main__':
    sys.exit(main())
