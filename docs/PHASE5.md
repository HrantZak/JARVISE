# Phase 5 — Tools, permissions, confirmation, audit

What was built, what was measured, and what is still missing.

See [TOOLS.md](TOOLS.md) for the catalogue and [SECURITY.md](SECURITY.md) for
the threat model and the guarantees.

## What exists now

```
src/tools/                  Qt-free security core
  ToolTypes.h               permission levels, argument types, ValidatedCall, ToolResult
  ToolRegistry.{h,cpp}      the fixed set; exact-match lookup only
  ToolValidator.{h,cpp}     THE BOUNDARY
  PermissionManager.{h,cpp} policy, in C++, taking no input from the model
  Confirmation.{h,cpp}      one-shot grants bound to one exact call
  AuditLog.{h,cpp}          append-only, thread-safe, bounded
  ToolExecutor.{h,cpp}      the only path to ITool::execute
  SystemTools.{h,cpp}       nine read-only tools + open_application

src/app/                    Qt integration
  ConfirmationManager.{h,cpp}  the dialog's timer and signals
  ToolCoordinator.{h,cpp}      the pipeline, the audit model, the HUD phase
  LlmController.{h,cpp}        the tool loop (modified)

ui/pages/ToolsPage.qml         catalogue, live activity, audit
ui/pages/SecurityPage.qml      rewritten: real state, not a Phase 10 placeholder
ui/qml/ConfirmationOverlay.qml the dialog
ui/qml/SystemHud.qml           tool phase in the status strip (modified)
```

`jarvis_tools` links `Qt6::Core` for `QJsonDocument` and nothing else from Qt.
The security rules are testable without an event loop, which is why they are
where they are.

## Measurements

Reference machine: i9-11900K, 32 GB, RTX 3070 (8 GB), Windows 11 Pro 25H2.
Model: Qwen3 8B Q4_K_M, 37/36 layers on GPU via Vulkan.
Taken from `tst_tool_live::benchmarkTheStages`, not estimated.

| Stage | Time |
|---|---|
| Validation (parse + schema + types + enums) | **1.6 µs** per call |
| `memory_info`, end to end through the coordinator | **3 ms** |
| Full round trip: question → tool → answer | **3429 ms** |
| Generation during that round trip | 198 tokens at 62.0 tok/s, 568-token prompt |

The validator is four orders of magnitude cheaper than a single token. Whatever
the tool system costs, it is not the checking.

`cpu_info` and `network_info` take ~120 ms because a load figure is a delta and
the first sample has no interval to measure against. That is a deliberate wait
for a real number rather than a fast fabricated one — and it runs on the pool,
so the interface keeps its frame rate (`tst_tool_loop::executionHappensOffTheGuiThread`
asserts the event loop keeps turning throughout).

**Not measured:** CPU, RAM and VRAM overhead attributable to the tool system
specifically. The tools read counters that were already being collected once a
second for the HUD, so the marginal cost is a validator call and a map of
strings — below the noise floor of the measurement, and reporting a number for
it would be inventing one.

## Live behaviour

Real questions, real model, real machine data. Full transcripts in
`tst_tool_live`; abbreviated here:

> **Сколько у меня оперативной памяти?**
> Ваш компьютер имеет следующее количество оперативной памяти:
> **Всего**: 31,89 ГБ · **Использовано**: 19,02 ГБ · **Доступно**: 12,87 ГБ · **Загрузка**: 59,6%

> **Какая у меня видеокарта?**
> Видеокарта: NVIDIA GeForce RTX 3070. Общее количество видеопамяти — 8,00 ГБ,
> используется — 7,24 ГБ. Температура — 63 °C, загрузка — 90%.

> **Какая у меня операционная система?**
> Ваша операционная система: Windows 11 Pro 25H2 (build 26200).

> **Сколько будет два плюс два?**
> Два плюс два будет четыре. *(no tool used)*

Each figure is asserted against `WindowsSystemMetricsProvider` in the test. The
number never passes through the model: C++ reads it, inserts it as data, and the
model's only job is to say it back.

## Defects found and fixed

**1. The think filter ate the last six characters of every answer.**

`ThinkFilter::feed()` holds back up to six characters at the tail in case they
are the start of a `<think>` marker split across tokens. Nothing ever released
them. Present since Phase 3 and invisible on prose — "четыре." arrived as "ч" —
but fatal for a tool call, which lost its closing `": {}}` and stopped parsing
entirely. The live test failed with the model emitting perfectly correct calls
that never validated.

*Fix:* `ThinkFilter::flush()`, called when generation ends.
*Regression test:* `tst_llm_stream::deliversEveryCharacterOfTheReply`, seven
cases including replies shorter than the holdback and ones ending in `}` or `<`.

**2. The tool round limit looped forever.**

At the limit the model was told to stop and asked to answer. If it emitted
another call, the same branch ran again — notice, regenerate, notice — without
bound. With `maxRounds = 2` a runaway model produced seven generations and
counting.

*Fix:* the notice is given once; a further request ends the exchange.
*Regression test:* `tst_llm_stream::loopStopsAtTheRoundLimit`.

**3. `system_info` reported the wrong operating system.**

The registry's `ProductName` still reads "Windows 10 Pro" on Windows 11 —
Microsoft never updated it. A read-only tool that misreports a fact is exactly
what this phase forbids.

*Fix:* corrected in `WindowsSystemMetricsProvider` using the build number.
*Verified:* `tst_tool_live::modelAsksForTheOperatingSystem`.

**4. `ThreadPool::post` could not carry a move-only task.**

It took `std::function`, which requires a copyable target. A confirmation grant
must not be copyable — two callers holding permission the user gave once is the
whole thing the type prevents.

*Fix:* `std::move_only_function<void()>`. Strictly more permissive; every
existing caller still compiles.

## Tests

19 suites, all passing in Debug and Release.

| Suite | Cases | Covers |
|---|---|---|
| `tst_tools` | 85 | validator, registry, permissions, allowlist |
| `tst_tool_security` | 72 | prompt injection, model compromise, the 20 listed attacks |
| `tst_tool_pipeline` | 26 | confirmation rules, audit, executor, threading |
| `tst_tool_loop` | 17 | Qt layer, dialog lifecycle, off-GUI execution |
| `tst_llm_stream` | 15 | streaming, think filter, loop bound, result framing |
| `tst_tool_live` | 7 | real model, real data, benchmarks |

Phase 1–4 suites are unchanged and still pass.

## Human verification required

These cannot be asserted by software and are not claimed as passing:

- [ ] **Press Allow on a real confirmation** and see Calculator open. The gate
      is fully tested against spy tools; the automated suite stops short of
      launching an application, deliberately.
- [ ] **Press Cancel** and confirm nothing opens. *(The software half — that
      declining produces `NOT PERFORMED` and never dispatches — is asserted in
      `tst_tool_loop::decliningLeavesTheMachineUntouched`.)*
- [ ] **Let a confirmation lapse** and confirm nothing opens. *(Software half
      asserted in `tst_tool_loop::lapsingProducesNoGrant`.)*
- [ ] **Look at the TOOLS and SECURITY pages** and judge whether they read
      clearly in Russian.
- [ ] **Watch the HUD** during a tool call.

## Not implemented

- Persistent audit log on disk. In memory only.
- Per-tool permission editing from the interface.
- File access with protected paths.
- Scheduled or automated actions.

All four are shown as `TODO` on the Security page rather than being silently
absent.
