"""Concurrent reference-cache publication must preserve complete oracle captures."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import tempfile
import threading
import unittest
import sys
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import battle_sweep as sweep


class ReferenceCacheTests(unittest.TestCase):
    def test_renderer_writers_publish_independent_complete_archives(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'reference.zip'
            sources = []
            for i in range(3):
                source = root / str(i)
                source.mkdir()
                (source / 'ares.trace').write_text(str(i) * 10000)
                sources.append(source)
            barrier = threading.Barrier(3)
            staging = []
            original = zipfile.ZipFile

            def simultaneous_open(path, *args, **kwargs):
                result = original(path, *args, **kwargs)
                if args and args[0] == 'w':
                    staging.append(path)
                    barrier.wait(timeout=10)
                return result

            with patch.object(sweep.zipfile, 'ZipFile', side_effect=simultaneous_open):
                with ThreadPoolExecutor(max_workers=3) as pool:
                    list(pool.map(lambda source: sweep.publish_reference_archive(str(archive), source), sources))
            self.assertEqual(len(set(staging)), 3)
            with original(archive) as result:
                self.assertIsNone(result.testzip())
                self.assertIn(result.read('ares.trace'), [str(i).encode() * 10000 for i in range(3)])
            self.assertEqual(list(root.glob('*.tmp')), [])

    def test_existing_capture_can_be_read_during_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'reference.zip'
            (root / 'ares.trace').write_text('original')
            sweep.publish_reference_archive(str(archive), root)
            (root / 'ares.trace').write_text('later writer')
            with zipfile.ZipFile(archive) as reader:
                sweep.publish_reference_archive(str(archive), root)
                self.assertEqual(reader.read('ares.trace'), b'original')
            with zipfile.ZipFile(archive) as reader:
                self.assertEqual(reader.read('ares.trace'), b'original')

    def test_corrupt_capture_is_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'reference.zip'
            archive.write_bytes(b'broken zip')
            (root / 'ares.trace').write_text('repaired')
            sweep.publish_reference_archive(str(archive), root)
            with zipfile.ZipFile(archive) as reader:
                self.assertEqual(reader.read('ares.trace'), b'repaired')

    def test_failed_writer_preserves_previous_archive_and_removes_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'reference.zip'
            archive.write_bytes(b'previous capture')
            (root / 'ares.trace').write_text('new capture')
            with patch.object(zipfile.ZipFile, 'write', side_effect=OSError('disk error')):
                with self.assertRaises(OSError):
                    sweep.publish_reference_archive(str(archive), root)
            self.assertEqual(archive.read_bytes(), b'previous capture')
            self.assertEqual(list(root.glob('*.tmp')), [])


if __name__ == '__main__':
    unittest.main()
