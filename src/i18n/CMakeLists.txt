# jarvis_i18n - language identity and the language rules for the model.
#
# Qt-free: the LLM backend that will consume LanguagePolicy in Phase 3 wraps
# llama.cpp and has no business linking QtCore to build a prompt string.
# Everything Qt-side (QTranslator, retranslation) lives in src/app.

jarvis_add_module(jarvis_i18n
    SOURCES
        src/Language.cpp
        src/LanguagePolicy.cpp
    HEADERS
        include/jarvis/i18n/Language.h
        include/jarvis/i18n/LanguagePolicy.h
    PUBLIC
        JARVIS::jarvis_core
)
