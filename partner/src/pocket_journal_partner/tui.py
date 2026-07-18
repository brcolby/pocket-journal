from __future__ import annotations

import sqlite3
from pathlib import Path
import sys
import textwrap
from typing import Any, Callable, TextIO
import unicodedata
import webbrowser

from .library import LIBRARY_AVAILABILITY_FILTERS, LibraryNote, NoteLibrary


try:  # The Windows standard library does not always ship curses.
    import curses as _curses
except ImportError:  # pragma: no cover - exercised through the injected-module test
    _curses = None


MIN_HEIGHT = 8
MIN_WIDTH = 44
_FILTER_LABELS = {
    "all": "all",
    "audio": "has audio",
    "text": "has text",
    "missing_text": "needs text",
    "missing_audio": "missing audio",
}


def _open_audio(path: Path) -> bool:
    return webbrowser.open(path.as_uri())


def _terminal_text(value: str) -> str:
    return "".join(
        character
        if character in "\n\t" or unicodedata.category(character) != "Cc"
        else "?"
        for character in value
    )


def _attr(module: Any, name: str) -> int:
    return int(getattr(module, name, 0))


def _add(screen: Any, y: int, x: int, value: str, width: int, attribute: int = 0) -> None:
    if y < 0 or x < 0 or width <= 0:
        return
    try:
        screen.addnstr(y, x, _terminal_text(value), width, attribute)
    except Exception as error:  # curses.error is platform-specific and absent on Windows.
        if _curses is None or not isinstance(error, _curses.error):
            raise


def _prompt(
    screen: Any,
    module: Any,
    label: str,
    *,
    initial: str = "",
    maximum: int = 200,
) -> str | None:
    value = list(_terminal_text(initial)[:maximum])
    try:
        module.curs_set(1)
    except Exception:
        pass
    while True:
        height, width = screen.getmaxyx()
        row = max(0, height - 1)
        try:
            screen.move(row, 0)
            screen.clrtoeol()
        except Exception:
            pass
        shown = label + "".join(value)
        _add(screen, row, 0, shown, max(1, width - 1), _attr(module, "A_BOLD"))
        try:
            screen.move(row, min(max(0, width - 1), len(shown)))
        except Exception:
            pass
        screen.refresh()
        key = screen.get_wch()
        if key in ("\n", "\r", getattr(module, "KEY_ENTER", -1)):
            try:
                module.curs_set(0)
            except Exception:
                pass
            return "".join(value)
        if key in ("\x1b",):
            try:
                module.curs_set(0)
            except Exception:
                pass
            return None
        if key in ("\b", "\x7f", getattr(module, "KEY_BACKSPACE", -1)):
            if value:
                value.pop()
            continue
        if isinstance(key, str) and len(key) == 1 and key.isprintable() and len(value) < maximum:
            value.append(key)


def _availability(note: LibraryNote) -> str:
    audio = "audio" if note.audio_path else "no audio"
    text = "text" if note.transcript_text else "no text"
    return f"[{audio}] [{text}]"


def _delete_note(library: NoteLibrary, note: LibraryNote) -> str:
    result = library.delete_note(note.note_id)
    if result.cleanup_errors:
        return "Deleted index; cleanup warning: " + "; ".join(result.cleanup_errors)
    if result.audio_retained_reason:
        return "Deleted local note; " + result.audio_retained_reason
    return "Deleted local note and managed audio" if result.audio_removed else "Deleted local note"


def _draw_home(
    screen: Any,
    module: Any,
    library: NoteLibrary,
    *,
    selected: int,
    query: str,
    availability: str,
    status: str,
) -> tuple[list[LibraryNote], int, int, int]:
    screen.erase()
    height, width = screen.getmaxyx()
    if height < MIN_HEIGHT or width < MIN_WIDTH:
        _add(screen, 0, 0, "Pocket Journal", max(1, width - 1), _attr(module, "A_BOLD"))
        _add(
            screen,
            2,
            0,
            f"Resize to at least {MIN_WIDTH}×{MIN_HEIGHT}. Q quits.",
            max(1, width - 1),
        )
        screen.refresh()
        return [], 0, 0, 1

    total = library.count(search=query, availability=availability)
    page_size = max(1, height - 6)
    selected = min(max(0, selected), max(0, total - 1))
    offset = (selected // page_size) * page_size
    notes = library.list_notes(
        limit=page_size,
        offset=offset,
        search=query,
        availability=availability,
    )
    page = offset // page_size + 1
    pages = max(1, (total + page_size - 1) // page_size)
    _add(screen, 0, 0, "POCKET JOURNAL", width - 1, _attr(module, "A_BOLD"))
    _add(
        screen,
        1,
        0,
        f"{total} {'note' if total == 1 else 'notes'}  page {page}/{pages}  "
        f"filter: {_FILTER_LABELS[availability]}  search: {query or '—'}",
        width - 1,
        _attr(module, "A_DIM"),
    )
    _add(screen, 2, 0, "─" * max(1, width - 1), width - 1, _attr(module, "A_DIM"))
    if not notes:
        message = (
            "No matching notes. Press / to search or F to change the filter."
            if query or availability != "all"
            else "No local notes. Run 'pj sync' after recording a note."
        )
        _add(screen, 4, 2, message, max(1, width - 4))
    for index, note in enumerate(notes):
        absolute = offset + index
        marker = "› " if absolute == selected else "  "
        line = marker + note.title
        badges = _availability(note)
        available_title = max(8, width - len(badges) - 5)
        if len(line) > available_title:
            line = line[: max(1, available_title - 1)] + "…"
        line = line.ljust(available_title) + " " + badges
        attribute = _attr(module, "A_REVERSE") if absolute == selected else 0
        _add(screen, 3 + index, 0, line, width - 1, attribute)
    if status:
        _add(screen, height - 2, 0, status, width - 1, _attr(module, "A_BOLD"))
    _add(
        screen,
        height - 1,
        0,
        "↑↓ move  Enter read  / search  F filter  P play  E title  D delete  Q quit",
        width - 1,
        _attr(module, "A_DIM"),
    )
    screen.refresh()
    return notes, offset, total, page_size


def _show_note(
    screen: Any,
    module: Any,
    library: NoteLibrary,
    note_id: str,
    open_audio: Callable[[Path], bool],
) -> str:
    scroll = 0
    status = ""
    while True:
        note = library.get(note_id)
        if note is None:
            return status or "Note is no longer in the library"
        screen.erase()
        height, width = screen.getmaxyx()
        if height < MIN_HEIGHT or width < MIN_WIDTH:
            _add(screen, 0, 0, "Resize terminal. B returns.", max(1, width - 1))
            screen.refresh()
        else:
            _add(screen, 0, 0, note.title, width - 1, _attr(module, "A_BOLD"))
            _add(screen, 1, 0, _availability(note), width - 1, _attr(module, "A_DIM"))
            _add(screen, 2, 0, "─" * max(1, width - 1), width - 1, _attr(module, "A_DIM"))
            transcript = _terminal_text(note.transcript_text or "No transcript yet.")
            wrapped: list[str] = []
            for paragraph in transcript.splitlines() or [""]:
                wrapped.extend(textwrap.wrap(paragraph, max(1, width - 2)) or [""])
            visible = max(1, height - 5)
            scroll = min(max(0, scroll), max(0, len(wrapped) - visible))
            for row, line in enumerate(wrapped[scroll : scroll + visible], 3):
                _add(screen, row, 1, line, max(1, width - 2))
            if status:
                _add(screen, height - 2, 0, status, width - 1, _attr(module, "A_BOLD"))
            _add(
                screen,
                height - 1,
                0,
                "↑↓ scroll  P play  E title  D delete  B back",
                width - 1,
                _attr(module, "A_DIM"),
            )
            screen.refresh()
        key = screen.get_wch()
        if key in ("b", "B", "q", "Q", "\x1b", getattr(module, "KEY_LEFT", -1)):
            return status
        if key in (getattr(module, "KEY_UP", -1), "k", "K"):
            scroll = max(0, scroll - 1)
            continue
        if key in (getattr(module, "KEY_DOWN", -1), "j", "J"):
            scroll += 1
            continue
        if key in ("p", "P"):
            audio_path = library.resolve_audio_path(note)
            if audio_path is None:
                status = "No local audio is available for this note"
            else:
                try:
                    status = "Opened audio" if open_audio(audio_path) else "Could not open audio"
                except OSError as error:
                    status = f"Could not open audio: {error}"
            continue
        if key in ("e", "E"):
            value = _prompt(screen, module, "Title: ", initial=note.title)
            if value is not None:
                try:
                    library.update_title(note.note_id, value)
                    status = "Title updated"
                except (KeyError, OSError, ValueError, sqlite3.Error) as error:
                    status = f"Could not update title: {error}"
            continue
        if key in ("d", "D"):
            confirmation = _prompt(screen, module, "Type DELETE to remove local note: ")
            if confirmation == "DELETE":
                try:
                    return _delete_note(library, note)
                except (KeyError, OSError, sqlite3.Error) as error:
                    status = f"Could not delete note: {error}"
            elif confirmation is not None:
                status = "Deletion canceled; confirmation did not match"


def _run_curses(
    screen: Any,
    module: Any,
    library: NoteLibrary,
    open_audio: Callable[[Path], bool],
) -> int:
    try:
        screen.keypad(True)
    except Exception:
        pass
    try:
        module.curs_set(0)
    except Exception:
        pass
    selected = 0
    query = ""
    availability = "all"
    status = ""
    while True:
        try:
            notes, offset, total, page_size = _draw_home(
                screen,
                module,
                library,
                selected=selected,
                query=query,
                availability=availability,
                status=status,
            )
        except (OSError, ValueError, sqlite3.Error) as error:
            screen.erase()
            height, width = screen.getmaxyx()
            _add(screen, 0, 0, "Pocket Journal library error", max(1, width - 1), _attr(module, "A_BOLD"))
            _add(screen, 2, 0, str(error), max(1, width - 1))
            _add(screen, max(0, height - 1), 0, "R retry  Q quit", max(1, width - 1))
            screen.refresh()
            notes, offset, total, page_size = [], 0, 0, 1
        selected = min(max(0, selected), max(0, total - 1))
        status = ""
        key = screen.get_wch()
        if key in ("q", "Q"):
            return 0
        if key in (getattr(module, "KEY_RESIZE", -1), "r", "R"):
            continue
        if key in (getattr(module, "KEY_UP", -1), "k", "K"):
            selected = max(0, selected - 1)
            continue
        if key in (getattr(module, "KEY_DOWN", -1), "j", "J"):
            selected = min(max(0, total - 1), selected + 1)
            continue
        if key == getattr(module, "KEY_PPAGE", -1):
            selected = max(0, selected - page_size)
            continue
        if key == getattr(module, "KEY_NPAGE", -1):
            selected = min(max(0, total - 1), selected + page_size)
            continue
        if key in ("g", getattr(module, "KEY_HOME", -1)):
            selected = 0
            continue
        if key in ("G", getattr(module, "KEY_END", -1)):
            selected = max(0, total - 1)
            continue
        if key == "/":
            value = _prompt(screen, module, "Search: ", initial=query)
            if value is not None:
                query = value
                selected = 0
            continue
        if key in ("f", "F"):
            current = LIBRARY_AVAILABILITY_FILTERS.index(availability)
            availability = LIBRARY_AVAILABILITY_FILTERS[
                (current + 1) % len(LIBRARY_AVAILABILITY_FILTERS)
            ]
            selected = 0
            continue
        if key == "\x1b":
            if query or availability != "all":
                query = ""
                availability = "all"
                selected = 0
            continue
        local = selected - offset
        note = notes[local] if 0 <= local < len(notes) else None
        if note is None:
            status = "No note is selected"
            continue
        if key in ("\n", "\r", getattr(module, "KEY_ENTER", -1), "o", "O"):
            status = _show_note(screen, module, library, note.note_id, open_audio)
            continue
        if key in ("p", "P"):
            audio_path = library.resolve_audio_path(note)
            if audio_path is None:
                status = "No local audio is available for this note"
            else:
                try:
                    status = "Opened audio" if open_audio(audio_path) else "Could not open audio"
                except OSError as error:
                    status = f"Could not open audio: {error}"
            continue
        if key in ("e", "E"):
            value = _prompt(screen, module, "Title: ", initial=note.title)
            if value is not None:
                try:
                    library.update_title(note.note_id, value)
                    status = "Title updated"
                except (KeyError, OSError, ValueError, sqlite3.Error) as error:
                    status = f"Could not update title: {error}"
            continue
        if key in ("d", "D"):
            confirmation = _prompt(screen, module, "Type DELETE to remove local note: ")
            if confirmation == "DELETE":
                try:
                    status = _delete_note(library, note)
                    selected = min(selected, max(0, total - 2))
                except (KeyError, OSError, sqlite3.Error) as error:
                    status = f"Could not delete note: {error}"
            elif confirmation is not None:
                status = "Deletion canceled; confirmation did not match"


def run_tui(
    library: NoteLibrary,
    *,
    open_audio: Callable[[Path], bool] = _open_audio,
    screen: Any | None = None,
    curses_module: Any | None = None,
    error_stream: TextIO | None = None,
) -> int:
    """Run the dependency-free curses browser over the local note library."""
    module = curses_module if curses_module is not None else _curses
    errors = error_stream or sys.stderr
    if module is None:
        errors.write("The curses terminal UI is unavailable on this Python installation.\n")
        return 2
    if screen is not None:
        return _run_curses(screen, module, library, open_audio)
    try:
        return int(module.wrapper(lambda active: _run_curses(active, module, library, open_audio)))
    except KeyboardInterrupt:
        return 130
    except Exception as error:
        curses_error = getattr(module, "error", ())
        if curses_error and isinstance(error, curses_error):
            errors.write(f"Could not start the curses terminal UI: {error}\n")
            return 2
        raise
