from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from pocket_journal_partner.transcription import FakeTranscriptionBackend


class TranscriptionTests(unittest.TestCase):
    def test_fake_backend(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.wav"
            path.write_bytes(b"RIFF")
            result = FakeTranscriptionBackend().transcribe(path)
            self.assertEqual(result["model"], "fake")
            self.assertEqual(result["audio_file"], "sample.wav")


if __name__ == "__main__":
    unittest.main()

