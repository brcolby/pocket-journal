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


class BoardTimeTransactionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BOARD_SOURCE.read_text(encoding="utf-8")

    def test_sntp_uses_durable_transaction_before_sync_publication(self) -> None:
        publish = function_body(self.source, "publish_sntp_civil_time_locked")
        self.assertIn("board_time_commit_locked", publish)
        self.assertNotIn("board_time_publish(", publish)
        self.assertIn("PJ_TIME_SYNC_PUBLICATION_RTC_ROLLED_BACK", publish)
        self.assertIn("PJ_TIME_SYNC_PUBLICATION_PARTIAL_COMMIT", publish)
        process = function_body(self.source, "connectivity_sntp_process")
        self.assertLess(
            process.index("publish_sntp_civil_time_locked"),
            process.rindex("pj_time_sync_on_success"),
        )

    def test_http_and_usb_share_error_fields(self) -> None:
        required_fields = (
            "retryable",
            "partial_commit",
            "partial_components",
            "rtc_rollback_failed",
            "utc_offset_rollback_failed",
        )
        serial = function_body(self.source, "serial_print_time_update_error")
        http = function_body(self.source, "time_send_update_error")
        self.assertIn("board_time_update_error_fields", serial)
        self.assertIn("board_time_update_error_fields", http)
        for field in required_fields:
            self.assertIn(field, serial)
            self.assertIn(field, http)

    def test_time_projection_cannot_claim_zero_civil_time(self) -> None:
        snapshot = function_body(self.source, "board_time_snapshot")
        self.assertRegex(
            snapshot,
            r"else\s+if\s*\(snapshot\.time_set\)\s*\{\s*"
            r"snapshot\.time_set\s*=\s*0;",
        )

    def test_rtc_wake_plan_and_sleep_share_time_transaction(self) -> None:
        sync = function_body(self.source, "rtc_wake_sync")
        sync_locked = function_body(self.source, "rtc_wake_sync_locked")
        sleep = function_body(self.source, "pj_board_enter_sleep")
        for first, second in (
            ("time_transaction_take()", "rtc_wake_sync_locked()"),
            ("rtc_wake_sync_locked()", "time_transaction_give()"),
        ):
            self.assertLess(sync.index(first), sync.index(second))
        self.assertIn("board_time_model_clock(&clock)", sync_locked)
        light_sleep = sleep.index("esp_light_sleep_start()")
        positions = [
            sleep.index("time_transaction_take()"),
            sleep.index("rtc_wake_sync_locked()"),
            light_sleep,
            sleep.index("time_transaction_give()", light_sleep),
            sleep.index("rtc_wake_disarm_board(&rtc_flags, 1)"),
            sleep.index("rtc_read_status_time()"),
        ]
        self.assertEqual(positions, sorted(positions))

    def test_http_defaults_are_resolved_inside_the_transaction(self) -> None:
        handler = function_body(self.source, "time_put_handler")
        before_commit = handler[: handler.index("board_set_time_date")]
        self.assertNotIn("board_time_snapshot()", before_commit)
        self.assertRegex(before_commit, r"int\s+second\s*=\s*0;")
        self.assertRegex(before_commit, r"int\s+year\s*=\s*0;")

    def test_provisioning_state_and_credentials_have_owners(self) -> None:
        save = function_body(self.source, "wifi_save_provisioning")
        self.assertIn("xSemaphoreTake(g_provisioning_lock", save)
        self.assertIn("xSemaphoreGive(g_provisioning_lock)", save)
        apply_config = function_body(self.source, "wifi_apply_config")
        self.assertLess(
            apply_config.index("portENTER_CRITICAL(&g_credentials_lock)"),
            apply_config.index('snprintf(ssid, sizeof(ssid), "%s", g_wifi_ssid)'),
        )
        access = function_body(self.source, "ble_provision_access")
        self.assertIn("ble_provision_claim()", access)
        self.assertIn("ble_provision_release()", access)
        worker = function_body(self.source, "ble_provision_task")
        self.assertIn("ble_provision_release()", worker)
        self.assertNotIn("g_ble_provision_task", self.source)

    def test_subsystems_only_clear_owned_status_errors(self) -> None:
        completion = function_body(self.source, "recording_publish_completion")
        self.assertIn("board_status_set_error_if_empty", completion)
        wipe = function_body(self.source, "recording_wipe_worker")
        self.assertIn("board_status_clear_error_if_equal", wipe)
        recovery = function_body(self.source, "pj_board_storage_recover")
        self.assertIn("board_status_clear_error_if_owned", recovery)
        wifi = function_body(self.source, "wifi_event_handler")
        got_ip = wifi[wifi.index("IP_EVENT_STA_GOT_IP") :]
        self.assertIn('strncmp(g_status.last_error, "Wi-Fi "', got_ip)


if __name__ == "__main__":
    unittest.main()
