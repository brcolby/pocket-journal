from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
from io import BytesIO, StringIO
import json
import os
from pathlib import Path
import subprocess
import tarfile
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from pocket_journal_partner import cli
from pocket_journal_partner.config import (
    DeviceProfile,
    PartnerConfig,
    TranscriptionProfile,
    load_config,
)
from pocket_journal_partner.device import DeviceError
from pocket_journal_partner import transcription_setup as setup


def _sha256(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def _write_runtime(root: Path) -> tuple[Path, Path]:
    executable = root / "whisper-cli"
    executable.write_text("#!/bin/sh\necho 'whisper.cpp version: 1.9.1'\n", encoding="utf-8")
    executable.chmod(0o755)
    quantizer = root / "whisper-quantize"
    quantizer.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    quantizer.chmod(0o755)
    return executable, quantizer


def _profile(executable: Path, model: Path) -> TranscriptionProfile:
    return TranscriptionProfile(
        executable=str(executable.resolve()),
        executable_sha256=setup._sha256_file(executable),
        runtime_version=setup.WHISPER_CPP_VERSION,
        runtime_source="fixture-runtime-source",
        runtime_license="MIT",
        model_path=str(model.resolve()),
        model_sha256=setup._sha256_file(model),
        model_source=setup.MODEL_PROVENANCE,
        model_license=setup.MODEL_LICENSE,
        threads=3,
    )


class _Response:
    def __init__(self, content: bytes, *, declared_length: int | None = None) -> None:
        self._content = BytesIO(content)
        self.headers = {}
        if declared_length is not None:
            self.headers["Content-Length"] = str(declared_length)

    def __enter__(self):  # type: ignore[no-untyped-def]
        return self

    def __exit__(self, *args):  # type: ignore[no-untyped-def]
        return False

    def geturl(self) -> str:
        return "https://artifacts.example/verified.bin"

    def read(self, size: int = -1) -> bytes:
        return self._content.read(size)


class TranscriptionSetupTests(unittest.TestCase):
    def test_cli_enrolls_pinned_local_artifacts_and_status_reuses_verified_config(self) -> None:
        model_content = b"pinned-q5-model"
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable, _ = _write_runtime(root)
            model = root / setup.MODEL_NAME
            model.write_bytes(model_content)
            stdout = StringIO()
            with (
                patch.object(setup, "MODEL_BYTES", len(model_content)),
                patch.object(setup, "MODEL_SHA256", _sha256(model_content)),
                redirect_stdout(stdout),
            ):
                result = cli.main([
                    "transcription", "setup",
                    "--runtime", str(executable),
                    "--runtime-sha256", setup._sha256_file(executable),
                    "--runtime-source", "fixture-runtime-source",
                    "--runtime-license", "MIT",
                    "--model", str(model),
                    "--threads", "3",
                    "--data-dir", tmp,
                ])

            self.assertEqual(result, 0)
            payload = json.loads(stdout.getvalue())
            self.assertEqual(payload["state"], "configured")
            self.assertTrue(payload["ready"])
            profile = load_config(root).transcription
            self.assertIsNotNone(profile)
            assert profile is not None
            self.assertEqual(profile.executable_sha256, setup._sha256_file(executable))
            self.assertEqual(profile.model_sha256, _sha256(model_content))
            self.assertEqual(profile.threads, 3)

            stdout = StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(cli.main([
                    "transcription", "status", "--digest", "--data-dir", tmp,
                ]), 0)
            status = json.loads(stdout.getvalue())
            self.assertTrue(status["available"])
            self.assertTrue(status["integrity_verified"])
            self.assertEqual(status["selection"]["model"], "persisted_verified_setup")
            self.assertEqual(status["selection"]["runtime"], "persisted_verified_setup")

    def test_setup_failure_keeps_existing_config_byte_for_byte(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable, _ = _write_runtime(root)
            model = root / setup.MODEL_NAME
            model.write_bytes(b"model")
            config = PartnerConfig(devices={"pj-test": DeviceProfile("pj-test", token="secret")})
            config.save(root / "config.json")
            before = (root / "config.json").read_bytes()
            stderr = StringIO()
            with redirect_stderr(stderr):
                result = cli.main([
                    "transcription", "setup",
                    "--runtime", str(executable),
                    "--runtime-sha256", "0" * 64,
                    "--runtime-source", "fixture",
                    "--runtime-license", "MIT",
                    "--model", str(model),
                    "--data-dir", tmp,
                ])

            self.assertEqual(result, 1)
            self.assertIn("runtime SHA-256 mismatch", stderr.getvalue())
            self.assertEqual((root / "config.json").read_bytes(), before)

    def test_setup_rejects_unpinned_local_runtime_without_provenance(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable, _ = _write_runtime(root)
            model = root / setup.MODEL_NAME
            model.write_bytes(b"model")
            with self.assertRaisesRegex(setup.TranscriptionSetupError, "not a repository-pinned"):
                setup.setup_transcription(
                    root,
                    None,
                    runtime_path=str(executable),
                    runtime_sha256=None,
                    runtime_source=None,
                    runtime_license=None,
                    model_path=str(model),
                    download_runtime=False,
                    download_model=False,
                    threads=None,
                )

            with patch.object(setup.subprocess, "run") as run:
                with self.assertRaisesRegex(setup.TranscriptionSetupError, "provenance is incomplete"):
                    setup._inspect_runtime(
                        executable,
                        expected_sha256=setup._sha256_file(executable),
                        source=None,
                        license_name=None,
                        timeout_seconds=10,
                        system="test-os",
                        machine="test-machine",
                    )
            run.assert_not_called()

    def test_already_ready_setup_is_idempotent_and_does_not_download(self) -> None:
        model_content = b"pinned-model"
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable, _ = _write_runtime(root)
            model = root / setup.MODEL_NAME
            model.write_bytes(model_content)
            current = _profile(executable, model)
            with (
                patch.object(setup, "MODEL_BYTES", len(model_content)),
                patch.object(setup, "MODEL_SHA256", _sha256(model_content)),
                patch.object(setup, "urlopen") as opener,
            ):
                profile, result = setup.setup_transcription(
                    root,
                    current,
                    runtime_path=None,
                    runtime_sha256=None,
                    runtime_source=None,
                    runtime_license=None,
                    model_path=None,
                    download_runtime=False,
                    download_model=False,
                    threads=None,
                )

            self.assertEqual(profile, current)
            self.assertEqual(result["state"], "already_ready")
            self.assertFalse(result["network_used"])
            opener.assert_not_called()

    def test_persisted_artifact_tampering_blocks_companion_backend(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable, _ = _write_runtime(root)
            model = root / setup.MODEL_NAME
            model.write_bytes(b"verified")
            PartnerConfig(
                devices={"pj-test": DeviceProfile("pj-test", token="token")},
                transcription=_profile(executable, model),
            ).save(root / "config.json")
            args = cli.build_parser().parse_args([
                "companion", "serve", "--device", "pj-test", "--data-dir", tmp,
            ])
            backend = cli._backend_from_args(args)
            self.assertTrue(backend.availability()["integrity_verified"])  # type: ignore[attr-defined]

            model.write_bytes(b"tampered")
            with self.assertRaisesRegex(DeviceError, "model SHA-256"):
                cli._backend_from_args(args)

    def test_explicit_companion_paths_still_override_persisted_setup(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            persisted_executable, _ = _write_runtime(root)
            persisted_model = root / "persisted.bin"
            persisted_model.write_bytes(b"persisted")
            explicit_root = root / "explicit"
            explicit_root.mkdir()
            explicit_executable, _ = _write_runtime(explicit_root)
            explicit_model = explicit_root / "model.bin"
            explicit_model.write_bytes(b"explicit")
            PartnerConfig(
                transcription=_profile(persisted_executable, persisted_model),
            ).save(root / "config.json")
            args = cli.build_parser().parse_args([
                "companion", "serve", "--data-dir", tmp,
                "--model", str(explicit_model),
                "--whisper-executable", str(explicit_executable),
            ])
            backend, selection = cli._whisper_backend_from_args(args)

            self.assertEqual(backend.model_path, explicit_model)
            self.assertEqual(Path(backend.executable or "").resolve(), explicit_executable.resolve())
            self.assertIsNone(backend.expected_model_sha256)
            self.assertIsNone(backend.expected_executable_sha256)
            self.assertEqual(selection["model"], "argument")
            self.assertEqual(selection["runtime"], "argument")

    def test_download_rejects_offline_partial_and_wrong_digest(self) -> None:
        cases = (
            (OSError("offline"), 4, _sha256(b"data"), "offline"),
            (_Response(b"da"), 4, _sha256(b"data"), "incomplete"),
            (_Response(b"data", declared_length=4), 4, "0" * 64, "SHA-256 mismatch"),
        )
        with TemporaryDirectory() as tmp:
            for index, (response, size, digest, message) in enumerate(cases):
                with self.subTest(message=message):
                    destination = Path(tmp) / f"artifact-{index}"
                    side_effect = response if isinstance(response, BaseException) else None
                    return_value = None if side_effect else response
                    with patch.object(setup, "urlopen", side_effect=side_effect, return_value=return_value):
                        with self.assertRaisesRegex(setup.TranscriptionSetupError, message):
                            setup._download_verified(
                                "https://artifacts.example/file",
                                destination,
                                expected_bytes=size,
                                expected_sha256=digest,
                                timeout_seconds=10,
                            )

    def test_model_quantization_failure_preserves_existing_target(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            model_root = root / "transcription" / "models"
            model_root.mkdir(parents=True)
            target = model_root / setup.MODEL_NAME
            target.write_bytes(b"old-corrupt-target")
            quantizer = root / "whisper-quantize"
            quantizer.write_bytes(b"verified-quantizer")
            runtime = {
                "quantizer": str(quantizer),
                "quantizer_sha256": setup._sha256_file(quantizer),
            }

            def download(url, destination, **kwargs):  # type: ignore[no-untyped-def]
                _ = url, kwargs
                destination.write_bytes(b"verified-source")

            def run(command, **kwargs):  # type: ignore[no-untyped-def]
                _ = kwargs
                Path(command[2]).write_bytes(b"wrong-output")
                return subprocess.CompletedProcess(command, 0, "", "")

            with (
                patch.object(setup, "_download_verified", side_effect=download),
                patch.object(setup.subprocess, "run", side_effect=run),
                self.assertRaisesRegex(setup.TranscriptionSetupError, "not the pinned"),
            ):
                setup._acquire_model(
                    root,
                    runtime,
                    timeout_seconds=10,
                    process_timeout_seconds=10,
                )

            self.assertEqual(target.read_bytes(), b"old-corrupt-target")

    def test_later_model_failure_removes_newly_acquired_runtime(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            install = root / "transcription" / "runtime" / "new-runtime"
            install.mkdir(parents=True)
            executable = install / "whisper-cli"
            executable.write_bytes(b"runtime")
            quantizer = install / "whisper-quantize"
            quantizer.write_bytes(b"quantizer")
            runtime = {
                "executable": str(executable),
                "executable_sha256": _sha256(b"runtime"),
                "version": setup.WHISPER_CPP_VERSION,
                "source": "fixture",
                "license": "MIT",
                "archive_sha256": "a" * 64,
                "quantizer": str(quantizer),
                "quantizer_sha256": _sha256(b"quantizer"),
            }
            with (
                patch.object(setup, "_acquire_runtime", return_value=runtime),
                self.assertRaisesRegex(setup.TranscriptionSetupError, "does not exist"),
            ):
                setup.setup_transcription(
                    root,
                    None,
                    runtime_path=None,
                    runtime_sha256=None,
                    runtime_source=None,
                    runtime_license=None,
                    model_path=str(root / "missing-model"),
                    download_runtime=True,
                    download_model=False,
                    threads=None,
                )

            self.assertFalse(install.exists())

    def test_runtime_download_reports_unsupported_platform_without_network(self) -> None:
        with TemporaryDirectory() as tmp:
            with patch.object(setup, "urlopen") as opener:
                with self.assertRaisesRegex(setup.TranscriptionSetupError, "no pinned"):
                    setup._acquire_runtime(
                        Path(tmp),
                        timeout_seconds=10,
                        process_timeout_seconds=10,
                        system="darwin",
                        machine="x86_64",
                    )
            opener.assert_not_called()

    def test_verified_runtime_archive_is_safely_installed_as_one_unit(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture_archive = root / "fixture.tar.gz"
            entries = {
                "fixture/whisper-cli": b"#!/bin/sh\necho 'whisper.cpp version: 1.9.1'\n",
                "fixture/whisper-quantize": b"#!/bin/sh\nexit 0\n",
                "fixture/LICENSE": b"MIT\n",
            }
            with tarfile.open(fixture_archive, "w:gz") as bundle:
                for name, content in entries.items():
                    member = tarfile.TarInfo(name)
                    member.mode = 0o755 if name != "fixture/LICENSE" else 0o644
                    member.size = len(content)
                    bundle.addfile(member, BytesIO(content))
            archive_content = fixture_archive.read_bytes()
            asset = setup.RuntimeAsset(
                "fixture-os",
                "fixture-machine",
                "tar.gz",
                "https://artifacts.example/runtime.tar.gz",
                len(archive_content),
                _sha256(archive_content),
                "whisper-cli",
                "whisper-quantize",
            )

            def download(url, destination, **kwargs):  # type: ignore[no-untyped-def]
                _ = url, kwargs
                destination.write_bytes(archive_content)

            assets = dict(setup._RUNTIME_ASSETS)
            assets[(asset.system, asset.machine)] = asset
            with (
                patch.object(setup, "_RUNTIME_ASSETS", assets),
                patch.object(setup, "_download_verified", side_effect=download),
            ):
                runtime = setup._acquire_runtime(
                    root,
                    timeout_seconds=10,
                    process_timeout_seconds=10,
                    system=asset.system,
                    machine=asset.machine,
                )

            executable = Path(runtime["executable"])
            self.assertTrue(executable.is_file())
            self.assertTrue(Path(runtime["quantizer"]).is_file())
            self.assertEqual(runtime["archive_sha256"], asset.sha256)
            self.assertEqual(runtime["version"], setup.WHISPER_CPP_VERSION)
            self.assertEqual(list((root / "transcription" / "runtime").glob(".setup-*")), [])

    def test_runtime_archive_rejects_path_traversal(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / "runtime.tar.gz"
            content = b"payload"
            with tarfile.open(archive, "w:gz") as bundle:
                member = tarfile.TarInfo("../outside")
                member.size = len(content)
                bundle.addfile(member, BytesIO(content))
            destination = root / "extracted"
            destination.mkdir()

            with self.assertRaisesRegex(setup.TranscriptionSetupError, "unsafe path"):
                setup._extract_tar_verified(archive, destination)
            self.assertFalse((root / "outside").exists())

    def test_setup_parser_requires_explicit_download_flags_for_network(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args(["transcription", "setup", "--data-dir", "/tmp/pj"])
        self.assertFalse(args.download_runtime)
        self.assertFalse(args.download_model)
        self.assertIs(args.func, cli.cmd_transcription_setup)


if __name__ == "__main__":
    unittest.main()
