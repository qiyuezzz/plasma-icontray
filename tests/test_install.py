# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("icontray_manager", ROOT / "tools/manage.py")
manager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager)


class InstallLifecycleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="icontray-install-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "installation with spaces"
        self.plugin = self.prefix / "lib/qt6/plugins/plasma/applets" / manager.PLUGIN
        self.translation = self.prefix / "share/locale/zh_CN/LC_MESSAGES/icontray.mo"
        self.env = self.root / "config/plasma-workspace/env/icontray.sh"
        self.receipt = self.prefix / manager.RECEIPT

    def create_installation(self):
        helpers = [self.prefix / "bin/icontray-uninstall", self.prefix / "share/icontray/manage.py"]
        for path in [self.plugin, self.translation, *helpers]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("test")
        shutil.copy2(ROOT / "tools/icontray-uninstall", helpers[0])
        shutil.copy2(ROOT / "tools/manage.py", helpers[1])
        self.env.parent.mkdir(parents=True)
        self.env.write_text(manager.ENV_MARKER + "\nexport QT_PLUGIN_PATH=" + manager.shlex.quote(str(self.plugin.parent.parent.parent)) + "\n")
        files = [self.plugin, self.translation, *helpers, self.receipt]
        self.receipt.write_text(json.dumps({"schema": 1, "prefix": str(self.prefix), "system": False,
                                           "files": list(map(str, files)), "session_env": str(self.env),
                                           "plugin_dirs": [str(self.plugin.parent.parent.parent)]}))
        return files

    def test_standalone_uninstall_without_source_or_build(self):
        files = self.create_installation()
        unrelated = self.prefix / "share/keep-me.txt"
        unrelated.write_text("unrelated application")
        launcher = self.prefix / "bin/icontray-uninstall"
        preview = subprocess.run([launcher, "--dry-run"], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertIn("Would remove:", preview.stdout)
        self.assertTrue(all(p.exists() for p in files))
        result = subprocess.run([launcher], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(all(not p.exists() for p in files))
        self.assertFalse(self.env.exists())
        self.assertEqual(unrelated.read_text(), "unrelated application")

    def test_unexpected_receipt_cannot_remove_other_files(self):
        self.create_installation()
        unrelated = self.prefix / "share/keep-me.txt"
        unrelated.write_text("keep")
        data = json.loads(self.receipt.read_text())
        data["files"].append(str(unrelated))
        self.receipt.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "unexpected"):
            manager.uninstall(self.prefix, self.root / "absent-build", False, self.env, False, False, False)
        self.assertTrue(self.plugin.exists())
        self.assertTrue(unrelated.exists())

    def test_legacy_manifest_and_custom_environment_preserved(self):
        self.plugin.parent.mkdir(parents=True)
        self.plugin.write_text("legacy")
        build = self.root / "old-build"
        build.mkdir()
        (build / "install_manifest.txt").write_text(str(self.plugin) + "\n")
        self.env.parent.mkdir(parents=True)
        self.env.write_text("# user's own environment\n")
        manager.uninstall(self.prefix, build, False, self.env, False, False, False)
        self.assertFalse(self.plugin.exists())
        self.assertEqual(self.env.read_text(), "# user's own environment\n")

    def test_install_preflight_preserves_unmanaged_environment(self):
        self.env.parent.mkdir(parents=True)
        self.env.write_text("# custom environment\n")
        result = subprocess.run(["python3", ROOT / "tools/manage.py", "install", "--prefix", self.prefix,
                                 "--build-dir", self.root / "absent-build", "--cmake", "/does/not/exist"],
                                env={**manager.os.environ, "XDG_CONFIG_HOME": str(self.root / "config")},
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not managed by IconTray", result.stderr)
        self.assertFalse(self.prefix.exists())
        self.assertEqual(self.env.read_text(), "# custom environment\n")


if __name__ == "__main__":
    unittest.main()
