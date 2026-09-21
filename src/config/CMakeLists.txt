# jarvis_config - persisted settings and on-disk locations.
#
# This is the one foundation module that links Qt, and only Qt6::Core:
#   QJsonDocument  - a reviewed JSON implementation instead of a hand-rolled one
#   QSaveFile      - atomic write-and-rename, so an interrupted save cannot
#                    truncate the user's config
#   QStandardPaths - correct %LOCALAPPDATA% resolution
# No GUI module is linked here; see docs/ARCHITECTURE.md.

jarvis_add_module(jarvis_config
    SOURCES
        src/AppConfig.cpp
        src/AppPaths.cpp
        src/ConfigStore.cpp
    HEADERS
        include/jarvis/config/AppConfig.h
        include/jarvis/config/AppPaths.h
        include/jarvis/config/ConfigStore.h
    PUBLIC
        JARVIS::jarvis_core
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
    PRIVATE
        Qt6::Core
)
