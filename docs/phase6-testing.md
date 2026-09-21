# Phase 6 — Testing

What each suite is really asserting, and what none of them can.

34 ctest suites, 748 cases, green in Debug and Release on the reference machine,
with nothing skipped in either. Counts are per Qt Test case, which is what
`Totals:` reports.

The final regression was run three ways, and it is worth being exact about
which, because "clean build" gets used loosely:

- **Debug, incremental** — the everyday run.
- **Debug, `--fresh`** — the CMake cache deleted and regenerated. This catches a
  stale cache; it does not recompile untouched objects.
- **Release, from an empty directory** — `build/msvc-release` deleted outright,
  so every object, every generated header and every third-party library was
  rebuilt. This is the one that catches a header which only compiles because of
  a leftover.

## Running them

```bash
scripts\build.bat debug --test
```

```bash
scripts\build.bat release --test
```

Each suite also writes `build/<config>/tests/<name>-results.txt`. That is not
decoration: Qt Test's stdout does not survive ctest's capture on Windows, so a
failing suite otherwise appears in the ctest log as a bare "Test Failed" with no
case name and no comparison. The file is written whatever happens to stdout, and
it is the first place to look after a failure.

To chase an intermittent failure:

```bash
ctest --test-dir build/msvc-debug -R tst_agent_loop --repeat until-fail:40
```

## The Phase 6 suites

| Suite | Cases | What it is for |
|---|---|---|
| `tst_agent_state` | 25 | the transition table, including the one that cannot exist |
| `tst_agent_task` | 28 | identity, bounds, and the step that cannot be built unvalidated |
| `tst_agent_planner` | 49 | a plan validates whole or not at all |
| `tst_agent_gates` | 15 | `Destructive`, and the four confirmation replay routes |
| `tst_context_manager` | 25 | what the model sees, and the packing rule |
| `tst_agent_memory` | 27 | off by default, bounded, refuses secrets by shape |
| `tst_agent_recovery` | 23 | failure classes; what may and may not be retried |
| `tst_agent_loop` | 33 | the orchestrator, stop, and every property the page binds to |
| `tst_agent_stop` | 16 | stop under load, and late callbacks |
| `tst_prompt_injection` | 27 | ten injection routes, model assumed compromised |
| `tst_voice_agent` | 18 | speech through the agent, scripted model |
| `tst_voice_agent_live` | 5 | speech through the agent, every engine real |
| `tst_i18n_catalogues` | 8 | both languages cover the same strings; no file is mojibake |
| `tst_process_info` | 33 | the one tool Phase 6 added |
| `tst_microphone_capture` | 6 | the real capture device opens and delivers PCM |

## The one test that touches capture hardware

`tst_microphone_capture` is separate from everything above because it answers a
different kind of question. Every other test of the spoken path substitutes
Piper for a speaker; this one opens the actual device and reports what arrived —
device list, default device, format, block count, sample count, RMS and peak —
to `build/<config>/tests/microphone-capture-report.txt`.

It asserts the machine's obligations: a device is enumerated, it opens, blocks
arrive at roughly the claimed rate, and the samples are in the 16 kHz mono float
range the pipeline is written against. It deliberately does **not** assert that
the samples are non-silent, because a quiet room is not a defect and a test that
demanded sound would fail on a correct machine.

It is automated hardware evidence and nothing more. It does not close any check
in [phase6-human-verification.md](phase6-human-verification.md), because no
human spoke.

## The assertions that carry the most weight

**`tst_agent_state`** — that `AwaitingConfirmation` has no edge to `Completed`.
Everything else about confirmations is enforcement; this is the shape that makes
the enforcement possible.

**`tst_agent_gates::destructiveIsNeverAllowedByAnyPolicy`** — enumerates the
policies rather than asserting about one. A test that checked the default would
pass while a setting quietly opened the door.

**`tst_prompt_injection`** — every case supposes the injection succeeded. None
of them assert that the model refused, because that would be a test of the
model.

**`tst_voice_agent::aToolCallIsNeverSpokenAloud`** — the defect this suite was
written for. The pipeline used to stream the model's chunks into synthesis, so
the first generation of a tool round was read out brace by brace.

**`tst_i18n_catalogues::noSourceFileIsDamaged`** — scans every `.cpp`, `.h`,
`.qml`, `.py` and `.md` in the tree for mojibake. It found the header comment of
its own file on the first run, which is the kind of evidence a detector needs.

## Live tests and typed failures

`tst_tool_live` loads Qwen3 8B and asks real questions. When one fails it names
the class of failure, because "the answer was wrong" covers six different faults
with six different owners:

| Class | What it means |
|---|---|
| `MODEL_BUDGET_EXHAUSTED` | the reply ended at the token ceiling; raise the budget |
| `EMPTY_VISIBLE_RESPONSE` | finished under budget with nothing to show; the pipeline lost the text |
| `TOOL_SELECTION_FAILURE` | a valid call, for the wrong tool |
| `INVALID_TOOL_CALL` | the call was rejected as malformed |
| `SECURITY_FAILURE` | something ran that the gates should have stopped |
| `PIPELINE_FAILURE` | the exchange never completed |

The classification reads the token counts, the audit log and the visible answer.
It never guesses, and it never softens: `SECURITY_FAILURE` outranks every other
reading, because it is the only class that means the boundary itself moved.

This distinction was not academic. During Phase 6 a live test failed with an
empty answer and the first two hypotheses — a conversation leak, then planner
prompt size — were both wrong, and both were disproved by tests that passed. The
actual cause was Qwen3 spending its whole budget inside a `<think>` block. The
budget was raised from 320 to 768 tokens, which is a generation setting; not one
assertion was relaxed.

## What the live suites do not cover

`tst_voice_agent_live` synthesises the question with Piper and feeds the samples
into `VoiceController::submitUtterance()`. It covers Whisper, the agent, the
tools, and Piper again. It does **not** cover the microphone.

That substitution is stated in the test's own header, in its report, and here.
The microphone check is a person's, and it is
[phase6-human-verification.md](phase6-human-verification.md).

## Two bugs the tests had themselves

Worth recording, because both looked like product bugs:

1. **A sampling race.** `stepListReflectsRealSteps` polled `steps()` every 25 ms.
   Against a scripted backend the whole two-step plan can finish between two
   samples, so the list was empty every time it looked — about once in
   twenty-five runs. It now watches `taskChanged` instead of sampling, which
   observes every state the page could bind to.

2. **A data race in a test double.** `ScriptedTts` incremented a counter and then
   pushed the text; the assertion waited on the counter and read the text. In
   Release the two writes were free to be seen out of order, so a test saw
   "something was spoken" and then read an empty record. Both are under one
   mutex now, and twenty consecutive Release runs are clean.

3. **A test double that ignored `requestStop()`.** `ScriptedBackend` slept for
   its whole scripted delay whatever anyone asked of it. `ILLMBackend` requires
   a running `generate()` to stop at the next token boundary, so the double was
   not merely unrealistic — it changed what the test measured: the abandoned
   generation stayed on the pool, the next request found the controller busy,
   and a test about starting a new turn after Stop failed for a reason that
   cannot happen with llama.cpp. Honouring the stop also cut that suite from
   43 s to 5 s, which is its own evidence the flag now does something.

None of the three was a defect in JARVIS. All three would have been reported as
one.

## Mojibake

UTF-8 text read as Latin-1 and written back out is silent: the file still
parses, the build stays green, and the only symptom is a model answering
questions nobody asked. It reached a live test during Phase 6 before anyone
noticed.

Two tools guard it now:

- `scripts/repair_mojibake.py` — `--check` to report, no flag to repair
- `tst_i18n_catalogues` — the same algorithm as a test, over the whole tree

The repair script was itself broken twice: its character class started at U+00C0
and missed the C1 controls a byte `0x90` becomes, so it reported 461 damaged
strings as clean; and it encoded whole runs with one codec, which fails when a
run mixes CP1252-only and Latin-1-only characters. Both are fixed, and the
numbers are in the script's comments so the next person knows what a wrong
detector looks like.

**Do not edit source files containing Cyrillic through PowerShell's
`Set-Content -Encoding utf8`.** That is how the damage was introduced. Verify by
code point, not by what the terminal draws.
