from pathlib import Path
import re
import unittest


ROOT = Path(__file__).parents[2]
APP_SOURCE = ROOT / "firmware/main/app_main.c"
BOARD_SOURCE = ROOT / "firmware/components/pj_board/pj_board.c"
UI_SOURCE = ROOT / "firmware/components/pj_ui/pj_ui.c"


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


class RecordAsyncContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = APP_SOURCE.read_text(encoding="utf-8")
        cls.board = BOARD_SOURCE.read_text(encoding="utf-8")
        cls.ui = UI_SOURCE.read_text(encoding="utf-8")

    def test_sleep_preserves_record_stop_gate_until_audio_ack(self) -> None:
        sleep = function_body(self.ui, "pj_ui_sleep")
        self.assertIn("PJ_RECORD_ACTIVE", sleep)
        self.assertGreaterEqual(sleep.count("PJ_RECORD_STOPPING"), 2)
        self.assertLess(
            sleep.index("ctx->record_state ="),
            sleep.index("set_state(ctx, PJ_UI_STATE_STATIC)"),
        )

        audio = function_body(self.ui, "pj_ui_set_audio_state")
        self.assertIn("next_record == PJ_RECORD_IDLE", audio)
        self.assertIn("ctx->recording_seconds = 0", audio)
        self.assertIn("previous_record == PJ_RECORD_STOPPING", audio)
        self.assertIn("interaction_changed(ctx)", audio)

    def test_rearm_waits_for_old_board_worker_to_report_inactive(self) -> None:
        arm = function_body(self.app, "service_record_arming")
        positions = [
            arm.index("pj_board_status().recording"),
            arm.rindex("g_record_arm_display_generation = 0"),
            arm.index("set_recording_active(1)"),
            arm.index("sync_ui_audio_from_board(ui)"),
        ]
        self.assertEqual(positions, sorted(positions))

    def test_cadence_service_rechecks_board_projection_before_submit(self) -> None:
        service = function_body(self.app, "service_seconds_cadence")
        sync = service.index("sync_ui_audio_from_board(ui)")
        desired = service.index("desired_seconds_clock(ui)", sync)
        reconcile = service.index("reconcile_seconds_cadence(ui", desired)
        submit = service.index("compose_and_submit(ui", reconcile)
        self.assertEqual(
            [sync, desired, reconcile, submit],
            sorted([sync, desired, reconcile, submit]),
        )

    def test_board_clears_lifecycle_before_inactive_audio_ack(self) -> None:
        exit_body = function_body(self.board, "record_task_exit")
        positions = [
            exit_body.index("recording_take_completion"),
            exit_body.index("pj_audio_lifecycle_finish_record"),
            exit_body.index("board_audio_state_set(0, -1)"),
            exit_body.index("board_update_publish(BOARD_UPDATE_AUDIO)"),
        ]
        self.assertEqual(positions, sorted(positions))


if __name__ == "__main__":
    unittest.main()
