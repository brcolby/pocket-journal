from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import time
from typing import BinaryIO
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import uuid
import zipfile

from .config import TranscriptionProfile
from .transcription import WHISPER_CPP_DEFAULT_THREADS, _sha256_file


WHISPER_CPP_VERSION = "1.9.1"
WHISPER_CPP_RELEASE_COMMIT = "f049fff95a089aa9969deb009cdd4892b3e74916"
WHISPER_CPP_RELEASE_URL = (
    "https://github.com/ggml-org/whisper.cpp/releases/tag/v1.9.1"
)
WHISPER_CPP_LICENSE = "MIT"

MODEL_NAME = "ggml-base.en-q5_0.bin"
MODEL_BYTES = 55_308_851
MODEL_SHA256 = "ab8c5e525f2f38ca53ab6d7410cd805880e3598a9e9e6814bf81ad5469ec95fe"
MODEL_SOURCE_NAME = "ggml-base.en.bin"
MODEL_SOURCE_BYTES = 147_964_211
MODEL_SOURCE_SHA256 = "a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002"
MODEL_SOURCE_REVISION = "5359861c739e955e79d9a303bcbc70fb988958b1"
MODEL_SOURCE_URL = (
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
    f"{MODEL_SOURCE_REVISION}/{MODEL_SOURCE_NAME}?download=true"
)
MODEL_PROVENANCE = (
    f"{MODEL_SOURCE_URL} (SHA-256 {MODEL_SOURCE_SHA256}), quantized q5_0 with "
    f"whisper.cpp {WHISPER_CPP_VERSION}"
)
MODEL_LICENSE = "MIT"

DOWNLOAD_CHUNK_BYTES = 1024 * 1024
MAX_RUNTIME_ARCHIVE_BYTES = 64 * 1024 * 1024
MAX_RUNTIME_EXTRACTED_BYTES = 256 * 1024 * 1024
MAX_RUNTIME_ARCHIVE_MEMBERS = 512
DEFAULT_DOWNLOAD_TIMEOUT_SECONDS = 15 * 60.0
DEFAULT_PROCESS_TIMEOUT_SECONDS = 15 * 60.0


class TranscriptionSetupError(RuntimeError):
    pass


@dataclass(frozen=True)
class RuntimeAsset:
    system: str
    machine: str
    archive: str
    url: str
    bytes: int
    sha256: str
    executable_name: str
    quantizer_name: str


_RUNTIME_ASSETS = {
    ("linux", "x86_64"): RuntimeAsset(
        "linux",
        "x86_64",
        "tar.gz",
        "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/"
        "whisper-bin-ubuntu-x64.tar.gz",
        9_379_235,
        "f3bf3b4369a99b54665b0f19b88483b30de27f25963b0414235dea03198515c5",
        "whisper-cli",
        "whisper-quantize",
    ),
    ("linux", "aarch64"): RuntimeAsset(
        "linux",
        "aarch64",
        "tar.gz",
        "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/"
        "whisper-bin-ubuntu-arm64.tar.gz",
        4_555_819,
        "e0b66cd551ff6f2a28fabe3c6e89691eea037bb76833493abb9a71ca788994b3",
        "whisper-cli",
        "whisper-quantize",
    ),
    ("windows", "x86_64"): RuntimeAsset(
        "windows",
        "x86_64",
        "zip",
        "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/"
        "whisper-bin-x64.zip",
        7_982_101,
        "7d8be46ecd31828e1eb7a2ecdd0d6b314feafd82163038ab6092594b0a063539",
        "whisper-cli.exe",
        "whisper-quantize.exe",
    ),
}

# This is the exact Apple Silicon Homebrew executable used by the repository's
# benchmark evidence. Other local builds can be enrolled only with an explicit
# expected digest and provenance supplied by the operator.
_KNOWN_LOCAL_RUNTIMES = {
    ("darwin", "arm64", "e25f8bab12a19e4df44d8889f35944a89816d7dfb01c0f8e5de4727fb0939924"): {
        "source": (
            "Homebrew whisper-cpp 1.9.1 from "
            "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.1.tar.gz "
            "(source SHA-256 147267177eef7b22ec3d2476dd514d1b12e160e176230b740e3d1bd600118447)"
        ),
        "license": WHISPER_CPP_LICENSE,
        "archive_sha256": "",
    }
}


def _normalized_platform(system: str | None = None, machine: str | None = None) -> tuple[str, str]:
    selected_system = (system or platform.system()).strip().lower()
    selected_machine = (machine or platform.machine()).strip().lower()
    aliases = {
        "amd64": "x86_64",
        "x64": "x86_64",
        "arm64": "aarch64" if selected_system == "linux" else "arm64",
    }
    return selected_system, aliases.get(selected_machine, selected_machine)


def _validate_digest(value: str, field: str) -> str:
    digest = value.lower()
    if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise TranscriptionSetupError(f"{field} must be a 64-character hexadecimal SHA-256")
    return digest


def _download_verified(
    url: str,
    destination: Path,
    *,
    expected_bytes: int,
    expected_sha256: str,
    timeout_seconds: float,
) -> None:
    if not url.startswith("https://"):
        raise TranscriptionSetupError("artifact acquisition requires an HTTPS source")
    if expected_bytes <= 0 or expected_bytes > max(MODEL_SOURCE_BYTES, MAX_RUNTIME_ARCHIVE_BYTES):
        raise TranscriptionSetupError("artifact acquisition size is outside the supported guardrail")
    if timeout_seconds <= 0:
        raise TranscriptionSetupError("download timeout must be positive")

    deadline = time.monotonic() + timeout_seconds
    request = Request(url, headers={"User-Agent": "pocket-journal-partner/1"})
    digest = hashlib.sha256()
    received = 0
    try:
        with urlopen(request, timeout=min(timeout_seconds, 60.0)) as response:
            final_url = response.geturl()
            if not final_url.startswith("https://"):
                raise TranscriptionSetupError("artifact download redirected to a non-HTTPS source")
            raw_length = response.headers.get("Content-Length")
            if raw_length is not None:
                try:
                    content_length = int(raw_length)
                except ValueError as exc:
                    raise TranscriptionSetupError("artifact source returned an invalid Content-Length") from exc
                if content_length != expected_bytes:
                    raise TranscriptionSetupError(
                        f"artifact source size changed: expected {expected_bytes}, received {content_length}"
                    )
            with destination.open("xb") as handle:
                while True:
                    if time.monotonic() >= deadline:
                        raise TranscriptionSetupError(
                            f"artifact download exceeded the {timeout_seconds:g}s timeout"
                        )
                    chunk = response.read(min(DOWNLOAD_CHUNK_BYTES, expected_bytes - received + 1))
                    if not chunk:
                        break
                    received += len(chunk)
                    if received > expected_bytes:
                        raise TranscriptionSetupError(
                            f"artifact source exceeded the pinned {expected_bytes}-byte size"
                        )
                    digest.update(chunk)
                    handle.write(chunk)
                handle.flush()
                os.fsync(handle.fileno())
    except TranscriptionSetupError:
        raise
    except (HTTPError, URLError, TimeoutError, OSError) as exc:
        raise TranscriptionSetupError(
            f"artifact download failed or is offline: {exc}; retry the explicit setup command"
        ) from exc

    if received != expected_bytes:
        raise TranscriptionSetupError(
            f"artifact download was incomplete: expected {expected_bytes} bytes, received {received}"
        )
    actual_digest = digest.hexdigest()
    if actual_digest != expected_sha256:
        raise TranscriptionSetupError(
            f"artifact SHA-256 mismatch: expected {expected_sha256}, received {actual_digest}"
        )


def _safe_member_path(root: Path, archive_name: str) -> Path:
    pure = PurePosixPath(archive_name.replace("\\", "/"))
    if pure.is_absolute() or not pure.parts or any(part in {"", ".", ".."} for part in pure.parts):
        raise TranscriptionSetupError(f"runtime archive contains an unsafe path: {archive_name!r}")
    destination = root.joinpath(*pure.parts)
    if root.resolve() not in destination.resolve().parents:
        raise TranscriptionSetupError(f"runtime archive escapes its staging directory: {archive_name!r}")
    return destination


def _copy_archive_file(source: BinaryIO, destination: Path, size: int, mode: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    remaining = size
    with destination.open("xb") as handle:
        while remaining:
            chunk = source.read(min(DOWNLOAD_CHUNK_BYTES, remaining))
            if not chunk:
                raise TranscriptionSetupError("runtime archive member ended before its declared size")
            handle.write(chunk)
            remaining -= len(chunk)
        handle.flush()
        os.fsync(handle.fileno())
    os.chmod(destination, 0o755 if mode & 0o111 else 0o644)


def _extract_tar_verified(archive: Path, destination: Path) -> None:
    total = 0
    count = 0
    with tarfile.open(archive, mode="r:gz") as bundle:
        for member in bundle:
            count += 1
            if count > MAX_RUNTIME_ARCHIVE_MEMBERS:
                raise TranscriptionSetupError("runtime archive contains too many members")
            target = _safe_member_path(destination, member.name)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile():
                raise TranscriptionSetupError(
                    f"runtime archive contains a non-regular member: {member.name!r}"
                )
            total += member.size
            if total > MAX_RUNTIME_EXTRACTED_BYTES:
                raise TranscriptionSetupError("runtime archive exceeds the extracted-size guardrail")
            source = bundle.extractfile(member)
            if source is None:
                raise TranscriptionSetupError(f"runtime archive member is unreadable: {member.name!r}")
            with source:
                _copy_archive_file(source, target, member.size, member.mode)


def _extract_zip_verified(archive: Path, destination: Path) -> None:
    total = 0
    with zipfile.ZipFile(archive) as bundle:
        members = bundle.infolist()
        if len(members) > MAX_RUNTIME_ARCHIVE_MEMBERS:
            raise TranscriptionSetupError("runtime archive contains too many members")
        for member in members:
            target = _safe_member_path(destination, member.filename)
            unix_mode = member.external_attr >> 16
            if stat.S_ISLNK(unix_mode):
                raise TranscriptionSetupError(
                    f"runtime archive contains a symbolic link: {member.filename!r}"
                )
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            total += member.file_size
            if total > MAX_RUNTIME_EXTRACTED_BYTES:
                raise TranscriptionSetupError("runtime archive exceeds the extracted-size guardrail")
            with bundle.open(member) as source:
                _copy_archive_file(source, target, member.file_size, unix_mode or 0o644)


def _runtime_version(executable: Path, timeout_seconds: float) -> str:
    try:
        completed = subprocess.run(
            [str(executable), "--version"],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=min(timeout_seconds, 30.0),
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise TranscriptionSetupError(f"whisper.cpp runtime could not be inspected: {exc}") from exc
    output = f"{completed.stdout}\n{completed.stderr}"
    match = re.search(r"whisper\.cpp version:\s*([^\s]+)", output)
    if completed.returncode != 0 or match is None:
        detail = output.strip()[-300:] or f"exit status {completed.returncode}"
        raise TranscriptionSetupError(f"whisper.cpp runtime version probe failed: {detail}")
    return match.group(1)


def _find_sibling(executable: Path, name: str) -> Path:
    candidate = executable.resolve().with_name(name)
    if not candidate.is_file():
        raise TranscriptionSetupError(
            f"verified whisper.cpp runtime has no {name}; install the complete {WHISPER_CPP_VERSION} runtime"
        )
    return candidate


def _inspect_runtime(
    executable: Path,
    *,
    expected_sha256: str | None,
    source: str | None,
    license_name: str | None,
    archive_sha256: str = "",
    trusted_archive: bool = False,
    timeout_seconds: float,
    system: str | None = None,
    machine: str | None = None,
) -> dict[str, str]:
    try:
        resolved = executable.expanduser().resolve(strict=True)
    except OSError as exc:
        raise TranscriptionSetupError(f"whisper.cpp executable does not exist: {executable}") from exc
    if not resolved.is_file():
        raise TranscriptionSetupError(f"whisper.cpp executable is not a regular file: {resolved}")
    if os.name != "nt" and not os.access(resolved, os.X_OK):
        raise TranscriptionSetupError(f"whisper.cpp executable is not executable: {resolved}")

    executable_sha256 = _sha256_file(resolved)
    selected_system, selected_machine = _normalized_platform(system, machine)
    known = _KNOWN_LOCAL_RUNTIMES.get((selected_system, selected_machine, executable_sha256))
    if expected_sha256 is not None:
        expected = _validate_digest(expected_sha256, "runtime SHA-256")
        if executable_sha256 != expected:
            raise TranscriptionSetupError(
                f"runtime SHA-256 mismatch: expected {expected}, received {executable_sha256}"
            )
    elif not trusted_archive and known is None:
        raise TranscriptionSetupError(
            "local whisper.cpp runtime is not a repository-pinned artifact; pass its exact "
            "--runtime-sha256, --runtime-source, and --runtime-license to enroll it explicitly"
        )

    if known is not None:
        source = source or known["source"]
        license_name = license_name or known["license"]
        archive_sha256 = archive_sha256 or known["archive_sha256"]
    if not source or not license_name:
        raise TranscriptionSetupError(
            "runtime provenance is incomplete; pass --runtime-source and --runtime-license"
        )

    version = _runtime_version(resolved, timeout_seconds)
    if version != WHISPER_CPP_VERSION:
        raise TranscriptionSetupError(
            f"unsupported whisper.cpp version {version!r}; expected exactly {WHISPER_CPP_VERSION}"
        )
    if _sha256_file(resolved) != executable_sha256:
        raise TranscriptionSetupError("whisper.cpp executable changed during verification")
    quantizer_name = "whisper-quantize.exe" if selected_system == "windows" else "whisper-quantize"
    quantizer = _find_sibling(resolved, quantizer_name)
    return {
        "executable": str(resolved),
        "executable_sha256": executable_sha256,
        "version": version,
        "source": source,
        "license": license_name,
        "archive_sha256": archive_sha256,
        "quantizer": str(quantizer),
        "quantizer_sha256": _sha256_file(quantizer),
    }


def _acquire_runtime(
    root: Path,
    *,
    timeout_seconds: float,
    process_timeout_seconds: float,
    system: str | None = None,
    machine: str | None = None,
) -> dict[str, str]:
    selected_system, selected_machine = _normalized_platform(system, machine)
    asset = _RUNTIME_ASSETS.get((selected_system, selected_machine))
    if asset is None:
        raise TranscriptionSetupError(
            f"no pinned whisper.cpp {WHISPER_CPP_VERSION} CLI archive is published for "
            f"{selected_system}/{selected_machine}; install that exact version locally and pass "
            "--runtime with its SHA-256 and provenance"
        )

    runtime_root = root / "transcription" / "runtime"
    runtime_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".setup-runtime-", dir=runtime_root) as temporary:
        staging = Path(temporary)
        archive = staging / f"runtime.{asset.archive}"
        _download_verified(
            asset.url,
            archive,
            expected_bytes=asset.bytes,
            expected_sha256=asset.sha256,
            timeout_seconds=timeout_seconds,
        )
        extracted = staging / "extracted"
        extracted.mkdir()
        if asset.archive == "tar.gz":
            _extract_tar_verified(archive, extracted)
        else:
            _extract_zip_verified(archive, extracted)
        executables = [path for path in extracted.rglob(asset.executable_name) if path.is_file()]
        if len(executables) != 1:
            raise TranscriptionSetupError(
                f"verified runtime archive contained {len(executables)} {asset.executable_name} files"
            )
        runtime = _inspect_runtime(
            executables[0],
            expected_sha256=None,
            source=f"{asset.url} ({WHISPER_CPP_RELEASE_COMMIT})",
            license_name=WHISPER_CPP_LICENSE,
            archive_sha256=asset.sha256,
            trusted_archive=True,
            timeout_seconds=process_timeout_seconds,
            system=selected_system,
            machine=selected_machine,
        )
        payload_root = executables[0].parent
        install = runtime_root / (
            f"whisper.cpp-{WHISPER_CPP_VERSION}-{selected_system}-{selected_machine}-"
            f"{runtime['executable_sha256'][:12]}-{uuid.uuid4().hex[:8]}"
        )
        os.replace(payload_root, install)
        runtime["executable"] = str(install / asset.executable_name)
        runtime["quantizer"] = str(install / asset.quantizer_name)
        return runtime


def _inspect_model(path: Path) -> dict[str, str | int]:
    try:
        resolved = path.expanduser().resolve(strict=True)
    except OSError as exc:
        raise TranscriptionSetupError(f"whisper.cpp model does not exist: {path}") from exc
    if not resolved.is_file():
        raise TranscriptionSetupError(f"whisper.cpp model is not a regular file: {resolved}")
    size = resolved.stat().st_size
    digest = _sha256_file(resolved)
    if size != MODEL_BYTES or digest != MODEL_SHA256:
        raise TranscriptionSetupError(
            f"model is not the pinned {MODEL_NAME}: expected {MODEL_BYTES} bytes and SHA-256 "
            f"{MODEL_SHA256}, received {size} bytes and {digest}"
        )
    return {"path": str(resolved), "bytes": size, "sha256": digest}


def _acquire_model(
    root: Path,
    runtime: dict[str, str],
    *,
    timeout_seconds: float,
    process_timeout_seconds: float,
) -> dict[str, str | int]:
    model_root = root / "transcription" / "models"
    model_root.mkdir(parents=True, exist_ok=True)
    target = model_root / MODEL_NAME
    if target.is_file():
        try:
            return _inspect_model(target)
        except TranscriptionSetupError:
            pass

    with tempfile.TemporaryDirectory(prefix=".setup-model-", dir=model_root) as temporary:
        staging = Path(temporary)
        source = staging / MODEL_SOURCE_NAME
        output = staging / MODEL_NAME
        _download_verified(
            MODEL_SOURCE_URL,
            source,
            expected_bytes=MODEL_SOURCE_BYTES,
            expected_sha256=MODEL_SOURCE_SHA256,
            timeout_seconds=timeout_seconds,
        )
        quantizer = Path(runtime["quantizer"])
        if _sha256_file(quantizer) != runtime["quantizer_sha256"]:
            raise TranscriptionSetupError("whisper.cpp quantizer changed after runtime verification")
        try:
            completed = subprocess.run(
                [str(quantizer), str(source), str(output), "q5_0"],
                stdin=subprocess.DEVNULL,
                capture_output=True,
                text=True,
                timeout=process_timeout_seconds,
                check=False,
            )
        except (OSError, subprocess.SubprocessError) as exc:
            raise TranscriptionSetupError(f"Q5_0 model quantization failed: {exc}") from exc
        if completed.returncode != 0:
            detail = (completed.stderr or completed.stdout).strip()[-300:]
            raise TranscriptionSetupError(
                f"Q5_0 model quantization exited with status {completed.returncode}: {detail}"
            )
        inspected = _inspect_model(output)
        with output.open("rb") as handle:
            os.fsync(handle.fileno())
        os.chmod(output, 0o644)
        os.replace(output, target)
    return _inspect_model(target)


def setup_transcription(
    root: Path,
    current: TranscriptionProfile | None,
    *,
    runtime_path: str | None,
    runtime_sha256: str | None,
    runtime_source: str | None,
    runtime_license: str | None,
    model_path: str | None,
    download_runtime: bool,
    download_model: bool,
    threads: int | None,
    download_timeout_seconds: float = DEFAULT_DOWNLOAD_TIMEOUT_SECONDS,
    process_timeout_seconds: float = DEFAULT_PROCESS_TIMEOUT_SECONDS,
) -> tuple[TranscriptionProfile, dict[str, object]]:
    if download_runtime and runtime_path:
        raise TranscriptionSetupError("--download-runtime cannot be combined with --runtime")
    if download_model and model_path:
        raise TranscriptionSetupError("--download-model cannot be combined with --model")
    selected_threads = threads if threads is not None else (
        current.threads if current is not None else WHISPER_CPP_DEFAULT_THREADS
    )
    if selected_threads <= 0:
        raise TranscriptionSetupError("transcription threads must be positive")
    if not 1.0 <= download_timeout_seconds <= 3600.0:
        raise TranscriptionSetupError("download timeout must be between 1 and 3600 seconds")
    if not 1.0 <= process_timeout_seconds <= 3600.0:
        raise TranscriptionSetupError("process timeout must be between 1 and 3600 seconds")

    reused_runtime = False
    if download_runtime:
        runtime = _acquire_runtime(
            root,
            timeout_seconds=download_timeout_seconds,
            process_timeout_seconds=process_timeout_seconds,
        )
    else:
        selected_runtime = runtime_path
        selected_digest = runtime_sha256
        selected_source = runtime_source
        selected_license = runtime_license
        selected_archive_sha256 = ""
        if selected_runtime is None and current is not None and current.configured:
            selected_runtime = current.executable
            selected_digest = current.executable_sha256
            selected_source = current.runtime_source
            selected_license = current.runtime_license
            selected_archive_sha256 = current.runtime_archive_sha256
            reused_runtime = True
        if selected_runtime is None:
            selected_runtime = shutil.which("whisper-cli")
        if selected_runtime is None:
            raise TranscriptionSetupError(
                "whisper.cpp runtime was not found; pass --runtime or opt in with --download-runtime"
            )
        runtime = _inspect_runtime(
            Path(selected_runtime),
            expected_sha256=selected_digest,
            source=selected_source,
            license_name=selected_license,
            archive_sha256=selected_archive_sha256,
            timeout_seconds=process_timeout_seconds,
        )

    reused_model = False
    try:
        if download_model:
            model = _acquire_model(
                root,
                runtime,
                timeout_seconds=download_timeout_seconds,
                process_timeout_seconds=process_timeout_seconds,
            )
        else:
            selected_model = model_path
            if selected_model is None and current is not None and current.configured:
                selected_model = current.model_path
                reused_model = True
            if selected_model is None:
                raise TranscriptionSetupError(
                    f"the pinned {MODEL_NAME} was not configured; pass --model or opt in with --download-model"
                )
            model = _inspect_model(Path(selected_model))
    except BaseException:
        if download_runtime:
            install = Path(runtime["executable"]).parent
            managed_root = (root / "transcription" / "runtime").resolve()
            if managed_root in install.resolve().parents:
                shutil.rmtree(install, ignore_errors=True)
        raise

    profile = TranscriptionProfile(
        executable=runtime["executable"],
        executable_sha256=runtime["executable_sha256"],
        runtime_version=runtime["version"],
        runtime_source=runtime["source"],
        runtime_license=runtime["license"],
        runtime_archive_sha256=runtime["archive_sha256"],
        model_path=str(model["path"]),
        model_sha256=str(model["sha256"]),
        model_source=MODEL_PROVENANCE,
        model_license=MODEL_LICENSE,
        threads=selected_threads,
    )
    unchanged = current == profile and reused_runtime and reused_model
    return profile, {
        "state": "already_ready" if unchanged else "configured",
        "ready": True,
        "backend": "whisper-cpp",
        "cpu_only": True,
        "runtime": {
            "path": profile.executable,
            "version": profile.runtime_version,
            "sha256": profile.executable_sha256,
            "source": profile.runtime_source,
            "license": profile.runtime_license,
            "archive_sha256": profile.runtime_archive_sha256 or None,
        },
        "model": {
            "path": profile.model_path,
            "name": MODEL_NAME,
            "bytes": model["bytes"],
            "sha256": profile.model_sha256,
            "source": profile.model_source,
            "license": profile.model_license,
        },
        "threads": profile.threads,
        "network_used": bool(download_runtime or download_model),
    }
