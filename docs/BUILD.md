# Building JARVIS

## Verified environment

This is the exact environment the project was built and tested on.

| Component | Version | Location |
|---|---|---|
| Windows | 11 Pro 10.0.26200 | — |
| Visual Studio | 2026 Community 18.5.1 | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| MSVC toolset | 14.50.35717 (`cl` 19.50.35729) | inside the VS install |
| Windows SDK | 10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10` |
| Qt | 6.11.1 **msvc2022_64** | `C:\QtX\6.11.1\msvc2022_64` |
| CMake | 4.4.2 | `C:\Program Files\CMake\bin` |
| Ninja | 1.12.1 | `C:\QtX\Tools\Ninja` |
| Git | 2.55.0 | on `PATH` |

CMake and Ninja are **not** on `PATH`; the presets and the build script use
absolute paths on purpose, so the build does not depend on how a shell happens
to be configured.

### Required Qt components

Under `Qt 6.11.1` in the Qt Maintenance Tool:

- **MSVC 2022 64-bit** (the kit itself)
- Qt Multimedia — microphone capture and audio output, from Phase 4
- Qt Shader Tools — shader compilation for the AI Core effects
- Qt Quick 3D / Quick Timeline are present and used from Phase 2

**Do not install the MinGW kit for this project.** Qt libraries built with
MinGW cannot link against MSVC, and `CMakeLists.txt` rejects non-MSVC compilers
outright (`nvcc` on Windows requires MSVC, and Phase 8 targets Win32 APIs).

### Not required

- **CUDA Toolkit** — not used. The GPU path is Vulkan (see docs/ARCHITECTURE.md).
- **Vulkan SDK** — not needed until Phase 3, when `llama.cpp` is introduced.
- Any external test framework — tests use Qt Test, so the build stays offline.

## Build

```
scripts\build.bat debug
```

```
scripts\build.bat release --test
```

Arguments: `debug` | `release`, `--test` to run ctest afterwards, `--fresh` to
discard the CMake cache first.

The script initialises the MSVC environment (`vcvars64.bat`) before invoking
CMake, which the Ninja generator cannot do for itself.

Output:

```
build\msvc-debug\bin\JARVIS.exe
build\msvc-debug\bin\tst_core.exe
build\msvc-debug\bin\tst_logging.exe
build\msvc-debug\bin\tst_config.exe
```

`windeployqt` copies the Qt runtime next to `JARVIS.exe` after every relink, so
the executable runs straight from the build directory.

## Presets

| Preset | Generator | Use |
|---|---|---|
| `msvc-debug` | Ninja | day-to-day command-line builds |
| `msvc-release` | Ninja | optimised builds |
| `vs2026` | Visual Studio 18 2026 | opening the project in the VS IDE |

The Ninja presets require a shell where `vcvars64.bat` has run — that is what
`scripts\build.bat` is for. The `vs2026` preset finds MSVC by itself:

```
cmake --preset vs2026
```

then open `build\vs2026\JARVIS.sln`.

## Tests

```
ctest --preset msvc-debug
```

Or run a suite directly for the full case list:

```
build\msvc-debug\bin\tst_core.exe -o result.txt,txt
```

The test targets set `PATH` to the Qt `bin` directory themselves, so `ctest`
works from a shell that has never heard of Qt.

## Compiler settings

`/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc`, C++23, and
**warnings are errors** (`/WX`). To investigate a warning without the build
stopping:

```
cmake --preset msvc-debug -DJARVIS_WARNINGS_AS_ERRORS=OFF
```

Turn it back on before committing.

## Troubleshooting

**`Could not read presets from ...`**
`cmake --preset` resolves presets relative to the current directory. Run
`scripts\build.bat`, or `cd` to the project root first.

**`JARVIS requires the MSVC toolchain`**
The configure step found a non-MSVC compiler. Use `scripts\build.bat` or the
`vs2026` preset rather than a bare `cmake` call from a MinGW shell.

**`Forbidden Qt module linked by JARVIS target(s)`**
Something linked `Qt6::Widgets` or `Qt6::Charts`. This is intentional — see
docs/ARCHITECTURE.md. Draw charts with `QtQuick.Shapes`.

**`windeployqt.exe was not found`**
The Qt kit path in `CMakePresets.json` (`CMAKE_PREFIX_PATH`) does not match your
install. The executable will still build but needs Qt on `PATH` to run.

**The application exits immediately with code 2**
The QML root object failed to load. The reason is in
`%LOCALAPPDATA%\JARVIS\logs\jarvis-<date>.log` — Qt's own warnings are routed
into the JARVIS log, so QML errors appear there with file and line.

## Runtime data

```
%LOCALAPPDATA%\JARVIS\
├── config.json
├── logs\
├── data\
└── models\
```

Delete that folder to reset JARVIS to a first-run state.
