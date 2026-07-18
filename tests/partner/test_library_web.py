from __future__ import annotations

import http.client
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory
import threading
from urllib.parse import urlencode
import unittest
from unittest import mock

from pocket_journal_partner.library import NoteLibrary
from pocket_journal_partner.storage import PartnerStore
from pocket_journal_partner.web import create_server


class LibraryWebTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.library = NoteLibrary(self.root)
        self.note = self.library.upsert_discovered("device", "audio", "sample.wav")
        self.library.attach_transcript(self.note.note_id, {"text": "A <private> transcript"})
        self.audio = PartnerStore(self.root).audio_path(
            "device", "audio", "sample.wav"
        )
        self.audio.parent.mkdir(parents=True, exist_ok=True)
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
        self.assertIn(b">Audio</span>", index)
        self.assertIn(b">Text</span>", index)

        response, note = self.request("GET", f"/note/{self.note.note_id}")
        self.assertEqual(response.status, 200)
        self.assertIn(b"A &lt;private&gt; transcript", note)
        self.assertNotIn(b"A <private> transcript", note)

    def test_search_and_availability_filter_browser_and_api_results(self) -> None:
        quiet = self.library.upsert_discovered(
            "device", "quiet", "quiet.wav", label="Quiet <idea>"
        )
        path = "/?" + urlencode({"q": "Quiet", "availability": "missing_text"})
        response, index = self.request("GET", path)

        self.assertEqual(response.status, 200)
        self.assertIn(b"Quiet &lt;idea&gt;", index)
        self.assertNotIn(b">sample</a>", index)
        self.assertIn(b">No audio</span>", index)
        self.assertIn(b">No text</span>", index)

        api_path = "/api/notes?" + urlencode(
            {"q": "Quiet", "availability": "missing_audio"}
        )
        response, payload = self.request("GET", api_path)
        parsed = json.loads(payload)
        self.assertEqual(response.status, 200)
        self.assertEqual(parsed["total"], 1)
        self.assertEqual(parsed["availability"], "missing_audio")
        self.assertEqual(parsed["notes"][0]["note_id"], quiet.note_id)

        response, no_results = self.request("GET", "/?q=does-not-exist")
        self.assertEqual(response.status, 200)
        self.assertIn(b"No matching notes", no_results)

        response, invalid = self.request("GET", "/?availability=anything")
        self.assertEqual(response.status, 400)
        self.assertIn(b"Unknown availability filter", invalid)

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

    def test_title_race_with_local_delete_returns_not_found(self) -> None:
        _, note_page = self.request("GET", f"/note/{self.note.note_id}")
        match = re.search(rb"name='csrf' value='([^']+)'", note_page)
        self.assertIsNotNone(match)
        assert match is not None
        body = urlencode(
            {"csrf": match.group(1).decode(), "title": "Too late"}
        ).encode()

        with mock.patch.object(self.library, "update_title", side_effect=KeyError("gone")):
            response, payload = self.request(
                "POST",
                f"/note/{self.note.note_id}/title",
                body,
                {
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Content-Length": str(len(body)),
                },
            )

        self.assertEqual(response.status, 404)
        self.assertIn(b"Note not found", payload)

    def test_audio_disappearing_before_open_returns_clean_not_found(self) -> None:
        original_resolve = self.library.resolve_audio_path

        def remove_after_resolve(note):  # type: ignore[no-untyped-def]
            path = original_resolve(note)
            assert path is not None
            path.unlink()
            return path

        with mock.patch.object(
            self.library,
            "resolve_audio_path",
            side_effect=remove_after_resolve,
        ):
            response, payload = self.request("GET", f"/audio/{self.note.note_id}")

        self.assertEqual(response.status, 404)
        self.assertIn(b"Audio not found", payload)

    def test_delete_requires_confirmation_and_csrf_then_removes_local_note(self) -> None:
        response, confirmation = self.request(
            "GET", f"/note/{self.note.note_id}/delete"
        )
        self.assertEqual(response.status, 200)
        self.assertIn(b"does not delete the recording from the Pocket Journal device", confirmation)
        match = re.search(rb"name='csrf' value='([^']+)'", confirmation)
        self.assertIsNotNone(match)
        assert match is not None

        for form, expected in (
            ({"csrf": "wrong", "confirm": "DELETE"}, 403),
            ({"csrf": match.group(1).decode(), "confirm": "delete"}, 400),
        ):
            body = urlencode(form).encode()
            response, _ = self.request(
                "POST",
                f"/note/{self.note.note_id}/delete",
                body,
                {
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Content-Length": str(len(body)),
                },
            )
            self.assertEqual(response.status, expected)
            self.assertIsNotNone(self.library.get(self.note.note_id))
            self.assertTrue(self.audio.exists())

        body = urlencode(
            {"csrf": match.group(1).decode(), "confirm": "DELETE"}
        ).encode()
        response, _ = self.request(
            "POST",
            f"/note/{self.note.note_id}/delete",
            body,
            {
                "Content-Type": "application/x-www-form-urlencoded",
                "Content-Length": str(len(body)),
            },
        )
        self.assertEqual(response.status, 303)
        self.assertEqual(response.getheader("Location"), "/")
        self.assertIsNone(self.library.get(self.note.note_id))
        self.assertFalse(self.audio.exists())

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
