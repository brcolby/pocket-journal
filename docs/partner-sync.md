# Partner Sync

The partner CLI is the source of truth for laptop-side sync state.

## Commands

```sh
pj provision --ssid <ssid> --password <password>
pj discover
pj sync --device <device-id>
pj calendar sync --device <device-id>
pj settings get --device <device-id>
pj settings set --device <device-id> volume=5
```

## Data Stored Locally

By default the partner stores data under `~/.pocket-journal`:

- `config.json`: paired devices and tokens.
- `audio/<device-id>/`: downloaded WAV files.
- `transcripts/<device-id>/`: transcript JSON files.
- `sync-log.jsonl`: append-only sync results.

For sandboxed or test runs, pass `--data-dir <path>` to commands that need the paired-device config.

## Transcription

The target model id is `distil-whisper/distil-large-v3.5`.

The code uses a backend abstraction so tests can run with a fake backend and production can use Hugging Face Transformers or a later Apple Silicon optimized backend.
