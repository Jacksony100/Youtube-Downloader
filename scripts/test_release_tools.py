import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import release_tools as release


class ReleaseSecurityTests(unittest.TestCase):
    def test_exact_gnu_filename(self):
        first, second = "1" * 64, "aB" * 32
        self.assertEqual(release.checksum(f"{first}  wrong.exe\r\n{second} *yt-dlp.exe\r\n", "yt-dlp.exe"), second.lower())
        with self.assertRaises(ValueError):
            release.checksum(f"{first}  wrong-yt-dlp.exe", "yt-dlp.exe")

    def test_bsd_filename(self):
        self.assertEqual(release.checksum(f"SHA256 (tool.zip) = {'b' * 64}", "tool.zip"), "b" * 64)

    def test_single_document_only(self):
        for text in ("a" * 64, "Hash: " + "a" * 64):
            self.assertEqual(release.checksum(text, "tool.zip", True), "a" * 64)
            with self.assertRaises(ValueError):
                release.checksum(text, "tool.zip", False)
        with self.assertRaises(ValueError):
            release.checksum("Hash: " + "a" * 64 + "\nHash: " + "b" * 64, "tool.zip", True)

    def test_ambiguous_checksum(self):
        with self.assertRaises(ValueError):
            release.checksum(f"{'a' * 64}  file\n{'b' * 64}  file", "file")

    def test_powershell_checksum_document(self):
        text = f"Algorithm : SHA256\r\nHash : {'ab' * 32}\r\nPath : C:\\build\\tool.zip\r\n"
        self.assertEqual(release.checksum(text, "tool.zip", True), "ab" * 32)
        with self.assertRaises(ValueError):
            release.checksum(text, "different.zip", True)

    def test_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertEqual(release.checked_child(root, root / "ok"), root / "ok")
            for path in (root, root / "../outside", root / "../sibling/file"):
                with self.assertRaises(ValueError):
                    release.checked_child(root, path)

    def test_archive_does_not_trust_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for member in ("../tool.exe", "/tool.exe", "C:/tool.exe", "..\\tool.exe"):
                with zipfile.ZipFile(root / "bad.zip", "w") as archive:
                    archive.writestr(member, "bad")
                with self.assertRaises(ValueError):
                    release.extract_binary(root / "bad.zip", "tool.exe", root / "result")
                self.assertFalse((root / "result").exists())

    def test_archive_binary_and_duplicate(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with zipfile.ZipFile(root / "good.zip", "w") as archive:
                archive.writestr("tools/bin/tool.exe", "verified")
            release.extract_binary(root / "good.zip", "tool.exe", root / "result")
            self.assertEqual((root / "result").read_text(), "verified")
            with zipfile.ZipFile(root / "good.zip", "a") as archive:
                archive.writestr("other/tool.exe", "other")
            with self.assertRaises(ValueError):
                release.extract_binary(root / "good.zip", "tool.exe", root / "result")

    def test_https_only(self):
        with self.assertRaises(ValueError):
            release.download("http://example.com/tool", "unused")
        handler = release.HttpsRedirect()
        with self.assertRaises(ValueError):
            handler.redirect_request(None, None, 302, "", {}, "http://example.com/tool")

    def test_lock_schema_integrity(self):
        lock = json.loads((release.ROOT / "runtime/toolchain-lock.json").read_text())
        release.validate_lock(lock)
        artifact = next(iter(lock["tools"].values()))["artifacts"][0]
        artifact["checksumUrl"] = "http://unsafe.example/checksum"
        artifact.pop("sha256", None)
        with self.assertRaises(ValueError):
            release.validate_lock(lock)

    def test_cmake_version(self):
        self.assertRegex(release.version(), r"^\d+\.\d+\.\d+$")

    def test_smoke_is_bounded_and_isolated(self):
        (release.ROOT / "build-cpp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=release.ROOT / "build-cpp") as folder:
            with patch("release_tools.subprocess.run") as run, patch("release_tools.validate_managed_runtime") as validate:
                release.smoke(__file__, Path(folder) / "data")
                environment = run.call_args.kwargs["env"]
                self.assertEqual(environment["VDP_DATA_ROOT"], str(Path(folder) / "data"))
                self.assertTrue(run.call_args.kwargs["check"])
                self.assertEqual(run.call_args.kwargs["timeout"], 90)
                validate.assert_called_once_with(Path(folder) / "data")

    def test_smoke_rejects_unverified_runtime(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            (data / "runtime").mkdir()
            (data / "tool").write_text("binary")
            entry = {"path": str(data / "tool"), "version": "1.0", "sha256": release.sha256(data / "tool"), "verified": True}
            manifest = {"schema": 3, **{key: dict(entry) for key in ("yt_dlp", "deno", "ffmpeg", "ffprobe")}}
            target = data / "runtime/manifest.json"
            target.write_text(json.dumps(manifest))
            release.validate_managed_runtime(data)
            manifest["ffprobe"]["verified"] = False
            target.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                release.validate_managed_runtime(data)


if __name__ == "__main__":
    unittest.main()
