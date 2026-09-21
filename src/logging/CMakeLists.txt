# jarvis_logging - structured logging with pluggable sinks.
#
# Qt-free by design: every later module (including the ones wrapping llama.cpp
# and whisper.cpp on worker threads) logs through this, and none of them should
# be forced to link QtCore to do it.

jarvis_add_module(jarvis_logging
    SOURCES
        src/LogLevel.cpp
        src/LogRecord.cpp
        src/Logger.cpp
        src/ConsoleSink.cpp
        src/FileSink.cpp
    HEADERS
        include/jarvis/logging/ConsoleSink.h
        include/jarvis/logging/FileSink.h
        include/jarvis/logging/ILogSink.h
        include/jarvis/logging/LogLevel.h
        include/jarvis/logging/Logger.h
        include/jarvis/logging/LogRecord.h
    PUBLIC
        JARVIS::jarvis_core
)
