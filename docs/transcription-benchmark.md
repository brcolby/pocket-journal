# Transcription Benchmark

`pj transcription benchmark` runs a manifest-defined corpus through the same
CPU-only `whisper.cpp` command policy used by partner sync. It launches no shell,
makes no network request, and never downloads a model or runtime. Model
acquisition is a separate operator action.

## Reproducible Command

```sh
pj transcription benchmark \
  --manifest /path/to/manifest.json \
  --model /models/ggml-base.en-q5_0.bin \
  --whisper-executable /usr/local/bin/whisper-cli \
  --threads 4 \
  --runtime-root /path/to/whisper.cpp/install \
  --runtime-source <pinned-source-url> \
  --runtime-license MIT \
  --model-source <pinned-model-url> \
  --model-license MIT \
  --output /path/to/report.json
```

The command exits nonzero when any case misses its expected outcome, quality
threshold, or CPU-only evidence. A successful transcription requires both an
explicit `--no-gpu` command argument and runtime output reporting `use gpu = 0`.
Timeouts terminate, then kill if necessary, and reap the runtime child. Reports
are written with fsync plus atomic replacement.

## Corpus Manifest

```json
{
  "schema_version": 1,
  "cases": [
    {
      "id": "clean-short",
      "audio": "clean-short.wav",
      "reference": "A manually prepared reference transcript.",
      "max_word_error_rate": 0.15,
      "runs": 2
    },
    {
      "id": "silence",
      "audio": "silence.wav",
      "reference": "",
      "max_unexpected_words": 0
    },
    {
      "id": "truncated",
      "audio": "truncated.wav",
      "expect": "input_error"
    },
    {
      "id": "forced-timeout",
      "audio": "clean-short.wav",
      "timeout_seconds": 0.05,
      "expect": "timeout"
    }
  ]
}
```

Audio paths are resolved relative to the manifest. Inputs must match the device
format: RIFF/WAVE PCM, mono, 16 kHz, 16-bit, with internally consistent sizes.
Unknown manifest fields are rejected. Speech cases may set
`max_word_error_rate`; empty references may set `max_unexpected_words`.

Each report pins the manifest, audio, model, and runtime SHA-256 values; decoding
parameters; operator-supplied provenance and license assertions; platform and
memory identity; model/install sizes; wall time; runtime time; model-load time;
real-time factor; sampled peak RSS; transcript and segments; WER; CPU evidence;
and child cleanup. The first row is labelled `uncontrolled_first_process`, not
claimed as a cold-cache run. Later rows are labelled `warm_process`.

## Measured macOS Evidence

Measured 2026-07-15 on an Apple M4 with 10 logical CPUs and 16 GiB RAM, macOS
Darwin 25.3.0, Python 3.14.6, four threads, and `whisper.cpp` 1.9.1. The runtime
install measured 8,781,687 bytes and executable SHA-256 was
`e25f8bab12a19e4df44d8889f35944a89816d7dfb01c0f8e5de4727fb0939924`.

| Model | Bytes | Peak RSS | Short RTF | Long RTF | WER | Silence | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `base.en` F16 | 147,964,211 | 496,058,368 | 0.112-0.122 | 0.071-0.073 | 7.1% | Hallucinated `You` | Failed |
| `base.en` Q5_0 | 55,308,851 | 407,355,392 | 0.102-0.103 | 0.072-0.073 | 7.1% | `[BLANK_AUDIO]`, normalized | Passed |

The F16 model SHA-256 was
`a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002`.
The Q5_0 SHA-256 was
`ab8c5e525f2f38ca53ab6d7410cd805880e3598a9e9e6814bf81ad5469ec95fe`.
Both speech cases passed a 15% WER threshold. Both malformed-input checks passed,
and forced-timeout children were reaped. Q5_0's explicit blank-audio sentinel is
normalized to a `NO SPEECH` result in partner sync; arbitrary empty runtime output
is not mistaken for that sentinel.

Q5_0 is the production baseline: it is smaller, used less peak memory, retained
the measured quality, and handled this silence corpus without spoken-content
hallucination. F16 remains a useful comparator, not the default.

## Linux And Windows Procedure

1. Install the same `whisper.cpp` version from a pinned package or build and
   record the install root, source, license, executable hash, and version.
2. Use the exact Q5_0 model hash and an offline copy of the same manifest/audio.
3. Disconnect networking after installation, then run the command above twice.
4. Confirm `passed: true`, `cpu_only_evidence_met: true` for successful rows,
   non-null peak RSS, and `process_reaped: true` for every launched process.
5. Archive the JSON report and verify the model, runtime, manifest, and audio
   hashes match before comparing latency or memory.

RSS sampling uses `ps` on macOS, `/proc/<pid>/status` on Linux, and
`GetProcessMemoryInfo` on Windows. Native Linux and Windows execution remains
required evidence; this procedure does not claim those platforms were tested.

## Remaining Product Evidence

The synthetic corpus proves deterministic clean speech, digital silence,
malformed input, and interruption handling. Final product validation still needs
real device recordings with leading/trailing silence, enclosure noise, low-level
speech, approximately 30-second/5-minute/30-minute durations, and a complete
download-transcribe-upload-resume round trip. Human references should be prepared
before transcription and retained with the resulting reports.
