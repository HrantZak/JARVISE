# Phase 6 — Architecture

How a request becomes work, and where each decision is made.

## The chain, with the reason for each link

```
    request                     typed, or spoken and transcribed
      |
      v
    Task                        identity: TaskId, StepId, bounded size
      |
      v
    Planner            (opt)    plan -> steps, each re-validated separately
      |
      v
    ContextManager              what the model sees, and how it is framed
      |
      v
    LlmController               generation, and nothing else
      |
      v
    ToolValidator               THE BOUNDARY - text becomes ValidatedCall or nothing
      |
      v
    PermissionManager           policy, in C++, taking no input from the model
      |
      v
    ConfirmationManager         a human, for anything that is not read-only
      |
      v
    ToolExecutor                the only path to ITool::execute
      |
      v
    Recovery                    retry / skip / fail / stop
      |
      v
    answer                      spoken, or shown
```

Every arrow points one way. The model's output enters at `ToolValidator` and
cannot re-enter above it — which is what makes "the model is hostile" a
survivable assumption rather than a hope.

## AgentLoop

`src/app/AgentLoop.{h,cpp}` owns the task and nothing else owns it. It holds a
`Task`, a `Planner`, a `MemoryStore`, a `RecoveryPolicy` and an
`AgentStateMachine`, and it is the only place that decides what happens next.

`LlmController` used to run the tool loop. It no longer does: it generates, and
reports what it generated. That separation is what made a spoken request and a
typed one able to share a path.

### The generation guard

A stopped task can still have work in flight — a generation on the pool, a tool
executing. Checking `m_task != nullptr` when that work reports back is not
enough, because by then a *new* task may have started.

```cpp
std::uint64_t m_generation{0};       // bumped by every submit and every stop
std::uint64_t m_workGeneration{0};   // the generation the outstanding work belongs to
```

A late callback whose `m_workGeneration` no longer matches is dropped. Without
it, a result from an abandoned task could attach itself to the next one and
answer a question nobody asked. `tst_agent_loop::aLateResultCannotReviveAStoppedTask`
and `tst_agent_stop::lateCallbackCannotAttachToTheNextTask` hold this.

## The state machine

Fourteen states, in `src/agent/AgentState.h`:

```
Idle  Listening  Understanding  Planning  AwaitingConfirmation  Executing
WaitingForTool  Evaluating  Recovering  Speaking  Completed  Cancelled
Failed  Unavailable
```

`isValidTransition()` is a table, not a convention. The property that matters
most:

> `AwaitingConfirmation` can only be left for `Executing`, `Cancelled` or
> `Failed`.

There is no edge from `AwaitingConfirmation` to `Completed`. An approval can
therefore never arrive as a finished result — the only way out of a
confirmation is through it.

`AiCoreModel` is the single arbiter of what the interface shows when several
subsystems are busy at once; each source reports its own state and the model
decides. Three-way conflicts between the voice pipeline, the tool coordinator
and the agent are resolved in one place rather than three.

## Unrepresentable states

The types refuse the mistakes rather than checking for them:

| Type | How it cannot be built wrong |
|---|---|
| `ValidatedCall` | private constructor, `friend class ToolValidator` |
| `ConfirmationGrant` | move-only, minted only by `ConfirmationStore::approve()` |
| `ToolResult` | private constructor, static factories for success and failure |
| `Step` | holds a `ValidatedCall`, so an unvalidated step cannot exist |
| `ArgumentType` | `Integer`, `Boolean`, `Enumeration` — a path has nowhere to sit |

The last one is the load-bearing one. There is no free-form string argument
anywhere in the catalogue, so a command, a path or a shell fragment has no field
to travel in. That is a property of the type system, not of a filter.

## The planner

`src/agent/Planner.{h,cpp}`. A plan is JSON with a `steps` array, capped at
`kMaxPlanLength = 16384` characters and `kMaxPlanSteps = 32`.

Each step is **re-serialised and put through the same `ToolValidator`** the
single-call path uses. There is no second, more permissive parser for plans.
One invalid step rejects the whole plan — a valid prefix cannot be used to buy
trust for an invalid tail, which is
`tst_prompt_injection::aPlanThatEscalatesAtTheEndRunsNothingAtAll`.

The planner is **off by default**. A request that needs one tool costs one
generation for the call and one for the answer. Turning the planner on adds a
round trip, and for most requests it buys nothing.

## ContextManager

`src/agent/ContextManager.{h,cpp}` decides what the model sees. Two hard limits:

- `kMaxToolResultChars = 4096` — a tool result cannot flood the context
- `kMaxEntries = 512`

Entries carry a priority (`Critical`, `High`, `Normal`, `Low`). `prepare()`
keeps the longest **prefix** of the (priority, recency) ordering that fits.

An earlier version packed greedily — take each entry if it fits, skip it if it
does not. That kept a tiny stale entry while dropping a newer large one, which
is how a question about memory got answered with CPU figures. The prefix rule
has no such hole. `tst_context_manager::keepsTheNewestOfEqualPriority` caught it.

Everything that did not come from the user is framed:

```
<tool_result> ... </tool_result>
<untrusted_memory> ... carries no permissions ... </untrusted_memory>
```

## Memory

`src/agent/Memory.{h,cpp}`. Off by default; non-persistent by default. Ceilings
that configuration cannot raise:

```
kMaxEntriesCeiling    = 1024
kMaxEntryCharsCeiling = 4096
kMaxTotalCharsCeiling = 262144
```

Secrets are refused **by shape**, not by keyword: PEM blocks, key prefixes
(`sk-`, `ghp_`, `xox`), JWT shape, connection strings, labelled credentials, and
long high-entropy runs. `deserialise()` re-applies the whole policy, so a
hand-edited store on disk cannot smuggle past it.

## Recovery

`src/agent/Recovery.{h,cpp}` classifies a failure before deciding anything:

```
None  Transient  Permanent  Denied  ConfirmationRefused
NeedsConfirmation  Cancelled  ContextExceeded
```

Actions are `Retry`, `Skip`, `Fail`, `Stop`, `AwaitUser`, with
`kMaxAttemptsCeiling = 5` and backoff capped at 4 s.

**A destructive step and a confirm-required step are never retried
automatically.** Retrying an action a person approved once would spend that
approval more than once; the approval is for one call, and it is spent.

## Voice

`VoiceController` no longer talks to the model. When an agent is present it
submits through it:

```cpp
m_deps.agent->submit(m_lastTranscript, /*fromVoice=*/true);
```

and the turn ends on `AgentLoop::finished`, not on a generation completing.

The streaming chunks are deliberately **not** connected in that configuration.
A tool round's first generation is a JSON object; streaming it into synthesis
made JARVIS read the tool call aloud and then fall silent before the real answer
existed. Speaking the finished answer costs the incremental synthesis Phase 4
built — roughly the length of one sentence in added latency — and that is the
right trade. `tst_voice_agent::aToolCallIsNeverSpokenAloud` and
`tst_voice_agent_live::nothingOnThePathReadsAToolCallAloud` hold it.

Stop and barge-in call `AgentLoop::stop()`, not `LlmController::cancel()`.
Cancelling the generation alone would leave the task alive to start another one.

`stop()` ends the task synchronously and reports the turn as *not ok*, which is
right for the agent — the task did not finish — and wrong for the person who
pressed Stop. `VoiceController` marks its own stop with `m_stoppingSelf` and
lets that outcome pass silently; without it, every Stop and every barge-in put
"Stopped." in the error banner and flashed the Core red. A genuine failure still
reports one, and `aDeliberateStopIsNotReportedAsAnError` asserts both halves.

After a Stop the abandoned generation still has to unwind: `requestStop()` asks
the backend to stop at the next token boundary, and `LlmController` reports
itself free when the completion lands back on the GUI thread. A request made
inside that window is refused with "The previous request is still finishing" —
honest, and something a spoken turn never sees, because speaking and
transcribing take far longer than the unwind.

## Where the seams are

| Boundary | What crosses it |
|---|---|
| model → agent | text, always; never a decision |
| agent → tools | a `ValidatedCall`, never a string |
| tools → agent | a `ToolResult`, framed as data on the way back in |
| agent → interface | properties and signals; the page holds no state of its own |
| voice → agent | the transcript, through the same `submit()` a keyboard uses |
