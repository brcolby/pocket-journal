#!/usr/bin/env python3
"""Serve the simulator and persist browser debug logs under .logs/."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SIMULATOR_ROOT = REPO_ROOT / "simulator"
LOG_DIR = REPO_ROOT / ".logs"
LOG_JSONL = LOG_DIR / "simulator-debug.jsonl"
LATEST_JSON = LOG_DIR / "simulator-debug-latest.json"


class SimulatorRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(SIMULATOR_ROOT), **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self) -> None:
        if self.path != "/__simulator_logs":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(length)
        try:
            payload = json.loads(raw_body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            self.send_error(HTTPStatus.BAD_REQUEST, explain=str(error))
            return

        record = {
            "receivedAt": datetime.now(timezone.utc).isoformat(),
            "client": self.client_address[0],
            "payload": payload,
        }
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        with LOG_JSONL.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, sort_keys=True) + "\n")
        LATEST_JSON.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        response = json.dumps({"ok": True, "log": str(LOG_JSONL.relative_to(REPO_ROOT))}).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the Pocket Journal simulator with local debug logging.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8765, type=int)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), SimulatorRequestHandler)
    print(f"Serving simulator at http://{args.host}:{args.port}/")
    print(f"Writing simulator debug logs to {LOG_JSONL.relative_to(REPO_ROOT)}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nSimulator server stopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
