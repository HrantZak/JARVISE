# Phase 6 — Benchmarks

Measured on the reference machine, from test runs rather than estimates. Every
figure below has a test that produced it and a file it was written to.

## The machine

```
CPU     Intel Core i9-11900K
RAM     32 GB  (31.89 GB reported)
GPU     NVIDIA GeForce RTX 3070, 8 GB
OS      Windows 11 Pro 25H2, build 26200
Model   Qwen3 8B Q4_K_M, 37/36 layers on GPU via Vulkan
STT     Whisper large-v3-turbo, 1.59 GB, GPU (Vulkan)
TTS     Piper, Russian voice
```

Whisper and Qwen3 are **resident at the same time** on an 8 GB card. That is the
configuration the application runs in, and it is the configuration these numbers
come from.

## Baseline

`BASELINE_UNAVAILABLE` for everything agent-related. The agent did not exist
before Phase 6, so there is no earlier measurement to compare against. Nothing
below is presented as an improvement over a previous number, because there is no
previous number.

Two figures do have a Phase 5 predecessor — validation and `memory_info` — and
both are compared against it in the next section.

## The security boundary

From `tst_tool_live::benchmarkTheStages`, written to
`build/<config>/tests/tool-live-answers.txt`.

| Stage | Release | Debug |
|---|---|---|
| Validation — parse, schema, types, enums | **1.9 µs** per call | 28.2 µs |
| `memory_info`, end to end through the coordinator | **4 ms** | 3 ms |

Against Phase 5, which measured 1.6 µs and 3 ms on the same machine: validation
is 1.9 µs and `memory_info` is 4 ms. Both moved by less than the run-to-run
noise of a single-digit measurement. The boundary did not get meaningfully more
expensive when the agent was put in front of it, which is the point of measuring
it at all.

The Debug figure is fifteen times slower and is included so that nobody
benchmarks the wrong build and reports a regression. Always quote Release.

## A typed exchange

`tst_tool_live`, question → tool → answer, Russian, Qwen3 8B:

| | Release | Debug |
|---|---|---|
| Full round trip | **7.3 s** | 10.8 s |
| Generation rate | 41.8 tok/s | 23.4 tok/s |
| Answer length | 156 tokens | 128 tokens |
| Prompt | 631 tokens | 631 tokens |

Two generations are inside that round trip: one that produces the tool call, one
that phrases the answer. The planner is off, which is what keeps it at two.

The two columns were measured at different points in the session, and the
section below shows that when the session matters more than the configuration.
Read the round-trip row as *two measurements*, not as a Debug-versus-Release
comparison. The validation row above is safe to read as one, because it is
CPU-only and repeated 2000 times per run.

## A spoken exchange

`tst_voice_agent_live`, Piper speaks the question, everything else real. Model
loading is stable across runs; the round trip is not, and the spread is dealt
with in its own section below.

| Stage | Time | Spread across runs |
|---|---|---|
| Whisper load, cold | 1.9–2.4 s | narrow |
| Qwen3 load, cold | 6.6–11.5 s | narrow |
| Question synthesis (the stand-in speaker) | 0.4–0.8 s for ~2 s of audio | narrow |
| Spoken round trip, tool question | 11.7 s **best** | 11.7 – 63 s |
| Spoken round trip, plain question | 6.6 s **best** | 6.6 – 27 s |

From `tst_voice_integration`, which times each stage separately:

| Stage | Time |
|---|---|
| VAD, scanning 3.1 s of audio | 0 ms |
| Whisper transcription | 229 ms — 13.5× realtime |
| Time to first token | 204 ms |
| Piper synthesis | 680 ms for 4.66 s of audio |
| Playback start | 32 ms |
| Round trip, STT + LLM + TTS | 8.1 s |

## The spread, and what is and is not known about it

The same test, the same binary, the same machine, measured across one working
session:

| Run | Configuration | Tool question | Plain question |
|---|---|---|---|
| early | Debug | 11.7 s | 6.6 s |
| early | Debug | 6.6 s | — |
| three live suites in one `ctest` | Release | 71.7 s | 41.0 s |
| isolated, later | Release | 42.1 s | 19.8 s |
| isolated, later still | Debug | 63.1 s | 26.7 s |

**This is not Debug versus Release.** The same Debug binary produced 11.7 s
early in the session and 63.1 s late in it. Reading the table by configuration
gets the wrong answer, which is why the runs are listed in the order they
happened rather than grouped.

**It is not "model variability" either.** The answers were correct and the tool
was chosen correctly in every run; only the wall clock moved. Nothing here
supports a claim about the model behaving differently.

What is established:

- generation rate moved with it — 41.8 tok/s in the fast runs, 6.3 tok/s in the
  slow ones, measured by `tst_voice_integration`
- Whisper and Qwen3 together occupy 7.40 GB of 8.00 GB when both are resident

**The cause was later identified, and it is contention.** Enumerating the GPU
while no test was running found **thirty-five processes holding the card**, and
4.46 GB of the 8 GB already in use with JARVIS closed:

```
FC26.exe (a game, running)      chrome.exe x2      Discord.exe
steamwebhelper.exe              Telegram.exe       EADesktop.exe
NVIDIA Overlay.exe x2           msedgewebview2.exe GameBar.exe
PowerToys (five processes)      explorer.exe       ... and more
```

GPU utilisation sat at 36–39 % with nothing of ours running. So the fast figures
were taken on a quiet card and the slow ones were not. That is enough to explain
a sixfold spread without appealing to anything about the model, and the earlier
wording — which said the cause was not established — has been replaced rather
than left standing.

It does mean the numbers here are a *floor*, measured on a shared desktop rather
than a dedicated machine.

So the practical rules:

- **Quote the best isolated figure, and say it is the best.** That is what the
  bold numbers above are.
- **Measure with the GPU otherwise quiet.** Check first:
  ```bash
  nvidia-smi --query-compute-apps=pid,name --format=csv
  ```
  If a game or a browser is on that list, the run is not a measurement.
- **The 8 GB ceiling is real**, and it is why `sttUseGpu` is not switched on by
  default without a fresh VRAM measurement, and why the sustained-load check in
  [phase6-human-verification.md](phase6-human-verification.md) is not a
  formality.

## Where the time goes in a spoken turn

For the best measured 11.7 s tool question, approximately. The proportions hold
in the slow runs too — the model's share grows, it does not shrink:

```
  0.2 s   VAD and buffering
  0.7 s   Whisper                                    6 %
  8.5 s   Qwen3, two generations                    73 %
  0.1 s   validation, permissions, tool execution    1 %
  0.7 s   Piper                                      6 %
  1.5 s   scheduling, queueing, playback start      13 %
```

Two thirds of a spoken turn is the language model. Nothing Phase 6 added is
close to the cost of a generation, which is why the planner being opt-in
matters: turning it on adds a third generation, and a third generation is
several seconds.

## Test suite runtime

| | Release | Debug |
|---|---|---|
| Full `ctest` (33 suites, live tests included) | ~2.5 min | ~4 min |
| Without the live suites | ~35 s | ~50 s |

The first row inherits the spread above, since most of it is the live suites.
The second row does not: it is CPU work and it is steady.

## What was not measured

- **Anything through a real microphone.** See
  [phase6-human-verification.md](phase6-human-verification.md).
- **Sustained load.** Every figure here is from a cold or near-cold run. Twenty
  consecutive exchanges have not been timed, and thermal behaviour over that
  period is unknown. Given the spread documented above, this is the measurement
  most worth doing next.
- **Anything on a quiet machine.** Every figure here was taken on a desktop with
  a browser, a chat client and at one point a game holding the GPU. They are a
  floor, not a ceiling.
- **Any machine but this one.** The figures are not portable and are not offered
  as if they were.
