"""Check package entry points preserve the shared portable launcher data policy."""
from pathlib import Path
import importlib.util
import os
import plistlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


class LauncherPackagingTests(unittest.TestCase):
    def test_signed_macos_bundle_does_not_override_portable_directory(self):
        plist = plistlib.loads(module('make_macos_bundle').PLIST.format(bid='test.pokestadium', ver='test').encode())
        self.assertNotIn('SHIP_HOME', plist.get('LSEnvironment', {}))
        self.assertEqual(plist['CFBundleExecutable'], 'pokestadium')

    @unittest.skipUnless(sys.platform.startswith('linux'), 'AppRun executes on Linux')
    def test_appimage_entry_point_uses_sidecar_directory_and_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mount = root / 'readonly mount'
            executable = mount / 'usr/bin/pokestadium'
            executable.parent.mkdir(parents=True)
            executable.write_text('#!/bin/sh\nprintf "%s" "$SHIP_HOME"\n', encoding='utf-8')
            executable.chmod(0o755)
            apprun = mount / 'AppRun'
            apprun.write_text(module('make_appimage').APPRUN, encoding='utf-8')
            apprun.chmod(0o755)
            artifact = root / 'download folder' / 'Stadium.AppImage'
            artifact.parent.mkdir()
            artifact.touch()
            env = dict(os.environ, APPIMAGE=str(artifact))
            env.pop('SHIP_HOME', None)
            result = subprocess.run([str(apprun)], cwd=root, env=env, check=True, capture_output=True, text=True)
            self.assertEqual(Path(result.stdout), artifact.parent)
            self.assertNotEqual(Path(result.stdout), executable.parent)
            chosen = root / 'chosen data'
            env['SHIP_HOME'] = str(chosen)
            result = subprocess.run([str(apprun)], cwd=root, env=env, check=True, capture_output=True, text=True)
            self.assertEqual(Path(result.stdout), chosen)
            env.pop('SHIP_HOME')
            env.pop('APPIMAGE')
            result = subprocess.run([str(apprun)], cwd=root, env=env, check=True, capture_output=True, text=True)
            self.assertEqual(Path(result.stdout), mount)


if __name__ == '__main__':
    unittest.main()
