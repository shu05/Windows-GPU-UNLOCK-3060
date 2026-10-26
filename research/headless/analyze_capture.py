"""Offline topology research. Never authorizes unlock or accesses firmware/GPU."""
import argparse
import hashlib
import json
from pathlib import Path
import re

MARKER = 'gpu-console-graph-v1 observational-only; no-detach no-unlock no-nvram-write'
GOP = '9042a9de-23dc-4a38-96fb-7aded080516a'
TEXT = '387477c2-69c7-11d2-8e39-00a0c969723b'
INPUT = '387477c1-69c7-11d2-8e39-00a0c969723b'


def device_path(block):
    rows = re.findall(r'^path-node raw=([0-9a-f]+)$', block, re.M)
    if not rows:
        return None
    nodes = [bytes.fromhex(row) for row in rows]
    for i, node in enumerate(nodes):
        if len(node) < 4 or int.from_bytes(node[2:4], 'little') != len(node):
            raise ValueError('Malformed device path node')
        if node[0] == 127 and (node != b'\x7f\xff\x04\x00' or i != len(nodes)-1):
            raise ValueError('Ambiguous device path termination')
    if nodes[-1] != b'\x7f\xff\x04\x00':
        raise ValueError('Incomplete device path')
    return b''.join(nodes)


def analyze(raw, capture_index=None):
    if len(raw) > 8 * 1024 * 1024:
        raise ValueError('Capture exceeds the diagnostic format limit')
    text = raw.decode('utf-8-sig').replace('\r\n', '\n')
    sections = text.split(MARKER)[1:]
    if not sections:
        raise ValueError('No recognized graph capture')
    if capture_index is None:
        if len(sections) != 1:
            raise ValueError('Multiple captures: explicitly select --capture-index')
        capture_index = 0
    if capture_index < 0 or capture_index >= len(sections):
        raise ValueError('Capture index out of range')
    section = sections[capture_index]
    if section.count('capture-complete=1\n') != 1 or 'capture-complete=0' in section:
        raise ValueError('Incomplete or ambiguous capture')
    section = section.split('capture-complete=1\n')[0]
    console = re.search(r'^console out=(0x[0-9a-f]+) error=(0x[0-9a-f]+) input=(0x[0-9a-f]+)$', section, re.M)
    if not console:
        raise ValueError('Missing system console handles')
    records = re.findall(r'^handle id=(0x[0-9a-f]+)\n(.*?)(?=^handle id=|\Z)', section, re.M | re.S)
    handles = dict(records)
    if not handles or len(handles) != len(records):
        raise ValueError('Missing or duplicate handle records')
    declared = re.search(r'^handles status=(0x[0-9a-f]+) count=(0x[0-9a-f]+)$', section, re.M)
    if not declared or int(declared[1], 16) or int(declared[2], 16) != len(handles):
        raise ValueError('Declared handle snapshot does not match records')
    if any(h not in handles for h in console.groups()):
        raise ValueError('Unresolved console handle')
    parents = []
    for h, block in records:
        cfg = re.search(r'^pci config-dwords=([^\n]+)$', block, re.M)
        if not cfg:
            continue
        words = [int(w, 16) for w in cfg[1].split()]
        if len(words) != 16 or 'pci config-status=0x0000000000000000' not in block:
            raise ValueError('Incomplete PCI identity record')
        path = device_path(block)
        if not path:
            raise ValueError('Missing PCI device path')
        role = 'other'
        if words[0] == 0x220410de and words[11] == 0x145410de:
            role = 'rtx3090_exact_id'
        elif words[0] == 0x164e1002 and words[11] == 0x88771043:
            role = 'amd_igpu_exact_id'
        parents.append((h, path[:-4], role))
    physical, virtual = [], 0
    for h, block in records:
        if 'protocol guid=' + GOP not in block:
            continue
        mode = re.search(r'^gop framebuffer=(0x[0-9a-f]+) bytes=(0x[0-9a-f]+)', block, re.M)
        if not mode:
            raise ValueError('Missing GOP mode record')
        fb, size = int(mode[1], 16), int(mode[2], 16)
        path = device_path(block)
        if not path and not fb and not size and h == console[1]:
            virtual += 1
            continue
        if not path or not fb or not size:
            raise ValueError('Unclassified GOP without physical geometry')
        matches = [p for p in parents if path.startswith(p[1])]
        # PCI bridges also prefix endpoint paths. Select the nearest PCI
        # ancestor, but never choose between duplicate equally deep parents.
        if matches:
            longest = max(len(p[1]) for p in matches)
            matches = [p for p in matches if len(p[1]) == longest]
        if len(matches) != 1:
            raise ValueError('GOP ancestry is unresolved or ambiguous')
        physical.append(matches[0][2])
    console_text = 'protocol guid=' + TEXT in handles[console[1]]
    input_protocol = 'protocol guid=' + INPUT in handles[console[3]]
    candidate = (physical == ['rtx3090_exact_id'] and
                 sum(p[2] == 'rtx3090_exact_id' for p in parents) == 1 and
                 console_text and input_protocol)
    return {
        'schema_version': 1,
        'input_sha256': hashlib.sha256(raw).hexdigest(),
        'capture_index': capture_index,
        'capture_count': len(sections),
        'handle_count': len(handles),
        'physical_gop_count': len(physical),
        'physical_gop_roles': physical,
        'virtual_console_gop_count': virtual,
        'system_text_output_present': console_text,
        'system_text_input_present': input_protocol,
        'single_rtx_display_topology_observed': candidate,
        'hardware_access': False,
        'authorizes_driver_detach': False,
        'authorizes_unlock': False,
        'headless_handoff_verified': False,
        'limitations': [
            'Offline parsing cannot establish capture freshness or real hardware provenance.',
            'Only PCI identity/path topology is classified; driver code, ownership safety, PROM, input liveness and Windows behavior are not validated.',
            'An installed iGPU with no active GOP is not equivalent evidence to a CPU without an iGPU.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--capture-index', type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        with args.capture.open('rb') as stream:
            raw = stream.read(8 * 1024 * 1024 + 1)
        result = analyze(raw, args.capture_index)
        text = json.dumps(result, indent=2) + '\n'
        if args.output:
            # Explicit new local output only: never replace previous evidence.
            with args.output.open('x', encoding='utf-8', newline='\n') as stream:
                stream.write(text)
        else:
            print(text, end='')
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(2, 'Capture rejected: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
