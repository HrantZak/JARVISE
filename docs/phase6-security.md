# Phase 6 — Security

What changed at the boundary, what did not, and how each claim is held down.

[SECURITY.md](SECURITY.md) has the Phase 5 threat model and is still current.
This document covers only what Phase 6 added, and the one thing it is most
important to say about that: **nothing was loosened.**

## The claim

> A fully compromised language model can obtain read-only facts about this
> computer, and nothing else, unless a person allows a specific action by hand.

Phase 6 gave the model more rope — several actions in sequence, a memory, a
voice. The claim is unchanged, and `tst_prompt_injection` and
`tst_tool_security` are what keep it that way.

## Permission levels

`Destructive` was added **between** `ConfirmRequired` and `Denied`:

```
ReadOnly  <  SafeAction  <  ConfirmRequired  <  Destructive  <  Denied
```

Nothing was renamed and no existing level moved. What `Destructive` guarantees:

- **No policy allows it outright.** There is no configuration under which a
  destructive tool executes without a specific human approval —
  `tst_agent_gates::destructiveIsNeverAllowedByAnyPolicy`.
- **It always counts as needing confirmation**, whatever else a setting says —
  `destructiveAlwaysCountsAsNeedingConfirmation`.
- **It is off by default**, and asks when on —
  `destructiveIsOffByDefaultAndAsksWhenOn`.
- **It is never retried automatically** by `RecoveryPolicy`. An approval is for
  one call and is spent by it.

No tool in the shipped catalogue is `Destructive`. The level exists so that when
one is added, the gate is already there and already tested.

## Confirmation scope

A Phase 5 grant was bound to `tool + arguments`. That was enough for a single
call and not enough for a task: the same approval could be replayed at a
different step, or in a different task, for the same action.

The fingerprint is now:

```
taskId + stepId + tool + arguments
```

built by `agent::stepIdentity()` and `tools::scopedFingerprint()` from a
null-separated canonical text. Four replay routes are closed, one test each:

| Replay attempt | Test |
|---|---|
| the same approval in another task | `approvalCannotBeReplayedInAnotherTask` |
| the same approval at another step | `approvalCannotBeReplayedAtAnotherStep` |
| the same approval with other arguments | `approvalCannotBeReplayedWithOtherArguments` |
| the same approval for another tool | `approvalCannotBeReplayedForAnotherTool` |

And the positive case, so the gate is not simply always closed:
`approvalWorksAtTheStepItWasGivenFor`.

`TaskId` and `StepId` are monotonic and never reused, so a fingerprint cannot
come back into scope by a counter wrapping.

## The ten injection routes

`tst_prompt_injection` supposes, in every case, that the injection **worked** —
that the model read the hostile text, believed it, and is now doing exactly what
the attacker asked. The scripted backend plays that compromised model. No test
there asserts that the model refused.

| # | Where the hostile text comes from | What holds |
|---|---|---|
| 1 | the typed request | validation is downstream of the model; a believed instruction is still just a call to validate |
| 2 | a real tool's output | results arrive framed in `<tool_result>` |
| 3 | remembered text | recall arrives framed in `<untrusted_memory>`, and the store refuses instruction-shaped text |
| 4 | a forged `"confirmed": true` | the call format has room for `tool` and `arguments` and nothing else |
| 5 | a forged `"permission": "ReadOnly"` | permission is a property of the registry entry, not a field in the call |
| 6 | a `<tool_result>` the model wrote itself | the loop's record of what ran comes from the executor |
| 7 | a shell under another name | the registry is closed after startup; ten names tried, none resolve |
| 8 | twenty calls in one reply | one reply yields at most one execution |
| 9 | a plan whose last step escalates | a plan validates whole, or not at all |
| 10 | speech | the transcript is submitted through the same `submit()` a keyboard uses |

Route 10 deserves a sentence of its own. An instruction played from a speaker in
the room is transcribed faithfully by Whisper — that is Whisper working
correctly. It then becomes a task like any other, and meets every gate in the
same order. Speaking is not a privileged way in.

## What the model never sees

- No system-message channel it can write to. Refusals reach it through the
  context as data, never as a system turn —
  `tst_agent_loop::anUnknownToolIsRefusedAndReported` asserts the refusal
  arrives with `Role::User`.
- No permission state. It cannot read the current policy, so it cannot reason
  about how to satisfy one.
- No confirmation state. It cannot tell whether a dialog is open, so it cannot
  time a request around one.

## The audit log

Append-only, bounded, thread-safe, and it **never echoes raw model output** —
`tst_tool_pipeline::auditNeverEchoesRawModelOutput`. A log that quoted the model
would be an injection route into whoever reads the log.

When the bound is reached it says so rather than dropping silently:
`auditIsBoundedAndSaysSoWhenItDrops`.

## Data that could be attacker-controlled

`process_info` reports process names, and a process name is not under our
control. `sanitiseProcessName()` strips control characters — a name containing a
newline could otherwise forge a field boundary in the result text. Bounded at
`kMaxProcesses = 20` so the result stays well inside the context cap.

The tool was renamed once during Phase 6: its count field was `process_count`,
which collided with the `process_NN` prefix in the result format. It is
`total_processes` now. A collision like that is a parsing ambiguity, and a
parsing ambiguity in a result is a place to hide something.

## Privacy

Unchanged from Phase 4 and 5, and worth restating because Phase 6 added a
memory:

- No microphone audio is written to disk, by default or otherwise, in the normal
  path.
- Memory is off by default and non-persistent by default. Turning on persistence
  is two explicit settings, not one.
- Nothing leaves the machine. There is no network call anywhere in the agent.

## What is still missing

- **`open_application` remains the only non-read-only tool**, and no automated
  test presses Allow on it — approving it launches a real window. The gate
  around it is tested against spy tools instead. This is a deliberate gap and it
  is listed in [phase6-human-verification.md](phase6-human-verification.md).
- **The audit log is in memory only.** It does not survive a restart.
- **There is no rate limit on confirmations.** A model that asks repeatedly can
  produce repeated dialogs; the user's answer is still required every time, so
  this is an annoyance rather than a hole, but it is not addressed.
