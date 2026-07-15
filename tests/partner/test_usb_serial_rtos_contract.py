from pathlib import Path
import re
import unittest


ROOT = Path(__file__).parents[2]
BOARD_SOURCE = ROOT / "firmware/components/pj_board/pj_board.c"
BOARD_CMAKE = ROOT / "firmware/components/pj_board/CMakeLists.txt"


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


class UsbSerialRtosContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BOARD_SOURCE.read_text(encoding="utf-8")

    def test_serial_task_uses_interrupt_driven_esp_idf_vfs(self) -> None:
        start = function_body(self.source, "serial_command_task_start")
        positions = [
            start.index("usb_serial_jtag_driver_install"),
            start.index("fcntl(fileno(stdin), F_SETFL"),
            start.index("usb_serial_jtag_vfs_use_driver"),
            start.index("xTaskCreate"),
        ]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("usb_serial_jtag_is_driver_installed", start)
        self.assertIn("input_flags & ~O_NONBLOCK", start)
        self.assertIn("usb_serial_jtag_vfs_use_nonblocking", start)
        self.assertIn("usb_serial_jtag_driver_uninstall", start)

    def test_driver_buffers_cover_command_and_audio_response(self) -> None:
        rx = re.search(r"#define PJ_SERIAL_RX_BUFFER_BYTES (\d+)U", self.source)
        tx = re.search(r"#define PJ_SERIAL_TX_BUFFER_BYTES (\d+)U", self.source)
        self.assertIsNotNone(rx)
        self.assertIsNotNone(tx)
        assert rx is not None and tx is not None
        self.assertGreaterEqual(int(rx.group(1)), 768)
        self.assertGreaterEqual(int(tx.group(1)), 2 * 1024 + 512)

    def test_disconnected_console_retains_bounded_backoff(self) -> None:
        task = function_body(self.source, "serial_command_task")
        empty_read = task[task.index("fgets") : task.index("size_t chunk_length")]
        self.assertIn("clearerr(stdin)", empty_read)
        self.assertRegex(empty_read, r"vTaskDelay\(pdMS_TO_TICKS\(100\)\)")

    def test_component_declares_usb_serial_driver_dependency(self) -> None:
        cmake = BOARD_CMAKE.read_text(encoding="utf-8")
        self.assertIn("esp_driver_usb_serial_jtag", cmake)


if __name__ == "__main__":
    unittest.main()
