from __future__ import annotations

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import html
import ipaddress
import json
from pathlib import Path
import secrets
import socket
from typing import Any
from urllib.parse import parse_qs, quote, urlsplit

from .library import LibraryNote, NoteLibrary


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8766
MAX_FORM_BYTES = 4096
_NOTE_PATH_PREFIX = "/note/"
_AUDIO_PATH_PREFIX = "/audio/"


_CSS = """
:root { color-scheme: light dark; font-family: ui-sans-serif, system-ui, sans-serif; }
body { margin: 0; background: Canvas; color: CanvasText; }
header, main { max-width: 960px; margin: auto; padding: 24px; }
header { border-bottom: 2px solid CanvasText; }
h1 { margin: 0; font-size: 28px; }
.note { display: grid; grid-template-columns: minmax(12rem, 1fr) auto; gap: 16px;
        padding: 16px 0; border-bottom: 2px solid color-mix(in srgb, CanvasText 25%, Canvas); }
.note a { color: LinkText; font-size: 20px; font-weight: 700; }
.meta { font-variant-numeric: tabular-nums; opacity: .75; }
.transcript { white-space: pre-wrap; font: 18px/1.55 ui-serif, Georgia, serif; }
audio { width: 100%; margin: 16px 0; }
form { display: flex; gap: 8px; margin: 20px 0; }
input[type=text] { flex: 1; min-width: 0; padding: 10px; font: inherit; border: 2px solid CanvasText; }
button { padding: 10px 18px; border: 2px solid CanvasText; background: CanvasText; color: Canvas; font: inherit; font-weight: 700; }
@media (max-width: 560px) { .note { grid-template-columns: 1fr; } header, main { padding: 16px; } }
"""


def _is_loopback_host(host: str) -> bool:
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _document(title: str, body: str) -> bytes:
    escaped_title = html.escape(title)
    return (
        "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{escaped_title}</title><style>{_CSS}</style></head>"
        f"<body><header><h1>POCKET JOURNAL</h1></header><main>{body}</main></body></html>"
    ).encode("utf-8")


def _note_url(note: LibraryNote) -> str:
    return _NOTE_PATH_PREFIX + note.note_id


def _index_html(notes: list[LibraryNote]) -> bytes:
    if not notes:
        content = "<p>No local notes. Run <code>pj sync</code> after recording a note.</p>"
    else:
        rows = []
        for note in notes:
            media = "AUDIO" if note.audio_path else "NO AUDIO"
            text = "TEXT" if note.transcript_text else "NO TEXT"
            rows.append(
                "<article class='note'>"
                f"<a href='{_note_url(note)}'>{html.escape(note.title)}</a>"
                f"<span class='meta'>{media} / {text}</span></article>"
            )
        content = "".join(rows)
    return _document("Pocket Journal", content)


def _note_html(note: LibraryNote, csrf_token: str) -> bytes:
    audio = (
        f"<audio controls preload='metadata' src='{_AUDIO_PATH_PREFIX}{note.note_id}'></audio>"
        if note.audio_path
        else "<p class='meta'>No local audio.</p>"
    )
    transcript = html.escape(note.transcript_text or "No transcript yet.")
    action = _note_url(note) + "/title"
    body = (
        "<p><a href='/'>ALL NOTES</a></p>"
        f"<h2>{html.escape(note.title)}</h2>{audio}"
        f"<form method='post' action='{action}'>"
        f"<input type='hidden' name='csrf' value='{html.escape(csrf_token)}'>"
        f"<input type='text' name='title' maxlength='200' required value='{html.escape(note.title)}' "
        "aria-label='Note title'><button type='submit'>SAVE TITLE</button></form>"
        f"<div class='transcript'>{transcript}</div>"
    )
    return _document(note.title, body)


def _parse_range(value: str | None, size: int) -> tuple[int, int, bool] | None:
    if value is None:
        return 0, size - 1, False
    if not value.startswith("bytes=") or "," in value or size <= 0:
        return None
    spec = value[6:]
    start_text, separator, end_text = spec.partition("-")
    if not separator:
        return None
    try:
        if start_text:
            start = int(start_text)
            end = int(end_text) if end_text else size - 1
        else:
            suffix = int(end_text)
            if suffix <= 0:
                return None
            start = max(0, size - suffix)
            end = size - 1
    except ValueError:
        return None
    if start < 0 or start >= size or end < start:
        return None
    return start, min(end, size - 1), True


def create_server(
    library: NoteLibrary,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
) -> ThreadingHTTPServer:
    if not _is_loopback_host(host):
        raise ValueError("the note web UI may only bind to a loopback address")
    if not 0 <= port <= 65535:
        raise ValueError("web UI port must be between 0 and 65535")
    csrf_token = secrets.token_urlsafe(32)

    class Handler(BaseHTTPRequestHandler):
        server_version = "PocketJournal/1"
        sys_version = ""

        def log_message(self, format: str, *args: Any) -> None:
            return

        def _security_headers(self) -> None:
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'unsafe-inline'; media-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("X-Frame-Options", "DENY")

        def _trusted_host(self) -> bool:
            raw_host = self.headers.get("Host", "").lower()
            bound_host = str(self.server.server_address[0]).lower()
            bound_port = self.server.server_address[1]
            allowed = {
                f"127.0.0.1:{bound_port}",
                f"localhost:{bound_port}",
                f"[::1]:{bound_port}",
                f"{bound_host}:{bound_port}",
            }
            if ":" in bound_host:
                allowed.add(f"[{bound_host}]:{bound_port}")
            if bound_port in {80, 443}:
                allowed.update({"127.0.0.1", "localhost", "[::1]"})
            return raw_host in allowed

        def _send(self, status: HTTPStatus, payload: bytes, content_type: str) -> None:
            self.send_response(status)
            self._security_headers()
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(payload)

        def _error(self, status: HTTPStatus, message: str) -> None:
            self._send(status, _document(str(status.value), f"<p>{html.escape(message)}</p>"), "text/html; charset=utf-8")

        def _note_from_path(self, prefix: str, *, suffix: str = "") -> LibraryNote | None:
            path = urlsplit(self.path).path
            if not path.startswith(prefix) or (suffix and not path.endswith(suffix)):
                return None
            end = len(path) - len(suffix) if suffix else len(path)
            note_id = path[len(prefix):end]
            if "/" in note_id:
                return None
            return library.get(note_id)

        def do_HEAD(self) -> None:
            self.do_GET()

        def do_GET(self) -> None:
            if not self._trusted_host():
                self._error(HTTPStatus.MISDIRECTED_REQUEST, "Untrusted host header.")
                return
            path = urlsplit(self.path).path
            if path == "/":
                self._send(HTTPStatus.OK, _index_html(library.list_notes(limit=1000)), "text/html; charset=utf-8")
                return
            if path == "/api/notes":
                payload = json.dumps(
                    {"notes": [note.as_dict() for note in library.list_notes(limit=1000)]},
                    ensure_ascii=False,
                    separators=(",", ":"),
                ).encode("utf-8")
                self._send(HTTPStatus.OK, payload, "application/json; charset=utf-8")
                return
            if path.startswith(_AUDIO_PATH_PREFIX):
                note = self._note_from_path(_AUDIO_PATH_PREFIX)
                if note is None:
                    self._error(HTTPStatus.NOT_FOUND, "Note not found.")
                    return
                audio_path = library.resolve_audio_path(note)
                if audio_path is None:
                    self._error(HTTPStatus.NOT_FOUND, "Audio not found.")
                    return
                self._serve_audio(audio_path)
                return
            note = self._note_from_path(_NOTE_PATH_PREFIX)
            if note is not None:
                self._send(HTTPStatus.OK, _note_html(note, csrf_token), "text/html; charset=utf-8")
                return
            self._error(HTTPStatus.NOT_FOUND, "Page not found.")

        def _serve_audio(self, path: Path) -> None:
            size = path.stat().st_size
            selected = _parse_range(self.headers.get("Range"), size)
            if selected is None:
                self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                self._security_headers()
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            start, end, partial = selected
            length = max(0, end - start + 1)
            self.send_response(HTTPStatus.PARTIAL_CONTENT if partial else HTTPStatus.OK)
            self._security_headers()
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(length))
            if partial:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            if self.command == "HEAD":
                return
            with path.open("rb") as handle:
                handle.seek(start)
                remaining = length
                while remaining:
                    chunk = handle.read(min(64 * 1024, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)

        def do_POST(self) -> None:
            if not self._trusted_host():
                self._error(HTTPStatus.MISDIRECTED_REQUEST, "Untrusted host header.")
                return
            note = self._note_from_path(_NOTE_PATH_PREFIX, suffix="/title")
            if note is None:
                self._error(HTTPStatus.NOT_FOUND, "Page not found.")
                return
            if self.headers.get_content_type() != "application/x-www-form-urlencoded":
                self._error(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, "Expected a form submission.")
                return
            try:
                content_length = int(self.headers.get("Content-Length", ""))
            except ValueError:
                content_length = -1
            if not 0 <= content_length <= MAX_FORM_BYTES:
                self._error(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Form is too large.")
                return
            try:
                form = parse_qs(
                    self.rfile.read(content_length).decode("utf-8"),
                    keep_blank_values=True,
                    strict_parsing=True,
                )
            except (UnicodeError, ValueError):
                self._error(HTTPStatus.BAD_REQUEST, "Invalid form.")
                return
            submitted_csrf = form.get("csrf", [""])[0]
            if not secrets.compare_digest(submitted_csrf, csrf_token):
                self._error(HTTPStatus.FORBIDDEN, "Invalid form token.")
                return
            try:
                library.update_title(note.note_id, form.get("title", [""])[0])
            except ValueError as error:
                self._error(HTTPStatus.BAD_REQUEST, str(error))
                return
            self.send_response(HTTPStatus.SEE_OTHER)
            self._security_headers()
            self.send_header("Location", quote(_note_url(note), safe="/"))
            self.send_header("Content-Length", "0")
            self.end_headers()

    server_type = ThreadingHTTPServer
    if ":" in host:
        class IPv6ThreadingHTTPServer(ThreadingHTTPServer):
            address_family = socket.AF_INET6

        server_type = IPv6ThreadingHTTPServer
    server = server_type((host, port), Handler)
    server.daemon_threads = True
    return server
