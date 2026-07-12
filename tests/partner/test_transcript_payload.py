from __future__ import annotations

import unittest

from pocket_journal_partner.transcript_payload import (
    DEVICE_TRANSCRIPT_MAX_BYTES,
    build_device_transcript_payload,
    serialize_device_transcript,
)


SOURCE = {"sha256": "a" * 64, "bytes": 1234, "data_bytes": 1190}
IDENTITY = {
    "backend": "huggingface",
    "runtime": "transformers",
    "runtime_version": "4.53.0",
    "model_id": "example/model",
    "model_revision": "abc123",
    "decoding_parameters": {"task": "transcribe", "temperature": 0},
}


class TranscriptPayloadTests(unittest.TestCase):
    def test_preserves_rich_and_unknown_metadata_with_complete_provenance(self) -> None:
        transcript = {
            "text": "  hello world  ",
            "model": "example/model",
            "language": "en",
            "segments": [{"text": "hello", "start": 0.0, "end": 0.4}],
            "timestamps": [[0.0, 0.4]],
            "created_at": "2026-07-11T12:00:00+00:00",
            "vendor_extension": {"confidence": 0.98},
            "sync": {"stale": True},
        }

        payload = build_device_transcript_payload(transcript, SOURCE, IDENTITY)

        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["text"], "hello world")
        self.assertEqual(payload["source"], SOURCE)
        self.assertEqual(payload["transcription"], IDENTITY)
        self.assertEqual(payload["language"], "en")
        self.assertEqual(payload["segments"], transcript["segments"])
        self.assertEqual(payload["timestamps"], transcript["timestamps"])
        self.assertEqual(payload["vendor_extension"], {"confidence": 0.98})
        self.assertNotIn("sync", payload)

    def test_serialization_matches_compact_sorted_utf8_wire_format(self) -> None:
        encoded = serialize_device_transcript({"text": "café", "v": 1})
        self.assertEqual(encoded, b'{"text":"caf\xc3\xa9","v":1}')

    def test_accepts_exact_limit_and_rejects_one_byte_over_without_truncation(self) -> None:
        base = build_device_transcript_payload({"text": "x"}, SOURCE, IDENTITY)
        fixed_size = len(serialize_device_transcript(base)) - 1
        exact_text = "x" * (DEVICE_TRANSCRIPT_MAX_BYTES - fixed_size)
        exact = build_device_transcript_payload({"text": exact_text}, SOURCE, IDENTITY)
        self.assertEqual(len(serialize_device_transcript(exact)), DEVICE_TRANSCRIPT_MAX_BYTES)
        self.assertEqual(exact["text"], exact_text)

        with self.assertRaisesRegex(ValueError, "exceeds the 61440-byte"):
            build_device_transcript_payload({"text": exact_text + "x"}, SOURCE, IDENTITY)

    def test_non_ascii_limit_counts_utf8_bytes(self) -> None:
        base = build_device_transcript_payload({"text": "x"}, SOURCE, IDENTITY)
        fixed_size = len(serialize_device_transcript(base)) - 1
        fit_count = (DEVICE_TRANSCRIPT_MAX_BYTES - fixed_size) // 2
        payload = build_device_transcript_payload({"text": "é" * fit_count}, SOURCE, IDENTITY)
        self.assertLessEqual(len(serialize_device_transcript(payload)), DEVICE_TRANSCRIPT_MAX_BYTES)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            build_device_transcript_payload({"text": "é" * (fit_count + 1)}, SOURCE, IDENTITY)

    def test_rejects_malformed_inputs_and_non_json_metadata(self) -> None:
        invalid_cases = [
            ({}, SOURCE, IDENTITY, "text"),
            ({"text": "   "}, SOURCE, IDENTITY, "text"),
            ({"text": "ok"}, {"bytes": 1}, IDENTITY, "sha256"),
            ({"text": "ok"}, {"sha256": "a", "bytes": True}, IDENTITY, "bytes"),
            ({"text": "ok", "bad": object()}, SOURCE, IDENTITY, "valid JSON"),
            ({"text": "ok", "confidence": float("nan")}, SOURCE, IDENTITY, "valid JSON"),
        ]
        for transcript, source, identity, error in invalid_cases:
            with self.subTest(error=error):
                with self.assertRaisesRegex(ValueError, error):
                    build_device_transcript_payload(transcript, source, identity)

    def test_result_is_detached_from_nested_unknown_metadata(self) -> None:
        transcript = {"text": "ok", "unknown": {"items": [1]}}
        payload = build_device_transcript_payload(transcript, SOURCE, IDENTITY)
        transcript["unknown"]["items"].append(2)
        self.assertEqual(payload["unknown"], {"items": [1]})


if __name__ == "__main__":
    unittest.main()
