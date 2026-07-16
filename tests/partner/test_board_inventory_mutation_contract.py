from pathlib import Path
import re
import unittest


ROOT = Path(__file__).parents[2]
BOARD_SOURCE = ROOT / "firmware/components/pj_board/pj_board.c"
SYNC_RUNTIME_SOURCE = (
    ROOT / "firmware/components/pj_board/pj_companion_sync_runtime.c"
)


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


class BoardInventoryMutationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.board = BOARD_SOURCE.read_text(encoding="utf-8")
        cls.sync_runtime = SYNC_RUNTIME_SOURCE.read_text(encoding="utf-8")

    def test_raw_publication_fences_rename_and_preserves_valid_temp(self) -> None:
        publish = function_body(self.board, "recording_publish_file")
        positions = [
            publish.index("recording_file_valid(temporary_path"),
            publish.index(
                "pj_board_companion_sync_inventory_mutation_begin"
            ),
            publish.index("pj_recording_publish_raw"),
            publish.index(
                "pj_board_companion_sync_inventory_mutation_finish"
            ),
        ]
        self.assertEqual(positions, sorted(positions))
        self.assertRegex(
            publish,
            r"pj_board_companion_sync_inventory_mutation_begin\s*\(\s*"
            r"&sync_mutation,\s*0\s*\)",
        )
        after_begin = publish[
            publish.index(
                "pj_board_companion_sync_inventory_mutation_begin"
            ) :
        ]
        self.assertNotIn("remove(temporary_path)", after_begin)
        self.assertNotIn("remove(temporary_path)", publish)

        exit_body = function_body(self.board, "record_task_exit")
        self.assertLess(
            exit_body.index("record_storage_release()"),
            exit_body.index("pj_board_companion_sync_resume()"),
        )
        record = function_body(self.board, "record_task")
        before_open = record[: record.index('fopen(temporary_path, "wb+")')]
        self.assertNotIn("remove(temporary_path)", before_open)

    def test_recording_paths_reserve_recoverable_artifacts(self) -> None:
        available = function_body(self.board, "recording_path_available")
        self.assertIn('"%s.tmp"', available)
        self.assertIn('"%s.bak"', available)
        allocator = function_body(self.board, "next_recording_path")
        self.assertIn("recording_path_available(out)", allocator)
        start = function_body(self.board, "pj_board_record_set_active")
        self.assertLess(
            start.index("record_storage_try_acquire()"),
            start.index("next_recording_path("),
        )
        self.assertLess(
            start.index("next_recording_path("),
            start.index('xTaskCreate(record_task, "pj-record"'),
        )

    def test_wipe_fences_all_deletes_and_releases_before_resume(self) -> None:
        worker = function_body(self.board, "recording_wipe_worker")
        begin = worker.index(
            "pj_board_companion_sync_inventory_mutation_begin"
        )
        success = worker.index("int incomplete = 0;")
        failure = worker[begin:success]
        self.assertIn("PJ_WIPE_CODE_SYNC_STATE_FAILED", failure)
        self.assertRegex(
            failure,
            r"pj_storage_wipe_finish\s*\(\s*&g_storage_coordinator,\s*"
            r"0U,\s*0U,\s*0U,",
        )
        self.assertNotIn("delete_dir_entries", failure)

        success_body = worker[success:]
        positions = [
            success_body.index("delete_dir_entries"),
            success_body.rindex("delete_dir_entries"),
            success_body.index(
                "pj_board_companion_sync_inventory_mutation_finish"
            ),
            success_body.index("pj_storage_wipe_finish"),
            success_body.rindex(
                "portEXIT_CRITICAL(&g_storage_coordinator_lock)"
            ),
            success_body.index("pj_board_companion_sync_resume()"),
        ]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("pj_storage_audio_wipe_artifact", success_body)
        self.assertEqual(
            success_body.count("pj_storage_json_wipe_artifact"), 2
        )
        self.assertIn(
            "recording wipe blocked by unavailable Sync state",
            success_body,
        )
        self.assertIn(
            'board_status_clear_error_if_equal("recording wipe incomplete")',
            success_body,
        )
        self.assertRegex(
            worker,
            r"pj_board_companion_sync_inventory_mutation_begin\s*\(\s*"
            r"&sync_mutation,\s*0\s*\)",
        )

    def test_runtime_recovery_fences_before_unmount(self) -> None:
        recovery = function_body(self.board, "pj_board_storage_recover")
        positions = [
            recovery.index("pj_storage_recovery_try_begin"),
            recovery.index(
                "pj_board_companion_sync_inventory_mutation_begin"
            ),
            recovery.index("esp_vfs_fat_sdcard_unmount"),
            recovery.index("storage_init()"),
            recovery.rindex(
                "pj_board_companion_sync_inventory_mutation_finish"
            ),
            recovery.rindex("pj_storage_recovery_finish"),
            recovery.rindex(
                "portEXIT_CRITICAL(&g_storage_coordinator_lock)"
            ),
            recovery.rindex("pj_board_companion_sync_resume()"),
        ]
        self.assertEqual(positions, sorted(positions))
        before_unmount = recovery[: recovery.index(
            "esp_vfs_fat_sdcard_unmount"
        )]
        self.assertIn(
            "storage recovery blocked by unavailable Sync state",
            before_unmount,
        )
        self.assertIn("pj_storage_recovery_finish", before_unmount)
        self.assertRegex(
            recovery,
            r"pj_board_companion_sync_inventory_mutation_begin\s*\(\s*"
            r"&sync_mutation,\s*0\s*\)",
        )

    def test_storage_recovery_is_backup_first(self) -> None:
        recover = function_body(self.board, "recover_dir_artifacts")
        self.assertIn(
            "for (int recovery_pass = 0; recovery_pass < 2; recovery_pass++)",
            recover,
        )
        self.assertIn("rewinddir(dir)", recover)
        self.assertIn("int backup_action =", recover)
        self.assertIn(
            "PJ_STORAGE_RECOVERY_VALIDATE_BACKUP", recover
        )
        self.assertIn("PJ_STORAGE_RECOVERY_VALIDATE_TEMP", recover)
        self.assertLess(
            recover.index("int backup_action ="),
            recover.index("pj_storage_recover_temporary"),
        )

    def test_boot_restore_persists_successor_before_initialization(self) -> None:
        initialize = function_body(
            self.sync_runtime, "initialize_state_locked"
        )
        restore = initialize.index(
            "pj_companion_sync_restore_active_successor_transactional"
        )
        initialized = initialize.rindex("g_sync_initialized = 1")
        self.assertLess(restore, initialized)
        failure_branch = initialize[
            restore : initialize.index(
                "pj_companion_sync_mutation_barrier_advance", restore
            )
        ]
        self.assertIn("return 0;", failure_branch)


if __name__ == "__main__":
    unittest.main()
