# Phase 6 — The agent

What Phase 6 added, what it refused to add, what was measured, and what a
person still has to check by hand.

Phase 5 gave the model a way to run a tool safely. Phase 6 gives it a way to
run *several*, in order, towards something a person asked for — without
loosening a single thing Phase 5 tightened.

Companion documents:

- [phase6-architecture.md](phase6-architecture.md) — the loop, the states, the seams
- [phase6-security.md](phase6-security.md) — what changed at the boundary, and what did not
- [phase6-testing.md](phase6-testing.md) — the suites and what each one is really asserting
- [phase6-benchmarks.md](phase6-benchmarks.md) — measured figures from this machine
- [phase6-human-verification.md](phase6-human-verification.md) — the checks no test can do

## Scope

JARVIS is a desktop application for Windows with a local AI backend. Phase 6
adds an agent *inside that application*. It is not an operating system: there is
no kernel here, no bootloader, no driver, and nothing in this phase runs outside
a normal user process.

## What exists now

```
src/agent/                       Qt-free agent core
  AgentState.{h,cpp}             14 states and the transitions between them
  Task.{h,cpp}                   TaskId/StepId, statuses, the step identity
  Planner.{h,cpp}                a plan is re-validated step by step
  ContextManager.{h,cpp}         what the model is allowed to see, and how it is framed
  Memory.{h,cpp}                 optional, off by default, refuses secrets by shape
  Recovery.{h,cpp}               failure classes and what may be retried

src/app/
  AgentLoop.{h,cpp}              the orchestrator; owns the task
  LlmController.{h,cpp}          generation only — no longer owns the tool loop
  ToolCoordinator.{h,cpp}        one path after validation
  VoiceController.{h,cpp}        speech now enters through the agent

ui/pages/AgentPage.qml           the task, the steps, the state, Stop
```

`jarvis_agent` links `Qt6::Core` for `QJsonDocument` and nothing else from Qt.
The agent's rules can be tested without an event loop, which is why they live
where they do.

## The shape of a turn

```
request (typed or spoken)
   -> Task            identity, bounded to 32 steps and 4096 characters
   -> Planner         optional; a single call skips it entirely
   -> ContextManager  what the model sees, framed as data
   -> LlmController   generation
   -> ToolValidator   THE BOUNDARY (Phase 5, unchanged)
   -> PermissionManager / ConfirmationManager
   -> ToolExecutor
   -> Recovery        retry, skip, fail or stop — never for a destructive step
   -> answer
```

The direction of that arrow list is the whole design. There is no edge from the
model to the executor, and Phase 6 did not add one.

## What Phase 6 deliberately did not do

- **No second entrance for voice.** A spoken request is submitted through
  `AgentLoop::submit(text, fromVoice = true)`. There is no voice-specific tool
  loop, no voice-specific permission, and no shortcut past a confirmation.
- **No new tool.** The catalogue gained `process_info` and nothing that writes.
- **No relaxation of a Phase 5 rule.** `Destructive` was *added* between
  `ConfirmRequired` and `Denied`; nothing was renamed, and no existing level
  became easier to reach.
- **Memory is not a permission store.** It is off by default, non-persistent by
  default, and everything recalled arrives framed as untrusted data.
- **The planner is opt-in.** A one-step request costs one generation, not
  three. Turning the planner on is a setting, not a default.

## Status

Green on this machine, in both configurations:

| | Debug | Release |
|---|---|---|
| ctest suites | 34 / 34 | 34 / 34 |
| test cases | 748 passed, 0 failed, 0 skipped | 748 passed, 0 failed, 0 skipped |
| JARVIS compiler warnings (`/W4 /permissive- /WX`) | 0 | 0 |
| translation catalogues | 484 / 484 ru, 484 / 484 en | — |

Nothing skipped in either configuration: every live suite found its models and
ran. `tst_voice_agent`, `tst_voice_controller`, `tst_agent_stop` and
`tst_agent_loop` were additionally run **twenty times each in Release** without a
failure, which is where the one Release-only defect of this phase — a data race
in a test double — had shown up.

Release was built from an empty directory for the final run — every object,
every generated header and both third-party libraries recompiled — so nothing
above depends on a leftover artefact. See
[phase6-testing.md](phase6-testing.md#running-them) for exactly which runs were
done which way.

**The build as a whole is not warning-free, and it is worth saying so plainly.**
A from-scratch Release build emits about fifteen warnings, all of them from
`third_party/llama.cpp` and from MSVC's own headers included by it: C4003
(not enough arguments for a function-like macro), C4319 (zero-extending on `~`),
C4297 (a `noexcept` function that throws), C4244 (narrowing conversions) and
C4834 (a discarded `[[nodiscard]]` result).

They are visible on purpose rather than suppressed. `/W4 /permissive- /WX` is
carried by the `JARVIS::CompileOptions` interface target, which only JARVIS's own
targets link, so a warning in our code is an error and a warning in a vendored
library is not. Turning `/WX` on globally would mean either patching upstream or
silencing it, and both are worse than reading the list.

Zero warnings in JARVIS code, in both configurations. Roughly fifteen in
llama.cpp. The project is not warning-free.

The live suites (`tst_tool_live`, `tst_voice_agent_live`,
`tst_voice_integration`, `tst_llm_inference`, `tst_stt`, `tst_tts`) load the
real models and run against real hardware. They skip cleanly on a machine
without them; on this machine they run.

## Requirement audit

Every production requirement Phase 6 was given, and where it is held down. A row
is only marked done when something enforces it — a test, a type, or a default —
not when it was merely implemented.

| Requirement | Status | Held by |
|---|---|---|
| A spoken request takes the same path as a typed one | done | `VoiceController` submits through `AgentLoop`; `tst_voice_agent`, `tst_voice_agent_live` |
| No voice-specific tool loop | done | there is one `submit()`; `tst_prompt_injection` case 10 |
| `Destructive` added between `ConfirmRequired` and `Denied` | done | `tst_agent_gates` (4 cases) |
| No existing permission level renamed or loosened | done | `tst_agent_gates::existingLevelsAreUnchanged` |
| `Destructive` cannot become `Allow` through settings | done | `destructiveIsNeverAllowedByAnyPolicy` |
| Confirmation fingerprint covers `taskId + stepId + tool + args` | done | `tst_agent_gates`, 4 replay routes + 1 positive |
| Memory is data, never instruction | done | `<untrusted_memory>` framing; `tst_agent_memory` (7 cases) |
| Memory `persistent = false` by default | done | `AppConfig` v6; `persistenceIsOffUntilItIsTurnedOn` |
| Tool results bounded by `MAX_TOOL_RESULT` | done | `kMaxToolResultChars = 4096`; `tst_context_manager` |
| A single-step request skips the planner round trip | done | `plannerEnabled = false` by default; `aSingleToolCallSkipsThePlanner` |
| Prompt injection suite, 10 attack classes | done | `tst_prompt_injection`, 27 cases |
| Voice transcription injection covered | done | `anInstructionSpokenAloudChangesNothing` |
| Live tests classify failures by type | done | six classes in `tst_tool_live`; see [testing](phase6-testing.md) |
| Translation key sets equal across languages | done | `tst_i18n_catalogues::bothCataloguesCoverExactlyTheSameStrings` |
| No mojibake anywhere in the tree | done | `tst_i18n_catalogues::noSourceFileIsDamaged` |
| `sttUseGpu` not defaulted on without a fresh VRAM measurement | done | unchanged; the measurement is in [benchmarks](phase6-benchmarks.md) |
| Full regression, Debug and Release | done | 34 / 34 suites, 748 cases, 0 skipped, 0 JARVIS warnings |
| Release-only instability ruled out | done | 20 × 4 timing-sensitive suites in Release, no failure |
| The capture device opens and delivers PCM | done | `tst_microphone_capture` — automated hardware evidence, not a human check |
| Stop and barge-in are not reported as errors | done | `aDeliberateStopIsNotReportedAsAnError` |
| A new voice turn runs after a Stop | done | `aNewVoiceTurnStartsCleanlyAfterAStop` |
| A refused tool leaves the pipeline ready | done | `aFailedToolLeavesThePipelineReady` |
| A synthesis failure leaves no task running | done | `aFailedSynthesisLeavesNoTaskRunning` |
| Human verification of the microphone | **BLOCKED** | [human verification](phase6-human-verification.md) — a person's to do |
| Pre-Phase-6 performance baseline | **BASELINE_UNAVAILABLE** | the agent did not exist before this phase |

Two rows are not "done", and neither is quietly downgraded: no person has spoken
into the microphone, and there is no earlier measurement to compare the agent
against.

The distinction to hold onto:

```
Automated verification : COMPLETE
Human verification     : BLOCKED
```

`tst_microphone_capture` proves the device opens and delivers PCM. It does not
prove anything was said, and it is not offered as if it did. Every requirement
Phase 6 could satisfy in software is satisfied and enforced; nobody has yet sat
down with the application and talked to it.

## What is not verified by any test

One thing, and it is stated plainly rather than buried:

**No test has spoken into the microphone.** Every automated check of the spoken
path synthesises the question with Piper and feeds the samples in. That covers
recognition, the agent, the tools and synthesis; it does not cover the
microphone, room acoustics, or a person's voice. That check is
[phase6-human-verification.md](phase6-human-verification.md), and it is a person's
to perform.

There is also **no pre-Phase-6 baseline** for the agent figures: the agent did
not exist before this phase, so nothing was measured to compare against. The
benchmark document says `BASELINE_UNAVAILABLE` where that is the case rather
than inventing a number to improve on.
