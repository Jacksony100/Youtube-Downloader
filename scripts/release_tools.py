#!/usr/bin/env python3
"""Shared release helpers. Python is a build dependency, never an app dependency."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timedelta, timezone
from functools import partial

ROOT = Path(__file__).resolve().parent.parent
HEX = r"[0-9a-fA-F]{64}"


def version(root=ROOT):
    match = re.search(r"project\(\s*VideoDownloaderPro\s+VERSION\s+(\d+\.\d+\.\d+)\b", (root / "CMakeLists.txt").read_text())
    if not match:
        raise ValueError("CMake project version is missing")
    return match[1]


def checksum(text, filename, single=False):
    """Select the exact artifact, rejecting ambiguous or unlabelled multi-file sums."""
    matches = set()
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    for line in lines:
        gnu = re.fullmatch(rf"({HEX})\s+\*?(.+)", line)
        bsd = re.fullmatch(rf"SHA256 \((.+)\) = ({HEX})", line, re.I)
        if gnu and gnu[2] == filename:
            matches.add(gnu[1].lower())
        elif bsd and bsd[1] == filename:
            matches.add(bsd[2].lower())
        elif single:
            bare = re.fullmatch(rf"Hash\s*:\s*({HEX})", line, re.I)
            if len(lines) == 1:
                bare = re.fullmatch(rf"(?:Hash\s*:\s*)?({HEX})", line, re.I)
            if bare:
                matches.add(bare[1].lower())
            path = re.fullmatch(r"Path\s*:\s*(.+)", line, re.I)
            if path and PurePosixPath(path[1].replace("\\", "/")).name != filename:
                raise ValueError("Single-artifact checksum names a different artifact")
    if len(matches) != 1:
        raise ValueError(f"Missing or ambiguous SHA256 for {filename}")
    return matches.pop()


def sha256(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def checked_child(parent, child):
    parent, child = Path(parent).resolve(), Path(child).resolve()
    if child == parent or not child.is_relative_to(parent):
        raise ValueError(f"Path escapes staging directory: {child}")
    return child


class HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlparse(newurl).scheme != "https":
            raise ValueError("Runtime redirect must use HTTPS")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url, output, maximum=512 * 1024 * 1024):
    if urllib.parse.urlparse(url).scheme != "https":
        raise ValueError("Runtime downloads must use HTTPS")
    deadline = time.monotonic() + 600
    request = urllib.request.Request(url, headers={"User-Agent": f"VideoDownloaderPro/{version()}"})
    opener = urllib.request.build_opener(HttpsRedirect)
    total = 0
    with opener.open(request, timeout=45) as response, Path(output).open("wb") as target:
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > maximum or time.monotonic() > deadline:
                raise ValueError("Runtime download exceeds size or time limit")
            target.write(chunk)
    if not total:
        raise ValueError("Runtime download is empty")


def extract_binary(archive, name, target):
    """Copy exactly one regular binary; never trust an archive path for output."""
    with zipfile.ZipFile(archive) as source:
        matches = []
        for entry in source.infolist():
            path = PurePosixPath(entry.filename.replace("\\", "/"))
            if path.is_absolute() or ".." in path.parts or ":" in entry.filename:
                raise ValueError("Unsafe archive member")
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Archive symlink is not permitted")
            if path.name == name and not entry.is_dir():
                matches.append(entry)
        if len(matches) != 1 or matches[0].file_size > 512 * 1024 * 1024:
            raise ValueError(f"Expected exactly one bounded archive binary: {name}")
        with source.open(matches[0]) as stream, Path(target).open("wb") as output:
            shutil.copyfileobj(stream, output, 1024 * 1024)


def validate_lock(manifest):
    if manifest.get("schema") != 1 or not manifest.get("tools"):
        raise ValueError("Unsupported toolchain lock schema")
    for key, tool in manifest["tools"].items():
        if not tool.get("artifacts"):
            raise ValueError(f"No artifacts for {key}")
        for item in tool["artifacts"]:
            if urllib.parse.urlparse(item["url"]).scheme != "https":
                raise ValueError("Non-HTTPS artifact")
            if Path(item["fileName"]).name != item["fileName"] or "\\" in item["fileName"]:
                raise ValueError("Unsafe artifact name")
            if item.get("sha256"):
                if not re.fullmatch(HEX, item["sha256"]) or len(set(item["sha256"])) == 1:
                    raise ValueError("Invalid artifact digest")
            elif urllib.parse.urlparse(item.get("checksumUrl", "")).scheme != "https":
                raise ValueError("Artifact lacks integrity source")
            for binary in item["binaries"]:
                if not re.fullmatch(r"[A-Za-z0-9_.-]+", binary["name"]) or not re.fullmatch(r"[A-Za-z0-9_-]+", binary["target"]):
                    raise ValueError("Unsafe binary name")


def binary_version(binary, arguments):
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, errors="replace", timeout=30, check=True)
    output = (result.stdout or result.stderr).strip()
    if not output:
        raise ValueError(f"Empty version output: {binary.name}")
    return output.splitlines()[0]


def prepare_tools(platform, output, reuse=False):
    lock = json.loads((ROOT / "runtime/toolchain-lock.json").read_text())
    validate_lock(lock)
    output = checked_child(ROOT / "build_assets", output)
    output.mkdir(parents=True, exist_ok=True)
    manifest_file = output / "manifest.json"
    descriptors = [lock["tools"][f"{tool}-{platform}"] for tool in ("yt-dlp", "deno", "ffmpeg")]
    extension = ".exe" if platform.startswith("windows") else ""
    if reuse:
        stored = json.loads(manifest_file.read_text())
        if stored.get("schema") != 3:
            raise ValueError("Verified runtime cache is required for reuse")
        for tool in ("yt_dlp", "deno", "ffmpeg", "ffprobe"):
            filename = tool.replace("_", "-") + extension
            item = stored[tool]
            if item.get("verified") is not True or sha256(output / filename) != item["sha256"]:
                raise ValueError(f"Cached runtime integrity failed: {filename}")
        print(f"[OK] Verified cached runtime: {output}")
        return
    manifest = {"schema": 3}
    with tempfile.TemporaryDirectory(prefix="runtime-", dir=output.parent) as stage_dir:
        stage = Path(stage_dir)
        for descriptor in descriptors:
            for artifact in descriptor["artifacts"]:
                payload = stage / artifact["fileName"]
                print(f"[INFO] Fetching {artifact['fileName']}", flush=True)
                download(artifact["url"], payload)
                expected = artifact.get("sha256")
                if not expected:
                    sums = stage / (artifact["fileName"] + ".checksums")
                    download(artifact["checksumUrl"], sums, maximum=4 * 1024 * 1024)
                    expected = checksum(sums.read_text(encoding="utf-8-sig"), artifact["fileName"], artifact.get("singleArtifactChecksum", False))
                if sha256(payload) != expected.lower():
                    raise ValueError(f"SHA256 mismatch: {artifact['fileName']}")
                for binary in artifact["binaries"]:
                    filename = binary["target"].replace("_", "-") + extension
                    target = stage / ("verified-" + filename)
                    if artifact.get("archive"):
                        extract_binary(payload, binary["name"], target)
                    else:
                        shutil.copyfile(payload, target)
                    target.chmod(0o755)
                    reported = binary_version(target, binary["versionArgs"])
                    key = binary["target"].replace("-", "_")
                    manifest[key] = {"sha256": sha256(target), "version": reported, "verified": True, "source": artifact["url"], "artifactSha256": expected.lower()}
        # All tools passed checks before the packaging cache is updated.
        for key in ("yt_dlp", "deno", "ffmpeg", "ffprobe"):
            filename = key.replace("_", "-") + extension
            os.replace(stage / ("verified-" + filename), output / filename)
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        os.replace(stage / "manifest.json", manifest_file)
    print("[OK] All four packaged runtime binaries verified and version checked")


def validate_managed_runtime(data):
    manifest = json.loads((data / "runtime/manifest.json").read_text())
    if manifest.get("schema") != 3:
        raise ValueError("Packaged smoke did not produce a current runtime manifest")
    for key in ("yt_dlp", "deno", "ffmpeg", "ffprobe"):
        item = manifest[key]
        binary = checked_child(data, item["path"])
        if not item.get("verified") or not item.get("version") or sha256(binary) != item.get("sha256"):
            raise ValueError(f"Packaged smoke failed managed runtime integrity: {key}")


def smoke(executable, data, timeout=90):
    data = checked_child(ROOT / "build-cpp", data)
    data.mkdir(parents=True, exist_ok=False)
    environment = os.environ.copy()
    environment.update({"VDP_SMOKE_TEST": "1", "VDP_DATA_ROOT": str(data), "LOCALAPPDATA": str(data), "APPDATA": str(data), "HOME": str(data), "XDG_DATA_HOME": str(data)})
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        environment.pop(key, None)
    executable = Path(executable).resolve()
    if os.name == "nt":
        windows = Path(environment.get("SystemRoot", "C:/Windows"))
        environment["PATH"] = os.pathsep.join((str(executable.parent), str(windows / "System32"), str(windows)))
    else:
        environment["PATH"] = os.pathsep.join((str(executable.parent), "/usr/bin", "/bin", "/usr/sbin", "/sbin"))
    subprocess.run([str(executable)], cwd=executable.parent, env=environment, timeout=timeout, check=True)
    validate_managed_runtime(data)
    print(f"[OK] Packaged smoke passed: {executable}")


def rehash_signed_tools(directory):
    """Signing changes Mach-O bytes; retain verified upstream provenance separately."""
    directory = checked_child(ROOT / "dist", directory)
    manifest_file = directory / "manifest.json"
    manifest = json.loads(manifest_file.read_text())
    for key in ("yt_dlp", "deno", "ffmpeg", "ffprobe"):
        item = manifest[key]
        if item.get("verified") is not True or not item.get("artifactSha256"):
            raise ValueError("Signing requires a previously verified runtime manifest")
        item["unsignedBinarySha256"] = item["sha256"]
        item["sha256"] = sha256(directory / key.replace("_", "-"))
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")


def engine_smoke(toolchain, arguments_helper, evidence):
    """Exercise real binaries on generated local media using production arguments."""
    toolchain = Path(toolchain).resolve()
    evidence = checked_child(ROOT / "build-cpp", evidence)
    evidence.mkdir(parents=True, exist_ok=False)
    extension = ".exe" if os.name == "nt" else ""
    tools = {name: toolchain / (name + extension) for name in ("yt-dlp", "deno", "ffmpeg", "ffprobe")}
    manifest = json.loads((toolchain / "manifest.json").read_text())
    for name, binary in tools.items():
        record = manifest[name.replace("-", "_")]
        if not record.get("verified") or sha256(binary) != record.get("sha256"):
            raise ValueError(f"Engine smoke requires verified tools: {name}")
    media = evidence / "media"
    media.mkdir()
    generated = media / "source.mp4"
    subprocess.run([str(tools["ffmpeg"]), "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=320x180:rate=12", "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=44100",
                    "-t", "2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-movflags", "+faststart", str(generated)],
                   check=True, timeout=30, capture_output=True)

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, format, *args):
            return

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(media)))
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    url = f"http://127.0.0.1:{server.server_port}/source.mp4"
    environment = os.environ.copy()
    for key in list(environment):
        if key.lower().endswith("_proxy"):
            environment.pop(key)
    environment["NO_PROXY"] = "127.0.0.1,localhost"
    report = {"validatedAtMSK": datetime.now(timezone(timedelta(hours=3))).isoformat(), "argumentsSource": "vdp::buildDownloadArguments", "runs": []}
    try:
        for preset in ("best", "mp3"):
            destination = evidence / preset
            destination.mkdir()
            generated_arguments = subprocess.run([str(Path(arguments_helper).resolve()), url, preset, str(destination),
                                                  str(toolchain), str(tools["deno"])], check=True, capture_output=True, text=True, encoding="utf-8", timeout=15)
            arguments = json.loads(generated_arguments.stdout)
            result = subprocess.run([str(tools["yt-dlp"]), *arguments], capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=90, env=environment)
            output = result.stdout + "\n" + result.stderr
            (evidence / f"{preset}-process.log").write_text(output, encoding="utf-8")
            if result.returncode:
                raise ValueError(f"Real engine failed ({preset}, exit {result.returncode}); see {evidence / (preset + '-process.log')}")
            paths = re.findall(r"^vdppath:(.+)$", output, re.M)
            if len(paths) != 1:
                raise ValueError(f"Engine did not emit exactly one output path: {preset}")
            downloaded = checked_child(destination, paths[0].strip())
            if not downloaded.is_file() or not downloaded.stat().st_size:
                raise ValueError(f"Engine output is missing: {preset}")
            if not re.search(r"^download:\s*\d+(?:\.\d+)?%\|", output, re.M):
                raise ValueError(f"Production arguments did not emit download progress: {preset}")
            if preset == "mp3" and not re.search(r"^postprocess:", output, re.M):
                raise ValueError("Production arguments did not emit post-processing progress")
            probe = subprocess.run([str(tools["ffprobe"]), "-v", "error", "-show_entries", "stream=codec_type,codec_name",
                                    "-of", "json", str(downloaded)], check=True, capture_output=True, text=True, timeout=15)
            streams = json.loads(probe.stdout)["streams"]
            if preset == "best" and {stream["codec_type"] for stream in streams} != {"audio", "video"}:
                raise ValueError("Downloaded MP4 does not contain both audio and video")
            if preset == "mp3" and (len(streams) != 1 or streams[0].get("codec_name") != "mp3"):
                raise ValueError("Extracted audio is not a valid MP3")
            report["runs"].append({"preset": preset, "arguments": arguments, "outputPath": str(downloaded),
                                   "sha256": sha256(downloaded), "bytes": downloaded.stat().st_size,
                                   "streams": streams, "progress": True, "postProcessing": "postprocess:" in output})
    finally:
        server.shutdown()
        server.server_close()
        serving.join(timeout=5)
    (evidence / "engine-smoke-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] Real engine MP4 + MP3 smoke passed with production arguments: {evidence}")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("version")
    commands.add_parser("validate-lock")
    prepare = commands.add_parser("prepare-tools")
    prepare.add_argument("platform")
    prepare.add_argument("output", type=Path)
    prepare.add_argument("--reuse-verified", action="store_true")
    launch = commands.add_parser("smoke")
    launch.add_argument("executable")
    launch.add_argument("data", type=Path)
    sums = commands.add_parser("checksums")
    sums.add_argument("files", nargs="+", type=Path)
    signed = commands.add_parser("rehash-signed-tools")
    signed.add_argument("directory", type=Path)
    engine = commands.add_parser("engine-smoke")
    engine.add_argument("toolchain", type=Path)
    engine.add_argument("arguments_helper", type=Path)
    engine.add_argument("evidence", type=Path)
    args = parser.parse_args()
    if args.command == "version":
        print(version())
    elif args.command == "validate-lock":
        validate_lock(json.loads((ROOT / "runtime/toolchain-lock.json").read_text()))
        print("[OK] Runtime lock schema and integrity sources")
    elif args.command == "prepare-tools":
        prepare_tools(args.platform, args.output, args.reuse_verified)
    elif args.command == "smoke":
        smoke(args.executable, args.data)
    elif args.command == "checksums":
        for path in args.files:
            if not path.is_file() or not path.stat().st_size:
                raise ValueError(f"Missing distributable: {path}")
            print(f"{sha256(path)}  {path.name}")
    elif args.command == "rehash-signed-tools":
        rehash_signed_tools(args.directory)
    elif args.command == "engine-smoke":
        engine_smoke(args.toolchain, args.arguments_helper, args.evidence)


if __name__ == "__main__":
    main()
