from __future__ import annotations

from pathlib import Path
import sys
from typing import Callable, TextIO
import unicodedata
import webbrowser

from .library import LibraryNote, NoteLibrary


PAGE_SIZE = 10


def _open_audio(path: Path) -> bool:
    return webbrowser.open(path.as_uri())


def _terminal_text(value: str) -> str:
    return "".join(
        character
        if character in "\n\t" or unicodedata.category(character) != "Cc"
        else "?"
        for character in value
    )


def _write_page(output: TextIO, notes: list[LibraryNote], offset: int, total: int) -> None:
    page = offset // PAGE_SIZE + 1
    pages = max(1, (total + PAGE_SIZE - 1) // PAGE_SIZE)
    output.write(f"\nPOCKET JOURNAL  NOTES {total}  PAGE {page}/{pages}\n")
    output.write("=" * 64 + "\n")
    if not notes:
        output.write("No local notes. Run 'pj sync' after recording a note.\n")
    for index, note in enumerate(notes, 1):
        media = "A" if note.audio_path else "-"
        text = "T" if note.transcript_text else "-"
        output.write(f"{index:>2}. [{media}{text}] {_terminal_text(note.title)}\n")
    output.write("\nN/P page  O # read  T # title  L # listen  Q quit\n")
    output.flush()


def run_tui(
    library: NoteLibrary,
    *,
    input_stream: TextIO | None = None,
    output_stream: TextIO | None = None,
    open_audio: Callable[[Path], bool] = _open_audio,
) -> int:
    """Run a dependency-free terminal browser over the local note library."""
    source = input_stream or sys.stdin
    output = output_stream or sys.stdout
    offset = 0
    while True:
        total = library.count()
        if total and offset >= total:
            offset = max(0, ((total - 1) // PAGE_SIZE) * PAGE_SIZE)
        notes = library.list_notes(limit=PAGE_SIZE, offset=offset)
        _write_page(output, notes, offset, total)
        output.write("> ")
        output.flush()
        command = source.readline()
        if command == "":
            return 0
        command = command.strip()
        if not command:
            continue
        verb, _, arguments = command.partition(" ")
        verb = verb.lower()
        if verb in {"q", "quit", "exit"}:
            return 0
        if verb in {"n", "next"}:
            if offset + PAGE_SIZE < total:
                offset += PAGE_SIZE
            continue
        if verb in {"p", "prev", "previous"}:
            offset = max(0, offset - PAGE_SIZE)
            continue
        number, _, value = arguments.strip().partition(" ")
        try:
            selected = int(number) - 1
        except ValueError:
            output.write("Expected a note number from the current page.\n")
            continue
        if selected < 0 or selected >= len(notes):
            output.write("Note number is outside the current page.\n")
            continue
        note = notes[selected]
        if verb in {"o", "open", "read"}:
            title = _terminal_text(note.title)
            output.write(f"\n{title}\n{'-' * len(title)}\n")
            output.write(_terminal_text(note.transcript_text or "No transcript yet.") + "\n")
            continue
        if verb in {"t", "title", "rename"}:
            try:
                library.update_title(note.note_id, value)
            except ValueError as error:
                output.write(f"Could not update title: {error}\n")
            continue
        if verb in {"l", "listen", "play"}:
            audio_path = library.resolve_audio_path(note)
            if audio_path is None:
                output.write("No local audio is available for this note.\n")
            elif not open_audio(audio_path):
                output.write(f"Could not open {audio_path}.\n")
            continue
        output.write("Unknown command. Use N, P, O, T, L, or Q.\n")
