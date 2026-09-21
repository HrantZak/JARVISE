# jarvis_llm - local text generation.
#
# Qt-free. The Qt-side adapter that streams tokens into QML lives in src/app;
# this module knows only about models, tokens and devices.
#
# llama.cpp is linked privately: nothing above this module sees a llama.h type,
# which is what lets ILLMBackend be implemented a second time later without
# touching a single caller.

jarvis_add_module(jarvis_llm
    SOURCES
        src/GenerationParams.cpp
        src/GgufInspector.cpp
        src/LlamaCppBackend.cpp
        src/ModelInfo.cpp
        src/ModelRegistry.cpp
    HEADERS
        include/jarvis/llm/GenerationParams.h
        include/jarvis/llm/GgufInspector.h
        include/jarvis/llm/ILLMBackend.h
        include/jarvis/llm/LlamaCppBackend.h
        include/jarvis/llm/ModelInfo.h
        include/jarvis/llm/ModelRegistry.h
    PUBLIC
        JARVIS::jarvis_core
        JARVIS::jarvis_logging
    PRIVATE
        llama
        ggml
)
