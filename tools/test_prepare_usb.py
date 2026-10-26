"""Packaging containment checks; uses fake bytes, never hardware or real firmware."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from prepare_usb import prepare


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'source'
        self.assets = self.base / 'assets'
        self.output = self.base / 'output'
        self.assets.mkdir()
        for name, content in {
            'release/EFI/BOOT/BOOTX64.EFI': b'fake EFI',
            'unlock/app.c': b'fake source', 'LICENSE': b'license',
            'LICENSES/notice.txt': b'notice', 'THIRD_PARTY_NOTICES.md': b'notices',
            'docs/GUIDE.ko.md': b'guide',
            'docs/GUIDE.en.md': b'English guide',
        }.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        self.write('release/provenance.json', {
            'efi_sha256': self.hash(b'fake EFI'),
            'corresponding_sources': [{'path': 'unlock/app.c', 'sha256': self.hash(b'fake source')}],
        })
        specs = []
        for i in range(5):
            name = f'asset{i}.bin'
            data = bytes([i])
            (self.assets / name).write_bytes(data)
            specs.append({'Name': name, 'Bytes': len(data), 'Sha256': self.hash(data)})
        self.write('unlock/assets.json', specs)
        self.refresh()

    @staticmethod
    def hash(data):
        return hashlib.sha256(data).hexdigest()

    def write(self, relative, value):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def refresh(self):
        entries = [{'path': p.relative_to(self.root).as_posix(), 'bytes': p.stat().st_size,
                    'sha256': self.hash(p.read_bytes())}
                   for p in sorted(self.root.rglob('*')) if p.is_file() and p.name != 'SOURCE-MANIFEST.json']
        self.write('SOURCE-MANIFEST.json', {'files': entries})

    def test_complete_tree_with_source_and_no_firmware_in_source_zip(self):
        prepare(self.root, self.assets, self.output)
        self.assertEqual((self.output / 'EFI/BOOT/BOOTX64.EFI').read_bytes(), b'fake EFI')
        self.assertEqual(len(list((self.output / 'RTX3090-UNLOCK/ASSETS').iterdir())), 5)
        readme = (self.output / 'USB-README.md').read_text(encoding='utf-8')
        for language in ('en', 'ko'):
            guide = f'RTX3090-UNLOCK/docs/GUIDE.{language}.md'
            self.assertIn(guide, readme)
            self.assertTrue((self.output / guide).is_file())
        with zipfile.ZipFile(self.output / 'RTX3090-UNLOCK/SOURCE.zip') as archive:
            self.assertIn('unlock/app.c', archive.namelist())
            self.assertIn('SOURCE-MANIFEST.json', archive.namelist())
            self.assertFalse(any(name.endswith('.bin') for name in archive.namelist()))

    def test_bad_asset_rejected_before_output_created(self):
        (self.assets / 'asset0.bin').write_bytes(b'wrong')
        with self.assertRaises(ValueError):
            prepare(self.root, self.assets, self.output)
        self.assertFalse(self.output.exists())

    def test_modified_source_rejected(self):
        (self.root / 'unlock/app.c').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            prepare(self.root, self.assets, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_destination_preserved(self):
        self.output.mkdir()
        sentinel = self.output / 'keep.txt'
        sentinel.write_text('keep', encoding='utf-8')
        with self.assertRaises(ValueError):
            prepare(self.root, self.assets, self.output)
        self.assertEqual(sentinel.read_text(encoding='utf-8'), 'keep')

    def test_path_traversal_rejected(self):
        self.write('SOURCE-MANIFEST.json', {'files': [{'path': '../escape', 'bytes': 0, 'sha256': ''}]})
        with self.assertRaises(ValueError):
            prepare(self.root, self.assets, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
