# jarvis_voice - speech in, speech out.
#
# Qt-free, like every other engine module. The Qt adapter that owns the audio
# devices and drives the UI lives in src/app; this module knows only about
# samples, text and models.
#
# whisper.cpp is linked privately: no whisper.h type escapes, so ISttBackend can
# be implemented a second time without touching a caller.

# One documented exception to the Qt-free rule: PiperTtsBackend runs Piper as a
# child process and uses QProcess for it. Hand-rolling CreateProcess with pipes
# would add a hundred lines of plumbing to solve a problem QtCore already
# solves. Only Qt6::Core is linked - no GUI, no multimedia. The interfaces
# themselves and WhisperSttBackend stay free of Qt entirely.

jarvis_add_module(jarvis_voice
    SOURCES
        src/PiperTtsBackend.cpp
        src/WindowsTtsBackend.cpp
        src/SpeechText.cpp
        src/VoiceActivityDetector.cpp
        src/VoiceTypes.cpp
        src/WhisperSttBackend.cpp
    HEADERS
        include/jarvis/voice/ISttBackend.h
        include/jarvis/voice/ITtsBackend.h
        include/jarvis/voice/PiperTtsBackend.h
        include/jarvis/voice/WindowsTtsBackend.h
        include/jarvis/voice/SpeechText.h
        include/jarvis/voice/SpeechTone.h
        include/jarvis/voice/VoiceActivityDetector.h
        include/jarvis/voice/VoiceTypes.h
        include/jarvis/voice/WhisperSttBackend.h
    PUBLIC
        JARVIS::jarvis_core
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
    PRIVATE
        whisper
        ggml
        Qt6::Core
        windowsapp
)
