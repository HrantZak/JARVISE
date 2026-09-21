# Architecture

## Principles

1. **Local-first.** No component may require a network service to function.
   Internet access, when it arrives, is an optional tool behind its own module.
2. **The UI thread is never blocked.** Model inference, audio, file scanning and
   tool execution run on worker threads and report back through queued Qt
   signals. Target: 60 FPS, always.
3. **Failures are values.** Every fallible operation returns `Result<T>`.
   Exceptions are caught at module boundaries and converted, so one subsystem
   failing cannot unwind through another or terminate the process.
4. **The model never executes code.** When the LLM lands, it will only be able
   to name a registered tool. Argument validation and permission level are
   decided in C++, never by the model's own assertion about what it is allowed
   to do.
5. **Nothing pretends to work.** A capability that does not exist is absent from
   the interface or explicitly labelled `NOT IMPLEMENTED`. There are no
   decorative buttons and no simulated data.

## Layers

```
┌──────────────────────────────────────────────────────────┐
│ ui/            Qt Quick — four QML modules                │
│   Jarvis.Theme        design tokens (singleton)           │
│   Jarvis.Components   17 reusable pieces, no singletons   │
│   Jarvis.Pages        the eleven destinations             │
│   Jarvis.Ui           window shell, navigation, HUD       │
├──────────────────────────────────────────────────────────┤
│ src/app/       Composition root. The only place that      │
│                knows every module at once.                │
│   Bootstrap        brings subsystems up in order          │
│   AppController    build info, paths, live settings       │
│   AiCoreModel      Core state + audio/progress signals    │
│   SystemMonitor    1 Hz telemetry, published to QML       │
│   TranslationManager  loads and swaps language at runtime │
│   LlmController    generation, and nothing else           │
│   AgentLoop        owns the task; decides what happens next│
│   ToolCoordinator  one path from a call to an execution   │
│   VoiceController  microphone -> agent -> speaker         │
├──────────────────────────────────────────────────────────┤
│ src/agent/     Task, Planner, Context, Memory,     [Qt6::Core]
│                Recovery, the state machine                │
├──────────────────────────────────────────────────────────┤
│ src/tools/     The security boundary: registry,    [Qt6::Core]
│                validator, permissions, audit              │
├──────────────────────────────────────────────────────────┤
│ src/voice/     Whisper, Piper, VAD                 [pure] │
├──────────────────────────────────────────────────────────┤
│ src/llm/       llama.cpp backend, model registry   [pure] │
├──────────────────────────────────────────────────────────┤
│ src/system/    Win32 + NVML machine telemetry      [pure] │
├──────────────────────────────────────────────────────────┤
│ src/i18n/      Language identity + LLM language     [pure]│
│                rules (LanguagePolicy)                     │
├──────────────────────────────────────────────────────────┤
│ src/config/    Settings + on-disk locations   [Qt6::Core] │
├──────────────────────────────────────────────────────────┤
│ src/logging/   Levelled logging, pluggable sinks   [pure] │
├──────────────────────────────────────────────────────────┤
│ src/core/      Error, Result, ThreadPool, build info[pure]│
└──────────────────────────────────────────────────────────┘
```

### Telemetry

`jarvis_system` reads the machine through native APIs only - `GetSystemTimes`
for CPU, `GlobalMemoryStatusEx` for memory, `GetIfTable2` for network, and NVML
for the GPU. No WMI: it is slow, can block for seconds and would need COM on the
sampling thread.

NVML lives in `nvml.dll`, which ships with the NVIDIA display driver. It is
loaded with `LoadLibrary` at run time, so the build needs no CUDA Toolkit and
gains no third-party dependency. On a machine without an NVIDIA GPU the library
is simply absent, `GpuMetrics::available` stays false, and the UI reports the
GPU metrics as unavailable rather than as zero.

Sampling runs on the shared `ThreadPool` because `GetIfTable2` and NVML can each
block for milliseconds - far too long on a thread that owes the compositor a
frame every 16 ms. Results return through a queued invocation, and a second
sample is never queued behind a slow one.

Rate metrics (CPU, network) report `valid == false` on the first sample: there
is no interval to divide by yet, and inventing a zero would be a lie the UI
would faithfully render.

### Language

Russian is the default and English is the alternative; both are primary. The
presentation layer is the only thing that changes language - internal names,
state keys, proper nouns, paths and technical abbreviations never do. See
[I18N.md](I18N.md) for the full contract.

`LanguagePolicy` is the second seam into Phase 3: it produces the language
section of the LLM system prompt from the user's choice, so the llama.cpp
backend consumes a tested, reviewed set of rules rather than inventing its own
wording.

### The engine seam

`AiCoreModel` is the boundary between the interface and the engines that do not
exist yet. It exposes `state`, `inputLevel`, `outputLevel`, `progress` and
`statusText`; Phase 3 onwards drives them from C++ and the UI follows without a
single QML change. With no engine attached the state is `Offline` and every
level is zero - nothing here fabricates a value.

`previewState()` is the one deliberate exception: a development tool that forces
a visual state so the language can be inspected. It sets `previewing`, which the
Core and the title bar both display as a badge, and it never claims an engine is
running.

Dependencies point downward only. `core` knows nothing about anything else.

### Why `core` and `logging` are Qt-free

Later phases wrap plain C/C++ libraries — llama.cpp, whisper.cpp — and run them
on worker threads. Those wrappers need error types and a logger; forcing them to
link QtCore for that would be wrong. `config` is the one foundation module that
does link Qt, and only `Qt6::Core`, for three specific things:

| Need | Qt facility | Why not hand-rolled |
|---|---|---|
| JSON | `QJsonDocument` | A reviewed parser beats a bespoke one for a file users edit |
| Atomic save | `QSaveFile` | Write-to-temp-then-rename, so an interrupted save cannot truncate the config |
| `%LOCALAPPDATA%` | `QStandardPaths` | Correct known-folder resolution without hand-written Win32 |

## Decisions

### QtWidgets and QtCharts are excluded

JARVIS is a pure Qt Quick application. `QtCharts` was evaluated and rejected: its
`ChartView` requires `QApplication` and aborts under `QGuiApplication` with

```
ASSERT: "No style available without QApplication!"
  qtbase\src\widgets\kernel\qapplication.cpp:914
```

This was reproduced during the environment audit. It would have forced the whole
QtWidgets stack into the process. HUD graphs are drawn with `QtQuick.Shapes`
instead, which also matches the visual language far better than a stock chart.

The rule is enforced, not documented and hoped for:
`jarvis_assert_no_forbidden_qt_modules()` in `cmake/JarvisHelpers.cmake` fails
the configure step if any target links `Qt6::Widgets` or `Qt6::Charts`.

### MSVC only

`nvcc` on Windows accepts only MSVC as a host compiler, and several Win32
subsystems planned for Phase 8 are MSVC-oriented. `CMakeLists.txt` fails fast on
any other toolchain rather than producing a build that breaks three phases later.

### GPU backend: Vulkan first

`llama.cpp` and `whisper.cpp` will be built against their Vulkan backends. This
needs only the installed NVIDIA driver plus the Vulkan SDK for shader
compilation, delivers 85–95% of CUDA throughput on an RTX 3070, and sidesteps
the fact that CUDA 13 does not officially support MSVC 14.50. `ILLMBackend`
keeps CUDA available as a second implementation later.

### Four QML modules, not one

`ui/themes`, `ui/components`, `ui/pages` and `ui/qml` are separate
`qt_add_qml_module` targets with URIs `Jarvis.Theme`, `Jarvis.Components`,
`Jarvis.Pages` and `Jarvis.Ui`. Files in subdirectories of a single module would
land in implicit sub-namespaces; real modules keep imports explicit and let
qmllint resolve them. `QT_QML_OUTPUT_DIRECTORY` is set once at the top level so
each module's output path matches its URI.

The split is also a dependency rule: **`Jarvis.Components` may not read `App`,
`Core` or `Sys`.** Components take values through properties, which keeps the
library reusable and independently testable. Anything that needs a singleton
lives in `Jarvis.Pages` or `Jarvis.Ui`.

## Threading

`core::ThreadPool` is the single place worker threads come from. It defaults to
`hardware_concurrency() - 1`, leaving a core for the GUI thread.

- `submit()` returns a `std::future`; exceptions land in the future.
- `post()` is fire-and-forget; exceptions go to an installed handler and never
  reach a worker's stack unwind.
- Dropping a task after `shutdown()` breaks its promise, so a caller waiting on
  the future gets an error instead of hanging forever.

## Startup order

`Bootstrap::initialise()` — order matters and is enforced:

1. Resolve `%LOCALAPPDATA%\JARVIS` and create the directory tree. If that is
   impossible, fall back to `JARVIS-data` beside the executable and record a
   warning; the application still starts.
2. Load `config.json`. A missing file is not an error — defaults are written. A
   corrupt file is reported *and* degrades to defaults, because a broken config
   must never leave the user without an application.
3. Start logging at the configured level, attach the file sink (and the console
   sink if enabled).

Every non-fatal problem from this sequence is surfaced in the UI under
`STARTUP WARNINGS`, not silently swallowed.

## Logging

Synchronous and mutex-serialised, deliberately: the GUI thread does not log per
frame, and a synchronous writer means the last lines are on disk before a crash.
`Warning` and above flush immediately.

**Privacy rule:** conversation content, file contents and user data are not
written to the log. Log identifiers, outcomes and errors.

## Configuration

`config.json` only ever contains settings the running build actually honours.
Sections for AI, voice and permissions appear in the phase that implements them,
so the file never advertises a knob that does nothing.

Out-of-range values are clamped rather than rejected, and every adjustment is
reported through `ValidationReport` so it can be logged and shown.

## What Phases 1-2 do not include

LLM, STT, TTS, wake word, tool engine, Windows control, memory, permissions and
the model manager. Each has a phase; none is stubbed in the meantime. Eight of
the eleven destinations exist as navigable pages that state plainly what is
missing and which phase delivers it.
