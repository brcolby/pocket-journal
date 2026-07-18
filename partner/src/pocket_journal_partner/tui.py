from __future__ import annotations

from contextlib import ExitStack, contextmanager
import math
import os
import shutil
import signal
import sqlite3
from pathlib import Path
import subprocess
import sys
import textwrap
import threading
import time
from typing import Any, Callable, Iterator, Protocol, TextIO
import unicodedata
import wave

from .library import LIBRARY_AVAILABILITY_FILTERS, LibraryNote, NoteLibrary


try:  # The Windows standard library does not always ship curses.
    import curses as _curses
except ImportError:  # pragma: no cover - exercised through the injected-module test
    _curses = None


MIN_HEIGHT = 8
MIN_WIDTH = 44
POLL_INTERVAL_MS = 200
PLAYER_TERMINATE_GRACE_SECONDS = 0.05
PLAYER_KILL_WAIT_SECONDS = 1.0
_FILTER_LABELS = {
    "all": "all",
    "audio": "has audio",
    "text": "has text",
    "missing_text": "needs text",
    "missing_audio": "missing audio",
}


@contextmanager
def _blocked_signal_table(signums: tuple[int, ...]) -> Iterator[None]:
    """Make a signal-handler table update atomic without affecting children."""
    update_mask = getattr(signal, "pthread_sigmask", None)
    block = getattr(signal, "SIG_BLOCK", None)
    set_mask = getattr(signal, "SIG_SETMASK", None)
    if not callable(update_mask) or block is None or set_mask is None:
        yield
        return
    try:
        previous_mask = update_mask(block, signums)
    except (OSError, TypeError, ValueError):
        yield
        return
    try:
        yield
    finally:
        update_mask(set_mask, previous_mask)


class AudioPlayer(Protocol):
    def play(self, note_id: str, path: Path, *, duration_ms: int | None = None) -> None: ...

    def pause_resume(self) -> None: ...

    def stop(self) -> None: ...

    def stop_note(self, note_id: str) -> None: ...

    def status_text(self) -> str: ...

    def close(self) -> None: ...


class NativeAudioPlayer:
    """Own a terminal-launched audio process for the lifetime of the TUI."""

    _AUTO_COMMAND = object()

    def __init__(
        self,
        library_root: Path,
        *,
        command: tuple[str, ...] | None | object = _AUTO_COMMAND,
        clock: Callable[[], float] = time.monotonic,
        popen: Callable[..., subprocess.Popen[bytes]] = subprocess.Popen,
        signal_process: Callable[[int, int], None] = os.kill,
    ) -> None:
        self._root = library_root.resolve()
        detected, unavailable = (
            self._detect_command() if command is self._AUTO_COMMAND else (command, None)
        )
        if detected is not None:
            if not isinstance(detected, tuple) or not detected or not all(
                isinstance(part, str) and part for part in detected
            ):
                raise ValueError("audio player command must be a non-empty fixed argument tuple")
            if not Path(detected[0]).is_absolute():
                raise ValueError("audio player executable must use an absolute path")
        self._command = detected
        self._unavailable = unavailable or "Audio playback unavailable on this platform"
        self._clock = clock
        self._popen = popen
        self._signal_process = signal_process
        self._process: subprocess.Popen[bytes] | None = None
        self._note_id: str | None = None
        self._started_at = 0.0
        self._paused_at: float | None = None
        self._paused_total = 0.0
        self._duration: float | None = None
        self._last_message = ""

    @staticmethod
    def _detect_command() -> tuple[tuple[str, ...] | None, str | None]:
        if sys.platform == "darwin":
            executable = Path("/usr/bin/afplay")
            if executable.is_file() and os.access(executable, os.X_OK):
                return (str(executable),), None
            return None, "Audio playback unavailable: /usr/bin/afplay was not found"
        if sys.platform.startswith("linux"):
            candidates = (
                ("paplay", ()),
                ("aplay", ("-q", "--")),
                ("ffplay", ("-nodisp", "-autoexit", "-loglevel", "error", "-nostats")),
            )
            for name, arguments in candidates:
                found = shutil.which(name)
                if found:
                    return (str(Path(found).resolve()), *arguments), None
            return None, "Audio playback unavailable: install paplay, aplay, or ffplay"
        if sys.platform == "win32":
            return (
                None,
                "Audio playback unavailable: no pause-capable native Windows player was found",
            )
        return None, f"Audio playback unavailable on {sys.platform}"

    @staticmethod
    def _wave_duration(path: Path) -> float | None:
        try:
            with wave.open(str(path), "rb") as stream:
                rate = stream.getframerate()
                return stream.getnframes() / rate if rate > 0 else None
        except (EOFError, OSError, wave.Error):
            return None

    def _validated_path(self, path: Path) -> Path:
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(self._root)
        except (OSError, ValueError) as error:
            raise ValueError("note audio must be a readable file inside the library") from error
        if not resolved.is_file():
            raise ValueError("note audio must be a readable file inside the library")
        return resolved

    @contextmanager
    def _defer_launch_signals(self) -> Iterator[None]:
        """Delay signal exceptions until a newly spawned child is owned."""
        if threading.current_thread() is not threading.main_thread():
            yield
            return
        watched = tuple(
            signum
            for name in ("SIGINT", "SIGTERM", "SIGHUP")
            if (signum := getattr(signal, name, None)) is not None
        )
        previous_handlers: dict[int, Any] = {}
        pending: list[tuple[int, Any]] = []
        restorations = ExitStack()

        def defer(signum: int, frame: Any) -> None:
            pending.append((signum, frame))

        def restore(signum: int, previous: Any) -> None:
            try:
                signal.signal(signum, previous)
            except (OSError, ValueError):
                pass

        try:
            # Unblock before Popen so the child never inherits this temporary
            # mask. The second atomic section restores the complete table.
            with _blocked_signal_table(watched):
                for signum in watched:
                    try:
                        previous = signal.getsignal(signum)
                    except (OSError, ValueError):
                        continue
                    if previous is signal.SIG_IGN:
                        continue
                    # Register restoration before changing the disposition.
                    previous_handlers[signum] = previous
                    restorations.callback(restore, signum, previous)
                    try:
                        signal.signal(signum, defer)
                    except (OSError, ValueError):
                        continue
            yield
        finally:
            with _blocked_signal_table(watched):
                if pending:
                    self.close()
                # ExitStack continues running older callbacks even if one
                # restoration is interrupted on a platform without sigmask.
                restorations.close()
                pending_signals = getattr(signal, "sigpending", None)
                if callable(pending_signals):
                    try:
                        if set(pending_signals()).intersection(watched):
                            self.close()
                    except (OSError, ValueError):
                        pass
            if pending:
                # Every watched disposition and the calling thread's original
                # mask are restored before the deferred signal is delivered.
                self.close()
                signum, frame = pending[0]
                previous = previous_handlers[signum]
                if callable(previous):
                    previous(signum, frame)
                else:
                    signal.raise_signal(signum)

    def _elapsed(self, now: float | None = None) -> float:
        if self._process is None:
            return 0.0
        current = self._clock() if now is None else now
        effective = self._paused_at if self._paused_at is not None else current
        elapsed = max(0.0, effective - self._started_at - self._paused_total)
        return min(elapsed, self._duration) if self._duration is not None else elapsed

    def _clear_process(self) -> None:
        self._process = None
        self._note_id = None
        self._paused_at = None
        self._paused_total = 0.0

    def _reap_stopped_process(self, *, terminate: bool) -> float:
        process = self._process
        if process is None:
            return 0.0
        elapsed = self._elapsed()
        if terminate and process.poll() is None:
            if self._paused_at is not None:
                try:
                    self._signal_process(process.pid, signal.SIGCONT)
                except OSError:
                    pass
            try:
                process.terminate()
            except OSError:
                pass
        try:
            # A parent may intentionally ignore SIGTERM; that disposition is
            # inherited across exec on POSIX. Keep graceful player shutdown
            # bounded so every interactive stop path remains responsive.
            process.wait(timeout=PLAYER_TERMINATE_GRACE_SECONDS)
        except subprocess.TimeoutExpired:
            try:
                process.kill()
            except OSError:
                pass
            process.wait(timeout=PLAYER_KILL_WAIT_SECONDS)
        self._clear_process()
        return elapsed

    def _refresh(self) -> None:
        process = self._process
        if process is None:
            return
        result = process.poll()
        if result is None:
            return
        elapsed = self._elapsed()
        duration = self._duration
        self._reap_stopped_process(terminate=False)
        if result == 0:
            finished = duration if duration is not None else elapsed
            self._last_message = f"Playback finished at {_format_duration(finished)}"
        else:
            self._last_message = f"Playback failed (player exited {result})"

    def play(self, note_id: str, path: Path, *, duration_ms: int | None = None) -> None:
        try:
            resolved = self._validated_path(path)
        except ValueError as error:
            self._last_message = f"Could not play audio: {error}"
            return
        if self._command is None:
            self._last_message = self._unavailable
            return
        if self._process is not None:
            self._reap_stopped_process(terminate=True)
        measured = self._wave_duration(resolved)
        if measured is None and duration_ms is not None and duration_ms >= 0:
            measured = duration_ms / 1000.0
        arguments = [*self._command, str(resolved)]
        try:
            with self._defer_launch_signals():
                process = self._popen(
                    arguments,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    close_fds=True,
                    shell=False,
                    start_new_session=True,
                )
                self._process = process
                self._note_id = note_id
                self._started_at = self._clock()
                self._paused_at = None
                self._paused_total = 0.0
                self._duration = measured
                self._last_message = ""
        except OSError as error:
            self._last_message = f"Could not start audio player: {error}"
            return

    def pause_resume(self) -> None:
        self._refresh()
        process = self._process
        if process is None:
            self._last_message = self._unavailable if self._command is None else "Nothing is playing"
            return
        now = self._clock()
        try:
            if self._paused_at is None:
                self._signal_process(process.pid, signal.SIGSTOP)
                self._paused_at = now
            else:
                self._signal_process(process.pid, signal.SIGCONT)
                self._paused_total += max(0.0, now - self._paused_at)
                self._paused_at = None
        except (OSError, ProcessLookupError) as error:
            self._last_message = f"Could not change playback state: {error}"
            self._refresh()

    def stop(self) -> None:
        self._refresh()
        if self._process is None:
            self._last_message = self._unavailable if self._command is None else "Nothing is playing"
            return
        elapsed = self._reap_stopped_process(terminate=True)
        self._last_message = f"Playback stopped at {_format_duration(elapsed)}"

    def stop_note(self, note_id: str) -> None:
        self._refresh()
        if self._process is not None and self._note_id == note_id:
            self.stop()

    def status_text(self) -> str:
        self._refresh()
        if self._process is None:
            return self._last_message or (self._unavailable if self._command is None else "")
        state = "Paused" if self._paused_at is not None else "Playing"
        elapsed = _format_duration(self._elapsed())
        total = _format_duration(self._duration) if self._duration is not None else "--:--"
        return f"{state} {elapsed} / {total}"

    def close(self) -> None:
        process = self._process
        if process is None:
            return
        try:
            self._reap_stopped_process(terminate=True)
            return
        except (OSError, subprocess.TimeoutExpired):
            pass
        process = self._process
        if process is None:
            return
        if self._paused_at is not None:
            try:
                self._signal_process(process.pid, signal.SIGCONT)
            except OSError:
                pass
        try:
            process.kill()
        except OSError:
            pass
        try:
            process.wait(timeout=1.0)
        except (OSError, subprocess.TimeoutExpired):
            pass
        self._clear_process()


def _format_duration(seconds: float | None) -> str:
    total = max(0, int(math.floor(seconds or 0.0)))
    hours, remainder = divmod(total, 3600)
    minutes, whole_seconds = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{whole_seconds:02d}"
    return f"{minutes:02d}:{whole_seconds:02d}"


class _TuiSignal(BaseException):
    def __init__(self, signum: int) -> None:
        super().__init__(signum)
        self.signum = signum


@contextmanager
def _unwind_on_termination_signal() -> Iterator[None]:
    if threading.current_thread() is not threading.main_thread():
        yield
        return
    installed: list[tuple[int, Any]] = []

    def unwind(signum: int, _frame: Any) -> None:
        raise _TuiSignal(signum)

    for name in ("SIGTERM", "SIGHUP"):
        signum = getattr(signal, name, None)
        if signum is None:
            continue
        try:
            previous = signal.getsignal(signum)
            if previous is signal.SIG_IGN:
                continue
            signal.signal(signum, unwind)
        except (OSError, ValueError):
            continue
        installed.append((signum, previous))
    try:
        yield
    finally:
        for signum, previous in reversed(installed):
            try:
                signal.signal(signum, previous)
            except (OSError, ValueError):
                pass


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


def _set_input_timeout(screen: Any, milliseconds: int) -> bool:
    timeout = getattr(screen, "timeout", None)
    if not callable(timeout):
        return False
    timeout(milliseconds)
    return True


def _read_key(screen: Any, module: Any) -> object | None:
    try:
        return screen.get_wch()
    except Exception as error:
        curses_error = getattr(module, "error", ())
        if curses_error and isinstance(error, curses_error):
            return None
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
    timed_input = _set_input_timeout(screen, -1)
    try:
        module.curs_set(1)
    except Exception:
        pass
    try:
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
                return "".join(value)
            if key in ("\x1b",):
                return None
            if key in ("\b", "\x7f", getattr(module, "KEY_BACKSPACE", -1)):
                if value:
                    value.pop()
                continue
            if isinstance(key, str) and len(key) == 1 and key.isprintable() and len(value) < maximum:
                value.append(key)
    finally:
        try:
            module.curs_set(0)
        except Exception:
            pass
        if timed_input:
            _set_input_timeout(screen, POLL_INTERVAL_MS)


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


def _status_line(status: str, player: AudioPlayer) -> str:
    playback = player.status_text()
    if status and playback:
        return f"{status}  •  {playback}"
    return status or playback


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
        "↑↓ move  Enter read  / search  F filter  P play/restart  Space pause  S stop  E title  D delete  Q quit",
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
    player: AudioPlayer,
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
            shown_status = _status_line(status, player)
            if shown_status:
                _add(screen, height - 2, 0, shown_status, width - 1, _attr(module, "A_BOLD"))
            _add(
                screen,
                height - 1,
                0,
                "↑↓ scroll  P play/restart  Space pause  S stop  E title  D delete  B back",
                width - 1,
                _attr(module, "A_DIM"),
            )
            screen.refresh()
        key = _read_key(screen, module)
        if key is None:
            continue
        status = ""
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
                player.play(note.note_id, audio_path, duration_ms=note.duration_ms)
            continue
        if key == " ":
            player.pause_resume()
            continue
        if key in ("s", "S"):
            player.stop()
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
                    player.stop_note(note.note_id)
                    return _delete_note(library, note)
                except (KeyError, OSError, sqlite3.Error) as error:
                    status = f"Could not delete note: {error}"
            elif confirmation is not None:
                status = "Deletion canceled; confirmation did not match"


def _run_curses(
    screen: Any,
    module: Any,
    library: NoteLibrary,
    player: AudioPlayer,
) -> int:
    try:
        screen.keypad(True)
    except Exception:
        pass
    try:
        module.curs_set(0)
    except Exception:
        pass
    try:
        _set_input_timeout(screen, POLL_INTERVAL_MS)
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
                status=_status_line(status, player),
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
        key = _read_key(screen, module)
        if key is None:
            continue
        status = ""
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
            status = _show_note(screen, module, library, note.note_id, player)
            continue
        if key in ("p", "P"):
            audio_path = library.resolve_audio_path(note)
            if audio_path is None:
                status = "No local audio is available for this note"
            else:
                player.play(note.note_id, audio_path, duration_ms=note.duration_ms)
            continue
        if key == " ":
            player.pause_resume()
            continue
        if key in ("s", "S"):
            player.stop()
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
                    player.stop_note(note.note_id)
                    status = _delete_note(library, note)
                    selected = min(selected, max(0, total - 2))
                except (KeyError, OSError, sqlite3.Error) as error:
                    status = f"Could not delete note: {error}"
            elif confirmation is not None:
                status = "Deletion canceled; confirmation did not match"


def run_tui(
    library: NoteLibrary,
    *,
    audio_player: AudioPlayer | None = None,
    screen: Any | None = None,
    curses_module: Any | None = None,
    error_stream: TextIO | None = None,
) -> int:
    """Run the dependency-free curses browser over the local note library."""
    module = curses_module if curses_module is not None else _curses
    errors = error_stream or sys.stderr
    player = audio_player or NativeAudioPlayer(library.root)
    try:
        if module is None:
            errors.write("The curses terminal UI is unavailable on this Python installation.\n")
            return 2
        try:
            with _unwind_on_termination_signal():
                try:
                    if screen is not None:
                        return _run_curses(screen, module, library, player)
                    return int(module.wrapper(lambda active: _run_curses(active, module, library, player)))
                except KeyboardInterrupt:
                    return 130
                except Exception as error:
                    curses_error = getattr(module, "error", ())
                    if curses_error and isinstance(error, curses_error):
                        errors.write(f"Could not start the curses terminal UI: {error}\n")
                        return 2
                    raise
        except _TuiSignal as termination:
            return 128 + termination.signum
    finally:
        player.close()
