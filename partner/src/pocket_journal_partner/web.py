from __future__ import annotations

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import html
import ipaddress
import json
from pathlib import Path
import secrets
import socket
import sqlite3
from typing import Any
from urllib.parse import parse_qs, quote, urlsplit

from .library import (
    LIBRARY_AVAILABILITY_FILTERS,
    MAX_SEARCH_LENGTH,
    LibraryDeleteResult,
    LibraryNote,
    NoteLibrary,
)


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8766
MAX_FORM_BYTES = 4096
_NOTE_PATH_PREFIX = "/note/"
_AUDIO_PATH_PREFIX = "/audio/"


_CSS = """
:root { color-scheme: light dark; font-family: Inter, ui-sans-serif, system-ui, sans-serif;
        --paper: #f4f1e8; --panel: #fffdf7; --ink: #20201e; --muted: #67645d;
        --line: #cbc5b8; --accent: #155f5a; --danger: #a2382d; }
@media (prefers-color-scheme: dark) { :root { --paper: #171816; --panel: #21221f;
        --ink: #f1eee4; --muted: #aaa69c; --line: #45463f; --accent: #73c7be;
        --danger: #f28f83; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--paper); color: var(--ink); line-height: 1.45; }
header, main { width: min(100% - 32px, 920px); margin: auto; }
header { padding: 34px 0 22px; border-bottom: 1px solid var(--line); }
.eyebrow { margin: 0 0 4px; color: var(--accent); font-size: 12px; font-weight: 800;
           letter-spacing: .16em; text-transform: uppercase; }
h1 { margin: 0; font: 700 clamp(28px, 5vw, 42px)/1.05 ui-serif, Georgia, serif; }
h2 { font: 700 clamp(26px, 5vw, 38px)/1.15 ui-serif, Georgia, serif; }
main { padding: 28px 0 64px; }
.toolbar { display: grid; grid-template-columns: minmax(12rem, 1fr) minmax(9rem, auto) auto;
           gap: 10px; margin: 0 0 14px; }
input, select, button { min-height: 44px; padding: 9px 12px; border: 1px solid var(--line);
                        border-radius: 7px; background: var(--panel); color: var(--ink); font: inherit; }
input:focus, select:focus, button:focus, a:focus { outline: 3px solid var(--accent); outline-offset: 2px; }
button { border-color: var(--accent); background: var(--accent); color: var(--paper); font-weight: 750;
         cursor: pointer; }
.summary { display: flex; justify-content: space-between; gap: 16px; margin: 0 0 8px;
           color: var(--muted); font-size: 14px; }
.notes { overflow: hidden; border: 1px solid var(--line); border-radius: 10px; background: var(--panel); }
.note { display: grid; grid-template-columns: minmax(12rem, 1fr) auto; gap: 18px;
        align-items: center; padding: 17px 18px; border-bottom: 1px solid var(--line); }
.note:last-child { border-bottom: 0; }
.note-title { color: var(--ink); font: 700 19px/1.25 ui-serif, Georgia, serif; text-decoration-thickness: 1px; }
.note-sub { margin-top: 5px; color: var(--muted); font-size: 13px; font-variant-numeric: tabular-nums; }
.badges { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 6px; }
.badge { padding: 4px 8px; border: 1px solid var(--line); border-radius: 999px; color: var(--muted);
         font-size: 11px; font-weight: 800; letter-spacing: .06em; text-transform: uppercase; }
.badge.yes { border-color: var(--accent); color: var(--accent); }
.empty, .notice { padding: 30px; border: 1px solid var(--line); border-radius: 10px; background: var(--panel); }
.meta { color: var(--muted); font-variant-numeric: tabular-nums; }
.transcript { padding: 22px; border: 1px solid var(--line); border-radius: 10px; background: var(--panel);
              white-space: pre-wrap; font: 18px/1.6 ui-serif, Georgia, serif; }
audio { width: 100%; margin: 8px 0 18px; }
.edit-form { display: grid; grid-template-columns: 1fr auto; gap: 8px; margin: 24px 0; }
.back { display: inline-block; margin-bottom: 10px; color: var(--accent); font-weight: 700; }
.danger-link { color: var(--danger); font-weight: 750; }
.danger { border-color: var(--danger); background: var(--danger); }
.danger-box { margin-top: 32px; padding-top: 18px; border-top: 1px solid var(--line); }
code { font-family: ui-monospace, SFMono-Regular, monospace; }
@media (max-width: 620px) { header, main { width: min(100% - 24px, 920px); }
  header { padding-top: 24px; } main { padding-top: 20px; }
  .toolbar { grid-template-columns: 1fr; } .note { grid-template-columns: 1fr; gap: 10px; }
  .badges { justify-content: flex-start; } .edit-form { grid-template-columns: 1fr; } }
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
        "<body><header><p class='eyebrow'>Pocket Journal</p>"
        "<h1>Local note library</h1></header>"
        f"<main>{body}</main></body></html>"
    ).encode("utf-8")


def _note_url(note: LibraryNote) -> str:
    return _NOTE_PATH_PREFIX + note.note_id


def _availability_options(selected: str) -> str:
    labels = {
        "all": "All notes",
        "audio": "Has audio",
        "text": "Has text",
        "missing_text": "Needs text",
        "missing_audio": "Missing audio",
    }
    return "".join(
        f"<option value='{value}'{' selected' if value == selected else ''}>"
        f"{html.escape(labels[value])}</option>"
        for value in LIBRARY_AVAILABILITY_FILTERS
    )


def _note_subtitle(note: LibraryNote) -> str:
    parts = [note.filename]
    if note.duration_ms is not None:
        seconds = max(0, note.duration_ms // 1000)
        parts.append(f"{seconds // 60}:{seconds % 60:02d}")
    if note.created_at:
        parts.append(note.created_at[:10])
    return " · ".join(parts)


def _index_html(
    notes: list[LibraryNote],
    *,
    total: int,
    query: str = "",
    availability: str = "all",
) -> bytes:
    search = (
        "<form class='toolbar' method='get' action='/'>"
        f"<input type='search' name='q' maxlength='{MAX_SEARCH_LENGTH}' "
        f"value='{html.escape(query)}' placeholder='Search titles and text' aria-label='Search notes'>"
        f"<select name='availability' aria-label='Filter availability'>{_availability_options(availability)}</select>"
        "<button type='submit'>Search</button></form>"
    )
    active = bool(query or availability != "all")
    clear = "<a href='/'>Clear filters</a>" if active else ""
    summary = (
        "<div class='summary'>"
        f"<span>{total} {'note' if total == 1 else 'notes'}</span>{clear}</div>"
    )
    if not notes:
        if active:
            content = (
                "<div class='empty'><strong>No matching notes.</strong> "
                "Try a broader search or clear the availability filter.</div>"
            )
        else:
            content = (
                "<div class='empty'><strong>No local notes yet.</strong> "
                "Run <code>pj sync</code> after recording a note.</div>"
            )
    else:
        rows = []
        for note in notes:
            audio_class = "yes" if note.audio_path else ""
            text_class = "yes" if note.transcript_text else ""
            audio_label = "Audio" if note.audio_path else "No audio"
            text_label = "Text" if note.transcript_text else "No text"
            rows.append(
                "<article class='note'><div>"
                f"<a class='note-title' href='{_note_url(note)}'>{html.escape(note.title)}</a>"
                f"<div class='note-sub'>{html.escape(_note_subtitle(note))}</div></div>"
                "<div class='badges' aria-label='Note availability'>"
                f"<span class='badge {audio_class}'>{audio_label}</span>"
                f"<span class='badge {text_class}'>{text_label}</span></div></article>"
            )
        content = "<section class='notes'>" + "".join(rows) + "</section>"
    return _document("Pocket Journal", search + summary + content)


def _note_html(note: LibraryNote, csrf_token: str, *, audio_available: bool) -> bytes:
    audio = (
        f"<audio controls preload='metadata' src='{_AUDIO_PATH_PREFIX}{note.note_id}'></audio>"
        if audio_available
        else "<p class='meta'>No local audio.</p>"
    )
    transcript = html.escape(note.transcript_text or "No transcript yet.")
    action = _note_url(note) + "/title"
    body = (
        "<a class='back' href='/'>← All notes</a>"
        f"<h2>{html.escape(note.title)}</h2>{audio}"
        f"<form class='edit-form' method='post' action='{action}'>"
        f"<input type='hidden' name='csrf' value='{html.escape(csrf_token)}'>"
        f"<input type='text' name='title' maxlength='200' required value='{html.escape(note.title)}' "
        "aria-label='Note title'><button type='submit'>SAVE TITLE</button></form>"
        f"<div class='transcript'>{transcript}</div>"
        "<div class='danger-box'><a class='danger-link' "
        f"href='{_note_url(note)}/delete'>Delete local note…</a></div>"
    )
    return _document(note.title, body)


def _delete_confirmation_html(note: LibraryNote, csrf_token: str) -> bytes:
    action = _note_url(note) + "/delete"
    body = (
        "<a class='back' href='" + _note_url(note) + "'>← Keep note</a>"
        "<h2>Delete local note?</h2>"
        f"<div class='notice'><strong>{html.escape(note.title)}</strong><p>"
        "This removes the local library entry, transcript and sync caches, and its unshared "
        "managed audio file. It does not delete the recording from the Pocket Journal device. "
        "A future <code>pj sync</code> can restore the device recording locally.</p></div>"
        f"<form class='edit-form' method='post' action='{action}'>"
        f"<input type='hidden' name='csrf' value='{html.escape(csrf_token)}'>"
        "<input type='text' name='confirm' required autocomplete='off' "
        "placeholder='Type DELETE' aria-label='Type DELETE to confirm'>"
        "<button class='danger' type='submit'>Delete local note</button></form>"
    )
    return _document("Delete local note", body)


def _delete_result_html(result: LibraryDeleteResult) -> bytes:
    details = " ".join(html.escape(error) for error in result.cleanup_errors)
    body = (
        "<h2>Library entry deleted</h2>"
        f"<div class='notice'><strong>{html.escape(result.title)}</strong> was removed from the library. "
        f"The database change is complete, but audio cleanup needs attention: {details}</div>"
        "<p><a class='back' href='/'>← Return to all notes</a></p>"
    )
    return _document("Note deleted with warning", body)


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

        def _filters(self) -> tuple[str, str]:
            query = parse_qs(
                urlsplit(self.path).query,
                keep_blank_values=True,
                max_num_fields=8,
            )
            search = query.get("q", [""])[0]
            availability = query.get("availability", ["all"])[0] or "all"
            if availability not in LIBRARY_AVAILABILITY_FILTERS:
                raise ValueError("Unknown availability filter.")
            return search, availability

        def do_HEAD(self) -> None:
            self.do_GET()

        def do_GET(self) -> None:
            if not self._trusted_host():
                self._error(HTTPStatus.MISDIRECTED_REQUEST, "Untrusted host header.")
                return
            path = urlsplit(self.path).path
            if path == "/":
                try:
                    search, availability = self._filters()
                    notes = library.list_notes(
                        limit=1000,
                        search=search,
                        availability=availability,
                    )
                    total = library.count(
                        search=search,
                        availability=availability,
                    )
                except ValueError as error:
                    self._error(HTTPStatus.BAD_REQUEST, str(error))
                    return
                self._send(
                    HTTPStatus.OK,
                    _index_html(
                        notes,
                        total=total,
                        query=search,
                        availability=availability,
                    ),
                    "text/html; charset=utf-8",
                )
                return
            if path == "/api/notes":
                try:
                    search, availability = self._filters()
                    notes = library.list_notes(
                        limit=1000,
                        search=search,
                        availability=availability,
                    )
                    total = library.count(
                        search=search,
                        availability=availability,
                    )
                except ValueError as error:
                    self._error(HTTPStatus.BAD_REQUEST, str(error))
                    return
                payload = json.dumps(
                    {
                        "notes": [note.as_dict() for note in notes],
                        "total": total,
                        "query": search,
                        "availability": availability,
                    },
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
            delete_note = self._note_from_path(
                _NOTE_PATH_PREFIX, suffix="/delete"
            )
            if delete_note is not None:
                self._send(
                    HTTPStatus.OK,
                    _delete_confirmation_html(delete_note, csrf_token),
                    "text/html; charset=utf-8",
                )
                return
            note = self._note_from_path(_NOTE_PATH_PREFIX)
            if note is not None:
                self._send(
                    HTTPStatus.OK,
                    _note_html(
                        note,
                        csrf_token,
                        audio_available=library.resolve_audio_path(note) is not None,
                    ),
                    "text/html; charset=utf-8",
                )
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
            path = urlsplit(self.path).path
            title_action = path.endswith("/title")
            delete_action = path.endswith("/delete")
            suffix = "/title" if title_action else "/delete" if delete_action else ""
            note = self._note_from_path(_NOTE_PATH_PREFIX, suffix=suffix) if suffix else None
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
            if title_action:
                try:
                    library.update_title(note.note_id, form.get("title", [""])[0])
                except ValueError as error:
                    self._error(HTTPStatus.BAD_REQUEST, str(error))
                    return
                except (OSError, sqlite3.Error):
                    self._error(
                        HTTPStatus.INTERNAL_SERVER_ERROR,
                        "The local note title could not be updated.",
                    )
                    return
                location = quote(_note_url(note), safe="/")
            else:
                if form.get("confirm", [""])[0] != "DELETE":
                    self._error(
                        HTTPStatus.BAD_REQUEST,
                        "Type DELETE exactly to confirm local deletion.",
                    )
                    return
                try:
                    result = library.delete_note(note.note_id)
                except KeyError:
                    self._error(HTTPStatus.NOT_FOUND, "Note not found.")
                    return
                except (OSError, TypeError, ValueError, sqlite3.Error):
                    self._error(
                        HTTPStatus.INTERNAL_SERVER_ERROR,
                        "The local note could not be deleted.",
                    )
                    return
                if result.cleanup_errors:
                    self._send(
                        HTTPStatus.OK,
                        _delete_result_html(result),
                        "text/html; charset=utf-8",
                    )
                    return
                location = "/"
            self.send_response(HTTPStatus.SEE_OTHER)
            self._security_headers()
            self.send_header("Location", location)
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
