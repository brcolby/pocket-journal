from __future__ import annotations

import http.client
from pathlib import Path
import re
from tempfile import TemporaryDirectory
import threading
from urllib.parse import urlencode
import unittest

from pocket_journal_partner.library import NoteLibrary
from pocket_journal_partner.web import create_server


class LibraryWebTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.library = NoteLibrary(self.root)
        self.note = self.library.upsert_discovered("device", "audio", "sample.wav")
        self.library.attach_transcript(self.note.note_id, {"text": "A <private> transcript"})
        self.audio = self.root / "sample.wav"
        self.audio.write_bytes(b"0123456789")
        self.library.attach_audio(self.note.note_id, self.audio)
        self.server = create_server(self.library, "127.0.0.1", 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.connection = http.client.HTTPConnection(
            "127.0.0.1", self.server.server_address[1], timeout=3
        )

    def tearDown(self) -> None:
        self.connection.close()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(3)
        self.temporary.cleanup()

    def request(self, method: str, path: str, body: bytes | None = None, headers=None):  # type: ignore[no-untyped-def]
        self.connection.request(method, path, body=body, headers=headers or {})
        response = self.connection.getresponse()
        payload = response.read()
        return response, payload

    def test_index_and_note_escape_content_and_set_security_headers(self) -> None:
        response, index = self.request("GET", "/")
        self.assertEqual(response.status, 200)
        self.assertEqual(response.getheader("X-Content-Type-Options"), "nosniff")
        self.assertIn("frame-ancestors 'none'", response.getheader("Content-Security-Policy"))

        response, note = self.request("GET", f"/note/{self.note.note_id}")
        self.assertEqual(response.status, 200)
        self.assertIn(b"A &lt;private&gt; transcript", note)
        self.assertNotIn(b"A <private> transcript", note)

    def test_audio_supports_bounded_ranges_and_rejects_invalid_ranges(self) -> None:
        response, payload = self.request(
            "GET", f"/audio/{self.note.note_id}", headers={"Range": "bytes=2-5"}
        )
        self.assertEqual(response.status, 206)
        self.assertEqual(response.getheader("Content-Type"), "audio/wav")
        self.assertEqual(response.getheader("Content-Range"), "bytes 2-5/10")
        self.assertEqual(payload, b"2345")

        response, payload = self.request(
            "GET", f"/audio/{self.note.note_id}", headers={"Range": "bytes=99-100"}
        )
        self.assertEqual(response.status, 416)
        self.assertEqual(response.getheader("Content-Range"), "bytes */10")
        self.assertEqual(payload, b"")

    def test_title_form_requires_csrf_and_updates_database(self) -> None:
        _, note_page = self.request("GET", f"/note/{self.note.note_id}")
        match = re.search(rb"name='csrf' value='([^']+)'", note_page)
        self.assertIsNotNone(match)
        assert match is not None

        invalid = urlencode({"csrf": "wrong", "title": "Wrong"}).encode()
        response, _ = self.request(
            "POST",
            f"/note/{self.note.note_id}/title",
            invalid,
            {
                "Content-Type": "application/x-www-form-urlencoded",
                "Content-Length": str(len(invalid)),
            },
        )
        self.assertEqual(response.status, 403)
        self.assertEqual(self.library.get(self.note.note_id).title, "sample")  # type: ignore[union-attr]

        valid = urlencode({"csrf": match.group(1).decode(), "title": "Web title"}).encode()
        response, _ = self.request(
            "POST",
            f"/note/{self.note.note_id}/title",
            valid,
            {
                "Content-Type": "application/x-www-form-urlencoded",
                "Content-Length": str(len(valid)),
            },
        )
        self.assertEqual(response.status, 303)
        self.assertEqual(response.getheader("Location"), f"/note/{self.note.note_id}")
        self.assertEqual(self.library.get(self.note.note_id).title, "Web title")  # type: ignore[union-attr]

    def test_unknown_and_traversal_shaped_paths_do_not_read_files(self) -> None:
        for path in ("/audio/../../sample.wav", "/audio/%2e%2e%2fsample.wav", "/note/not-an-id"):
            with self.subTest(path=path):
                response, _ = self.request("GET", path)
                self.assertEqual(response.status, 404)

    def test_dns_rebinding_host_header_is_rejected(self) -> None:
        response, payload = self.request("GET", "/", headers={"Host": "attacker.example"})

        self.assertEqual(response.status, 421)
        self.assertIn(b"Untrusted host header", payload)
        self.assertNotIn(b"A &lt;private&gt; transcript", payload)

    def test_non_loopback_bind_is_refused(self) -> None:
        with self.assertRaisesRegex(ValueError, "loopback"):
            create_server(self.library, "0.0.0.0", 0)


if __name__ == "__main__":
    unittest.main()
