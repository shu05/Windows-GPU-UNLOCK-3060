"""Synthetic offline evidence tests. No CUDA, NVIDIA tools or EFI execution."""

import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from compare_results import NAMES, compare, main


def write_json(path, value):
    # Windows PowerShell 5.1 Set-Content -Encoding utf8 emits a BOM.
    path.write_text(json.dumps(value), encoding='utf-8-sig')


def seal(directory):
    names = ['comparison-context.json', 'trial-1.json', 'trial-2.json', 'trial-3.json']
    write_json(directory / 'comparison-complete.json', {
        'schema_version': 1, 'files': [
            {'Path': name, 'SHA256': hashlib.sha256((directory / name).read_bytes()).hexdigest()}
            for name in names]})


def fixture(directory, ratios=(1, 1, 1), gpu='NVIDIA GeForce RTX 3090', card='3090'):
    directory.mkdir()
    context = {
        'schema_version': 1,
        'artifacts': [{'Path': f'bin\\compute-benchmark-{card}.exe', 'SHA256': 'a' * 64},
                      {'Path': r'bin\cublas64_12.dll', 'SHA256': 'b' * 64}],
        'runtime': {'cuda_runtime': 12060, 'cublas_major': 12, 'cublas_minor': 6,
                    'cublas_patch': 0, 'gpu_workload_run': False},
        'gpu_settings': [gpu, '00000000:01:00.0',
                         '617.14', '350.00', '2100', '9751']}
    write_json(directory / 'comparison-context.json', context)
    for trial_index in range(1, 4):
        trial = {'gpu': gpu, 'pci': '0000:01:00.0',
                 'compute_capability': '8.6', 'cuda_runtime': 12060,
                 'cuda_driver_api': 13000, 'cublas': 120600, 'benchmarks': []}
        for name, base, ratio in zip(NAMES, (20000, 60000, 62000), ratios):
            gflops = base * ratio * (1 + (trial_index - 2) * 0.01)
            ms = 2 * 4096 ** 3 / (gflops * 1e6)
            trial['benchmarks'].append({
                'name': name, 'n': 4096, 'samples': 30, 'warmup_seconds': 2,
                'reference_checked': True, 'median_ms': ms,
                'min_ms': ms * 0.95, 'max_ms': ms * 1.05, 'gflops': gflops})
        write_json(directory / f'trial-{trial_index}.json', trial)
    seal(directory)


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.before, self.after = root / 'before', root / 'after'
        fixture(self.before)
        fixture(self.after, (1.01, 1.65, 1.65))

    def compare(self):
        return compare(self.before, self.after, same_machine_confirmed=True)

    def mutate(self, name, operation):
        path = self.after / name
        value = json.loads(path.read_text(encoding='utf-8-sig'))
        operation(value)
        write_json(path, value)
        seal(self.after)

    def test_same_machine_medians_and_fp32_control(self):
        result = self.compare()
        self.assertAlmostEqual(result['benchmarks'][1]['ratio'], 1.65)
        self.assertAlmostEqual(result['fp32_control_change_percent'], 1)
        self.assertEqual(len(result['benchmarks'][0]['before_trials_tflops']), 3)

    def test_each_supported_card_uses_its_own_executable(self):
        for card, gpu in [('3090', 'NVIDIA GeForce RTX 3090'),
                          ('3090ti', 'NVIDIA GeForce RTX 3090 Ti'),
                          ('3070ti', 'NVIDIA GeForce RTX 3070 Ti')]:
            with self.subTest(card=card):
                before = Path(self.temp.name) / f'{card}-before'
                after = Path(self.temp.name) / f'{card}-after'
                fixture(before, gpu=gpu, card=card)
                fixture(after, (1, 1.5, 1.5), gpu=gpu, card=card)
                result = compare(before, after, same_machine_confirmed=True)
                self.assertAlmostEqual(result['benchmarks'][1]['ratio'], 1.5)

    def test_different_gpu_models_cannot_be_compared(self):
        after = Path(self.temp.name) / 'other-card'
        fixture(after, gpu='NVIDIA GeForce RTX 3090 Ti', card='3090ti')
        with self.assertRaisesRegex(ValueError, 'differ'):
            compare(self.before, after, same_machine_confirmed=True)

    def test_gpu_and_executable_mismatch_is_refused(self):
        self.mutate('comparison-context.json',
                    lambda c: c['gpu_settings'].__setitem__(0, 'NVIDIA GeForce RTX 3090 Ti'))
        with self.assertRaisesRegex(ValueError, 'selected benchmark executable'):
            self.compare()

    def test_unknown_gpu_model_is_refused(self):
        self.mutate('comparison-context.json',
                    lambda c: c['gpu_settings'].__setitem__(0, 'NVIDIA GeForce RTX 4090'))
        with self.assertRaisesRegex(ValueError, 'Unsupported benchmark GPU'):
            self.compare()

    def test_confirmation_required(self):
        with self.assertRaisesRegex(ValueError, 'Confirm'):
            compare(self.before, self.after)

    def test_same_directory_refused(self):
        with self.assertRaisesRegex(ValueError, 'distinct'):
            compare(self.before, self.before, same_machine_confirmed=True)

    def test_changed_context_refused(self):
        for section, key, replacement in [
            ('gpu_settings', 2, 'different-driver'),
            ('gpu_settings', 3, '300.00'),
            ('gpu_settings', 4, '2000'),
            ('runtime', 'cublas_patch', 1)]:
            with self.subTest(section=section, key=key):
                path = self.after / 'comparison-context.json'
                original = path.read_bytes()
                self.mutate(path.name, lambda c: c[section].__setitem__(key, replacement))
                with self.assertRaises(ValueError):
                    self.compare()
                path.write_bytes(original)
                seal(self.after)

    def test_changed_executable_refused(self):
        self.mutate('comparison-context.json',
                    lambda c: c['artifacts'][0].__setitem__('SHA256', 'c' * 64))
        with self.assertRaisesRegex(ValueError, 'Build'):
            self.compare()

    def test_unavailable_power_refused(self):
        self.mutate('comparison-context.json',
                    lambda c: c['gpu_settings'].__setitem__(3, '[N/A]'))
        with self.assertRaisesRegex(ValueError, 'must be available'):
            self.compare()

    def test_artifact_order_and_hash_case_are_not_conditions(self):
        def reorder(context):
            context['artifacts'].reverse()
            for artifact in context['artifacts']:
                artifact['SHA256'] = artifact['SHA256'].upper()
        self.mutate('comparison-context.json', reorder)
        self.compare()

    def test_artifact_ambiguity_refused(self):
        self.mutate('comparison-context.json',
                    lambda c: c['artifacts'].append(dict(c['artifacts'][0])))
        with self.assertRaisesRegex(ValueError, 'Duplicate artifact'):
            self.compare()

    def test_missing_cublas_refused(self):
        self.mutate('comparison-context.json', lambda c: c['artifacts'].pop())
        with self.assertRaisesRegex(ValueError, 'cuBLAS'):
            self.compare()

    def test_invalid_measurements_refused(self):
        for key, value in [('gflops', float('nan')), ('median_ms', float('inf')),
                           ('gflops', 0), ('reference_checked', False),
                           ('samples', 5), ('n', 8192), ('warmup_seconds', True),
                           ('gflops', 1), ('min_ms', 10000)]:
            with self.subTest(key=key, value=value):
                path = self.after / 'trial-1.json'
                original = path.read_bytes()
                self.mutate(path.name,
                            lambda t: t['benchmarks'][0].__setitem__(key, value))
                with self.assertRaises(ValueError):
                    self.compare()
                path.write_bytes(original)
                seal(self.after)

    def test_duplicate_benchmark_refused(self):
        self.mutate('trial-1.json', lambda t: t['benchmarks'].__setitem__(1, t['benchmarks'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate benchmark'):
            self.compare()

    def test_trial_metadata_change_refused(self):
        self.mutate('trial-2.json', lambda t: t.__setitem__('cuda_driver_api', 12000))
        with self.assertRaisesRegex(ValueError, 'changed between trials'):
            self.compare()

    def test_missing_extra_and_partial_trials_refused(self):
        (self.after / 'trial-4.json').write_text('{}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'exactly'):
            self.compare()
        (self.after / 'trial-4.json').unlink()
        (self.after / 'trial-3.json').unlink()
        with self.assertRaises(OSError):
            self.compare()

    def test_partial_run_without_completion_refused(self):
        (self.after / 'comparison-complete.json').unlink()
        with self.assertRaises(OSError):
            self.compare()

    def test_post_completion_edit_refused(self):
        path = self.after / 'trial-1.json'
        path.write_bytes(path.read_bytes() + b' ')
        with self.assertRaisesRegex(ValueError, 'changed after run completion'):
            self.compare()

    def test_cli_refusal_has_no_result_on_stdout(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = main([str(self.before), str(self.after)])
        self.assertEqual(status, 2)
        self.assertEqual(out.getvalue(), '')
        self.assertIn('Comparison refused', err.getvalue())

    def test_cli_success_is_json(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = main([str(self.before), str(self.after), '--same-machine-confirmed'])
        self.assertEqual(status, 0)
        self.assertEqual(len(json.loads(out.getvalue())['benchmarks']), 3)


if __name__ == '__main__':
    unittest.main()
