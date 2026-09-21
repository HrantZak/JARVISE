# Voice

Speech in, speech out, entirely on this machine. No audio ever leaves the
device, and there is no cloud STT or TTS anywhere in this path.

```
Microphone -> AudioCapture -> VAD -> Whisper -> Qwen3 -> Piper -> AudioPlayer -> Speaker
```

## Layout

| Piece | Where | Role |
|---|---|---|
| `ISttBackend`, `ITtsBackend` | `src/voice` | The interfaces the rest of JARVIS sees |
| `WhisperSttBackend` | `src/voice` | whisper.cpp v1.9.2, shares ggml with llama.cpp |
| `PiperTtsBackend` | `src/voice` | Piper 1.2.0 as a child process |
| `VoiceActivityDetector` | `src/voice` | RMS gate with hysteresis and pre-roll |
| `AudioCapture` | `src/app` | `QAudioSource`, 16 kHz mono |
| `AudioPlayer` | `src/app` | `QAudioSink`, pull mode |
| `VoiceController` | `src/app` | Orchestration; owns every connection |

`src/voice` is Qt-free apart from one documented exception: `PiperTtsBackend`
uses `QProcess`, because Piper ships as an executable and hand-rolling
`CreateProcess` with pipes would add a hundred lines to solve a solved problem.
Audio capture and playback live in `src/app` because they are inherently Qt
Multimedia.

## State machine

```
Idle -> Listening -> Transcribing -> Thinking -> Synthesizing -> Speaking -> Idle
```

plus `Disabled`, `Unavailable` and `Error`. The pipeline never rests in `Error`:
`setError()` takes the state to fall back to and always lands in `Listening` or
`Idle`.

`AiCoreModel` remains the single source of *visual* state. `VoiceController`
maps its finer steps onto the Core's existing states in one function -
`Transcribing` and `Thinking` both show as THINKING, `Synthesizing` and
`Speaking` as SPEAKING. There is no second state machine for the UI.

### What Phase 6 changed

`Thinking` no longer means "waiting for a generation". It means "waiting for the
agent's task", and a task can span several generations - one that asks for a
tool, one that phrases the answer. The turn ends on `AgentLoop::finished`, not
on a generation completing, so the microphone does not reopen while the answer
is still being written.

Two consequences worth knowing before reading the code:

- **The streamed chunks are not connected when an agent is present.** A tool
  round's first generation is a JSON object, and streaming it into synthesis had
  JARVIS read the tool call aloud. The finished answer is spoken instead, which
  costs the incremental synthesis this document describes below - about one
  sentence of added latency.
- **Stop and barge-in call `AgentLoop::stop()`.** Cancelling the generation
  alone would leave the task running, and it would simply start another one.

See [phase6-architecture.md](phase6-architecture.md) for the full chain.

## Threading

Audio blocks arrive on the GUI thread and go straight into the VAD, which is an
RMS over 100 ms - microseconds. Transcription and synthesis run on the shared
`ThreadPool` and return through queued invocations. Generation was already on
the pool. Nothing blocks a frame, and there is no polling timer anywhere in the
pipeline: `QAudioSink` pulls playback data on demand.

Queued completions hold a `shared_ptr<bool>` liveness marker that the destructor
clears, so a callback landing after teardown is a no-op rather than a
use-after-free.

Only one stage owns the pipeline at a time, so a slow transcription cannot be
overtaken by the next utterance.

## VRAM: the decision that matters

**Whisper runs on the CPU by default.** This is measured, not cautious.

An 8 GB RTX 3070 cannot hold Whisper large-v3-turbo (1.6 GB) and Qwen3 8B
Q4_K_M (5.5 GB) alongside the desktop. Both models load, and then the driver
starts evicting. Same prompt, same machine, both models resident:

| | Whisper on GPU | Whisper on CPU |
|---|---|---|
| STT latency | 8768 ms (x0.3 realtime) | 4599 ms (x0.6 realtime) |
| LLM time to first token | 9738 ms | **328 ms** |
| LLM generation | 7.0 tok/s | **42.4 tok/s** |
| Voice round trip | 28.2 s | **11.3 s** |

Moving Whisper off the GPU makes the language model six times faster *and*
Whisper itself twice as fast, because neither is thrashing. Set
`voice.sttUseGpu` to true only with a smaller language model or a bigger card.

For reference, Whisper alone on the GPU with no model loaded runs at x11.2
realtime (273 ms for 3 s). The GPU is not slow; there is simply no room for both.

## Benchmarks

Release build, reference machine, Whisper on CPU, Qwen3 8B on Vulkan:

| Stage | Cold | Warm |
|---|---|---|
| Whisper model load | 1227 ms | - |
| Qwen3 model load | 6071 ms | - |
| VAD speech detection | 0 ms | 0 ms |
| STT (3 s of audio) | 4599 ms | - |
| LLM time to first token | 328 ms | - |
| LLM generation | 42.4 tok/s | - |
| TTS (4.7 s of audio) | 556 ms | - |
| Playback start | 28 ms | - |
| **Round trip** | **11.3 s** | - |

Whisper alone, no model loaded: 333 ms cold, 273 ms warm, x11.2 realtime. The
first Vulkan run costs ~13 s of shader pipeline compilation; that is warm-up,
not inference.

Piper: ~560-650 ms for 2.5-4.7 s of audio, both languages, x4-8 realtime.

## Configuration (v4)

The `voice` section, migrated automatically from v3:

```json
"voice": {
  "enabled": false,
  "autoListen": false,
  "inputDeviceId": "", "outputDeviceId": "",
  "sttModelPath": "models/whisper/ggml-large-v3-turbo.bin",
  "sttUseGpu": false,
  "ttsExecutable": "", "ttsRuVoice": "", "ttsEnVoice": "",
  "ttsLengthScale": 1.0, "ttsVolume": 1.0,
  "vadActivationThreshold": 0.015, "vadReleaseThreshold": 0.008,
  "vadSilenceTimeoutMs": 800, "vadMinimumSpeechMs": 300,
  "vadMaximumUtteranceMs": 30000, "vadPreRollMs": 300
}
```

Empty device ids mean "system default", which is what survives a headset being
unplugged. Validation clamps every range and, in particular, forces the VAD
release threshold below the activation threshold - otherwise speech would end
the instant it started.

## Models

| Component | Model | Size | Licence |
|---|---|---|---|
| STT | `ggml-large-v3-turbo.bin` | 1.5 GB | MIT |
| TTS RU | `ru_RU-dmitri-medium.onnx` | 60 MB | MIT / CC-BY |
| TTS EN | `en_US-ryan-medium.onnx` | 60 MB | MIT / CC-BY |
| LLM | Qwen3 8B Q4_K_M | 4.9 GB | from the Ollama store |

## Voice activity detection

An RMS energy gate with hysteresis, a pre-roll buffer so the first syllable is
not clipped, and a minimum duration so a click is not sent to Whisper.

**It is not a neural VAD.** It separates speech from a quiet room reliably and
will misfire on sustained background noise - a fan, music, a second
conversation. That is the honest minimum until a real VAD model is added.

## Silence never reaches Whisper

Handed a silent buffer, Whisper reliably invents a plausible sentence - observed
here as a full Russian phrase produced from two seconds of digital zeros. The
backend therefore refuses to ask: audio below an RMS floor returns an empty
transcript without running the model. An empty transcript then never reaches the
language model, so silence cannot become a prompt.

## Barge-in

Interruption fires only on a confirmed `SpeechStarted` from the VAD - the
activation threshold has to be crossed, so a keyboard click or a cough does not
cut JARVIS off. On confirmation: stop TTS, cancel generation, clear the queue,
return to `Listening`.

## Security

Piper runs as a child process. The executable path and the voice paths come from
configuration and are validated as existing regular files before use. **No part
of the command line is ever derived from model output** - the only thing the
model contributes is the text to speak, and that travels through stdin. This
gives JARVIS no ability to run anything else. Tool calling remains Phase 7.

Synthesised audio lives in a temporary directory that is deleted with it.
Microphone audio exists only in memory and is never written to disk.

## Testing

| Suite | What it covers |
|---|---|
| `tst_stt` | Real transcription of Piper-synthesised speech, VAD, silence gate |
| `tst_tts` | Real synthesis in both languages, UTF-8 through the process boundary |
| `tst_audio` | Real microphone: enumeration, capture, 16 kHz output, level |
| `tst_audio_player` | Real speaker: playback, level, lifecycle, replacement |
| `tst_voice_controller` | Orchestration, with scripted STT/TTS doubles |
| `tst_voice_integration` | The whole pipeline, every engine real |

Doubles appear in exactly one suite, for orchestration, where driving a state
machine with a 1.5 GB model would test whisper.cpp instead.

## Human verification required

Three things a test process cannot do:

- **Live microphone.** Nobody can speak into it from inside a test. Capture is
  proven to deliver real PCM with a real level; that the level rises *because a
  person spoke* is not verified.
- **Physical speaker output.** The operating system is proven to accept and
  consume the stream to the end. Whether a sound reached the room is outside
  what a process can observe.
- **Live barge-in.** The mechanism is tested by stopping playback mid-utterance;
  triggering it with an actual voice is not.

## Known limitations

- Recognition of *synthetic* speech is worse than of a real voice, and Piper
  does not produce a bit-identical WAV twice, so the transcript varies between
  runs. Observed: "Джарвис" rendered as "Джеррис" and "Джервис" on the GPU run,
  correctly on the CPU run.
- Whisper on the CPU costs ~1.5x realtime. Acceptable for turn-taking, too slow
  for streaming transcription.
- The audio format conversion path in `AudioCapture` (non-16 kHz devices) is
  written but not exercised on this machine: the Realtek input delivers 16 kHz
  mono natively.
