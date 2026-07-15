from pathlib import Path
import re
import unittest


ROOT = Path(__file__).parents[2]
BOARD_SOURCE = ROOT / "firmware/components/pj_board/pj_board.c"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 1
    cursor = match.end()
    while cursor < len(source) and depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    if depth:
        raise AssertionError(f"function {name} is unbalanced")
    return source[match.end() : cursor - 1]


class BoardConcurrencyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BOARD_SOURCE.read_text(encoding="utf-8")

    def test_audio_stop_uses_notifications_not_shared_poll_flags(self) -> None:
        self.assertNotIn("g_record_stop_requested", self.source)
        self.assertNotIn("g_playback_stop_requested", self.source)
        self.assertGreaterEqual(self.source.count("xTaskNotifyGive(task)"), 2)
        self.assertGreaterEqual(
            self.source.count("ulTaskNotifyTake(pdTRUE, 0)"), 3
        )

    def test_lifecycle_handle_changes_are_serialized(self) -> None:
        self.assertIn("g_audio_lifecycle_lock", self.source)
        for name in (
            "record_task_exit",
            "playback_task_exit",
            "audio_process_task",
            "pj_board_record_set_active",
            "pj_board_playback_set_active",
        ):
            body = function_body(self.source, name)
            self.assertIn("audio_lifecycle_take()", body, name)
            self.assertIn("audio_lifecycle_give()", body, name)

    def test_update_bits_are_atomically_taken_before_projection(self) -> None:
        self.assertIn("xEventGroupClearBits", self.source)
        time_body = function_body(self.source, "board_time_take_pending")
        self.assertLess(
            time_body.index("board_update_take(BOARD_UPDATE_TIME)"),
            time_body.index("board_time_snapshot()"),
        )
        for name, bit, projection in (
            (
                "pj_board_consume_settings_update",
                "BOARD_UPDATE_SETTINGS",
                "pj_board_refresh_settings(ui)",
            ),
            (
                "pj_board_consume_audio_update",
                "BOARD_UPDATE_AUDIO",
                "pj_board_status()",
            ),
            (
                "pj_board_consume_notes_update",
                "BOARD_UPDATE_NOTES",
                "pj_board_status()",
            ),
        ):
            body = function_body(self.source, name)
            self.assertLess(body.index(f"board_update_take({bit})"), body.index(projection))

    def test_status_api_encodes_one_locked_snapshot(self) -> None:
        snapshot = function_body(self.source, "board_status_snapshot_base")
        self.assertLess(snapshot.index("board_status_take()"), snapshot.index("status = g_status"))
        self.assertLess(snapshot.index("status = g_status"), snapshot.index("board_status_give()"))
        handler = function_body(self.source, "status_handler")
        self.assertNotRegex(handler, r"g_status\s*\.")


if __name__ == "__main__":
    unittest.main()
