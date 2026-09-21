# The local model engine

JARVIS generates text with [llama.cpp](https://github.com/ggml-org/llama.cpp),
vendored as a pinned submodule and built with the **Vulkan** backend. Nothing
leaves the machine: there is no cloud API anywhere in this path, and none is
planned.

## Layout

| Piece | Where | Role |
|---|---|---|
| `ILLMBackend` | `src/llm` | The only thing the rest of JARVIS knows about |
| `LlamaCppBackend` | `src/llm` | llama.cpp implementation, Vulkan |
| `GgufInspector` | `src/llm` | Reads GGUF headers without loading weights |
| `ModelRegistry` | `src/llm` | Finds usable models on the machine |
| `LlmController` | `src/app` | Qt adapter: streaming, conversation, Core state |

`src/llm` is Qt-free and links llama.cpp **privately**: no llama.h type escapes
the module, which is what makes a second backend possible later without
touching a single caller.

## Why Vulkan and not CUDA

CUDA needs the 3 GB Toolkit and its `nvcc` refuses host compilers it does not
recognise - MSVC 14.50 from Visual Studio 2026 is not on its list. Vulkan needs
only the display driver at run time, and the LunarG SDK at build time for
`glslc`. On an RTX 3070 the measured difference is small enough not to matter.

## Building

`VULKAN_SDK` must point at the LunarG SDK; `scripts/build.bat` falls back to
`C:\VulkanSDK\1.4.357.0` and fails loudly if `glslc.exe` is not there.

Two integration details cost a debugging cycle each and are worth remembering:

**llama.cpp must compile as C++17.** JARVIS builds at C++23, and
`CMAKE_CXX_STANDARD` is inherited by subdirectories. Under C++20 and later a
`u8""` literal is `char8_t[]` rather than `char[]`, which breaks
`llama-chat.cpp` outright. `third_party/CMakeLists.txt` lowers the standard for
the vendored targets only.

**The strict warning flags must not reach upstream code.** `/W4 /WX
/permissive-` live on the `JARVIS::CompileOptions` interface target, never in
`CMAKE_CXX_FLAGS`, so vendored code is not held to this project's warning
policy - and we are not tempted to relax ours because of it.

## Finding models

`ModelRegistry` scans the `models/` folder and **Ollama's blob store**. Reusing
models the user already has is the difference between JARVIS working today and
asking them to download five gigabytes first.

Files are identified by their GGUF magic, not their extension: Ollama stores
weights as content-addressed blobs with no extension at all, and a `.gguf` file
may just as easily be a video diffusion model. Architectures llama.cpp cannot
run as a text model are filtered out.

## Proving the GPU is being used

The number JARVIS reports as `offloadedLayers` is **parsed from what llama.cpp
printed while loading**, never from what was requested. The load log is kept
verbatim in `LoadedModel::loadLog`.

On the reference machine, Qwen3 8B Q4_K_M:

```
model   : Qwen3 8B
device  : NVIDIA GeForce RTX 3070
layers  : 37/36 on GPU
context : 8192 tokens
```

llama.cpp counts one more than `llama_model_n_layer()` because the output layer
is offloaded alongside the 36 repeating blocks - hence 37 of 36. The MODELS
page shows the same numbers, and the HUD's VRAM gauge goes from ~20% to ~95%
when an 8B model loads.

Measured generation, Release build, greedy decoding: **57-58 tok/s**. A Debug
build of the same model runs at ~19 tok/s, because llama.cpp is compiled
unoptimised there.

## Language

The system prompt is assembled from `i18n::LanguagePolicy`, so the assistant
answers in the interface language by default while still understanding the
other. Application names, file names, paths, commands and technical
abbreviations are protected explicitly - without that rule models cheerfully
translate "Visual Studio" and the launcher stops matching anything.

## Thinking models

Qwen3 emits a `<think>...</think>` block before its answer. `LlmController`
filters it out of the stream. The filter is stateful because the markers can
straddle a token boundary.

## Known limitations

- **Qwen3-8B arithmetic in Russian.** Asked *«семнадцать умножить на двадцать
  три»* with spelled-out numerals under greedy decoding, the model answers
  **387**; the correct answer is 391. The same question in English, and the same
  question in Russian with digits, both give 391. This is model behaviour, not a
  pipeline defect - the Russian text itself arrives intact - and it is reported
  by `tst_llm_inference` rather than asserted away.
- **VRAM estimates are deliberately pessimistic.** `estimateVramBytes()` ignores
  the saving from grouped-query attention, so Qwen3 8B at 8192 tokens is
  estimated at 9.07 GB and still loads comfortably on an 8 GB card. Erring
  towards "will not fit" is the right direction; refusing to load is not yet
  wired up.
- **One conversation at a time.** The KV cache is cleared and the whole history
  replayed on every turn. Simple and correct; prompt caching is worth revisiting
  when conversations get long.
- **No tool calling yet.** GBNF grammar support is present in llama.cpp and will
  be used by the tool engine in Phase 7.
