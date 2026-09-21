# Unit tests.
#
# Qt Test is used instead of an external framework so the build stays offline:
# JARVIS is a local-first project and its build should not need to download a
# dependency to run its own tests.

function(jarvis_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

    qt_add_executable(${name} ${ARG_SOURCES})

    target_link_libraries(${name} PRIVATE
        JARVIS::CompileOptions
        Qt6::Test
        ${ARG_LIBRARIES}
    )

    set_target_properties(${name} PROPERTIES
        WIN32_EXECUTABLE FALSE    # console subsystem: ctest needs the output
        FOLDER "Tests"
    )

    # `-o <file>,txt` in addition to the console output. Qt Test's stdout does
    # not survive ctest's capture on Windows - a failing test shows up in the
    # ctest log as a bare "Test Failed" with no case name and no comparison,
    # which is useless for an intermittent failure. The file is written whatever
    # happens to stdout, so there is always something to read afterwards.
    add_test(NAME ${name} COMMAND ${name} -o ${name}-results.txt,txt -o -,txt)

    # Qt DLLs are not on PATH in a fresh shell, so point the test at them.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "PATH=${QT6_INSTALL_PREFIX}/bin;$ENV{PATH}"
        TIMEOUT 120
    )
endfunction()

jarvis_add_test(tst_scene3d SOURCES tst_scene3d.cpp LIBRARIES jarvis_scene3d)

jarvis_add_test(tst_deepseek
    SOURCES tst_deepseek.cpp ../src/app/DeepSeekBackend.cpp
    LIBRARIES JARVIS::jarvis_llm Qt6::Network
)

jarvis_add_test(tst_windows
    SOURCES tst_windows.cpp
    LIBRARIES JARVIS::jarvis_tools user32
)

jarvis_add_test(tst_network_discovery
    SOURCES tst_network_discovery.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)
target_include_directories(tst_network_discovery PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app"
)

jarvis_add_test(tst_screen_analysis
    SOURCES tst_screen_analysis.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core Qt6::Gui
)
target_include_directories(tst_screen_analysis PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app"
)

jarvis_add_test(tst_desktop_live
    SOURCES tst_desktop_live.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Network user32 shell32
)

jarvis_add_test(tst_core
    SOURCES tst_core.cpp
    LIBRARIES JARVIS::jarvis_core
)
jarvis_add_test(tst_shell
    SOURCES tst_shell.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)
jarvis_add_test(tst_artifacts
    SOURCES tst_artifacts.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)

jarvis_add_test(tst_logging
    SOURCES tst_logging.cpp
    LIBRARIES JARVIS::jarvis_logging
)

jarvis_add_test(tst_config
    SOURCES tst_config.cpp
    LIBRARIES JARVIS::jarvis_config Qt6::Core
)

jarvis_add_test(tst_system
    SOURCES tst_system.cpp
    LIBRARIES JARVIS::jarvis_system
)

# Needs a GUI application object: QFontDatabase is not available under a bare
# QCoreApplication.
jarvis_add_test(tst_i18n
    SOURCES tst_i18n.cpp
    LIBRARIES JARVIS::jarvis_i18n JARVIS::jarvis_config Qt6::Gui
)

# The catalogue files themselves, read from the source tree rather than from
# the embedded resource: the checks are about what is committed - both languages
# covering the same strings, nothing left untranslated, and no file anywhere
# carrying mojibake.
jarvis_add_test(tst_i18n_catalogues
    SOURCES tst_i18n_catalogues.cpp
    LIBRARIES Qt6::Core
)
target_compile_definitions(tst_i18n_catalogues
    PRIVATE JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

jarvis_add_test(tst_llm
    SOURCES tst_llm.cpp
    LIBRARIES JARVIS::jarvis_llm
)

# Real inference against a real model. Skips cleanly when the machine has no
# GGUF model; when it does have one, loading several gigabytes and generating
# takes far longer than the default 30-second ctest timeout allows.
jarvis_add_test(tst_llm_inference
    SOURCES tst_llm_inference.cpp
    LIBRARIES JARVIS::jarvis_llm
)
set_tests_properties(tst_llm_inference PROPERTIES TIMEOUT 900)

# Real speech recognition: Piper synthesises the audio, Whisper transcribes it.
# Skips when the model or Piper is absent. Loading large-v3-turbo alone takes
# longer than the default timeout.
jarvis_add_test(tst_stt
    SOURCES tst_stt.cpp
    LIBRARIES JARVIS::jarvis_voice
)
set_tests_properties(tst_stt PROPERTIES TIMEOUT 900)

# Real speech synthesis through Piper. Skips when Piper or a voice is absent.
jarvis_add_test(tst_tts
    SOURCES tst_tts.cpp
    LIBRARIES JARVIS::jarvis_voice
)
set_tests_properties(tst_tts PROPERTIES TIMEOUT 300)

jarvis_add_test(tst_windows_tts
    SOURCES tst_windows_tts.cpp
    LIBRARIES JARVIS::jarvis_voice Qt6::Core
)
set_tests_properties(tst_windows_tts PROPERTIES TIMEOUT 90)

# Real microphone capture. AudioCapture lives in the application target rather
# than a library, so its source is compiled into the test directly. Needs a GUI
# application object for QtMultimedia, and a real device - it SKIPs otherwise.
jarvis_add_test(tst_audio
    SOURCES tst_audio.cpp "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
    LIBRARIES JARVIS::jarvis_voice Qt6::Multimedia Qt6::Gui
)
target_include_directories(tst_audio PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_audio PROPERTIES AUTOMOC ON)
set_tests_properties(tst_audio PROPERTIES TIMEOUT 300)

# Real speaker output, playing audio Piper synthesises during the test.
jarvis_add_test(tst_audio_player
    SOURCES tst_audio_player.cpp "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
    LIBRARIES JARVIS::jarvis_voice Qt6::Multimedia Qt6::Gui
)
target_include_directories(tst_audio_player PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_audio_player PROPERTIES AUTOMOC ON)
set_tests_properties(tst_audio_player PROPERTIES TIMEOUT 300)

# The physical microphone: does a real device open, and does real PCM arrive?
# The only test that touches capture hardware. Skips visibly when there is no
# input device rather than substituting synthetic audio, because a synthetic
# substitute would prove nothing about the microphone.
jarvis_add_test(tst_microphone_capture
    SOURCES
        tst_microphone_capture.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_microphone_capture PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_microphone_capture PROPERTIES AUTOMOC ON)
set_tests_properties(tst_microphone_capture PROPERTIES TIMEOUT 120)

# Voice pipeline orchestration. Uses scripted STT/TTS doubles so the state
# machine is what is under test; real recognition and synthesis are covered by
# tst_stt and tst_tts.
jarvis_add_test(tst_voice_controller
    SOURCES
        tst_voice_controller.cpp
        "${CMAKE_SOURCE_DIR}/src/app/VoiceController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        # VoiceController holds LlmController* and AudioCapture*; the definitions
        # are needed even though these tests pass null pointers for them.
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        # LlmController drives the tool loop, so its collaborators come with it.
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_voice_controller PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_voice_controller PROPERTIES AUTOMOC ON)
set_tests_properties(tst_voice_controller PROPERTIES TIMEOUT 300)

# The voice pipeline joined to the agent: that a spoken request takes the same
# road as a typed one, meets the same gate, and that a tool call is never read
# aloud. Scripted model and scripted speech, real tools - the gate under test
# has to be the real one.
jarvis_add_test(tst_voice_agent
    SOURCES
        tst_voice_agent.cpp
        "${CMAKE_SOURCE_DIR}/src/app/VoiceController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_system
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_voice_agent PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_voice_agent PROPERTIES AUTOMOC ON)
set_tests_properties(tst_voice_agent PROPERTIES TIMEOUT 300)

# Prompt injection at the level of the whole agent: ten classes of hostile text,
# each one supposing the model was successfully persuaded by it. Needs the voice
# pipeline too, because speech is one of the ten.
jarvis_add_test(tst_prompt_injection
    SOURCES
        tst_prompt_injection.cpp
        "${CMAKE_SOURCE_DIR}/src/app/VoiceController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_system
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_prompt_injection PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_prompt_injection PROPERTIES AUTOMOC ON)
set_tests_properties(tst_prompt_injection PROPERTIES TIMEOUT 600)

# close_application: the capability, its gate, and the chain that reaches it.
# Closes nothing - live behaviour needs a running application and lives apart.
jarvis_add_test(tst_close_application
    SOURCES
        tst_close_application.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
    LIBRARIES
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_system
        Qt6::Gui
)
target_include_directories(tst_close_application PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_close_application PROPERTIES AUTOMOC ON)
set_tests_properties(tst_close_application PROPERTIES TIMEOUT 300)

# The tool security boundary. These are the tests that have to pass before the
# language model is allowed anywhere near the tool system, so they depend on
# nothing heavyweight: no model, no audio device, no GUI.
jarvis_add_test(tst_tools
    SOURCES tst_tools.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)

# The agent's state machine and the arbitration that decides what the interface
# shows when several subsystems are busy at once.
jarvis_add_test(tst_agent_state
    SOURCES
        tst_agent_state.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_agent_state PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_agent_state PROPERTIES AUTOMOC ON)

# The task and step model. Qt-free in substance: the model under test includes
# no Qt at all, and a step cannot be built without a real validated call.
jarvis_add_test(tst_agent_task
    SOURCES tst_agent_task.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_agent_task PROPERTIES AUTOMOC ON)

# The planner. A plan is untrusted input with several calls in it, so it faces
# every attack the single-call path faces, plus the ones a plan makes possible.
jarvis_add_test(tst_agent_planner
    SOURCES tst_agent_planner.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_agent_planner PROPERTIES AUTOMOC ON)

# The safety gates once tasks exist: the Destructive level, and approvals bound
# to one step of one task so they cannot be replayed elsewhere.
jarvis_add_test(tst_agent_gates
    SOURCES tst_agent_gates.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_agent_gates PROPERTIES AUTOMOC ON)

# The context budget and compaction rules, plus the framing that keeps tool
# results and memory as data rather than instructions.
jarvis_add_test(tst_context_manager
    SOURCES tst_context_manager.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_context_manager PROPERTIES AUTOMOC ON)

# Memory: what may be written down at all, and the framing that keeps whatever
# is written from becoming an instruction.
jarvis_add_test(tst_agent_memory
    SOURCES tst_agent_memory.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_agent_memory PROPERTIES AUTOMOC ON)

# The recovery policy. Mostly about what it refuses: an irreversible action is
# never repeated on a guess, and an approval is never spent twice.
jarvis_add_test(tst_agent_recovery
    SOURCES tst_agent_recovery.cpp
    LIBRARIES JARVIS::jarvis_agent Qt6::Core
)
set_target_properties(tst_agent_recovery PROPERTIES AUTOMOC ON)

# Global Stop, from every state work can be in - and the harder half: whether
# work already in flight can come back later and undo it.
jarvis_add_test(tst_agent_stop
    SOURCES
        tst_agent_stop.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_agent_stop PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_agent_stop PROPERTIES AUTOMOC ON)

# process_info against the real process table, and the fact that a process name
# is text somebody else chose.
jarvis_add_test(tst_process_info
    SOURCES tst_process_info.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)
set_target_properties(tst_process_info PROPERTIES AUTOMOC ON)
set_tests_properties(tst_agent_stop PROPERTIES TIMEOUT 300)

# The agent loop, with a scripted model and the real tool boundary. What most of
# these tests pin down is what the loop cannot do.
jarvis_add_test(tst_agent_loop
    SOURCES
        tst_agent_loop.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_agent_loop PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_agent_loop PROPERTIES AUTOMOC ON)
set_tests_properties(tst_agent_loop PROPERTIES TIMEOUT 300)

# The Qt layer around the tool system: the confirmation dialog's rules and the
# coordinator's pipeline, with a real event loop and the real registry.
jarvis_add_test(tst_tool_loop
    SOURCES
        tst_tool_loop.cpp
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_config
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_tool_loop PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_tool_loop PROPERTIES AUTOMOC ON)
set_tests_properties(tst_tool_loop PROPERTIES TIMEOUT 300)

# Streaming and the loop's control flow, driven by a scripted backend so token
# boundaries can be placed exactly where they break things.
jarvis_add_test(tst_llm_stream
    SOURCES
        tst_llm_stream.cpp
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_llm_stream PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_llm_stream PROPERTIES AUTOMOC ON)
set_tests_properties(tst_llm_stream PROPERTIES TIMEOUT 300)

# The whole loop with a real model: Qwen3 asks for a tool, the tool reads this
# machine, and the answer is checked against the operating system. Loads several
# gigabytes, so it gets the same long timeout as the other live suites.
jarvis_add_test(tst_tool_live
    SOURCES
        tst_tool_live.cpp
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
    LIBRARIES
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_i18n
        JARVIS::jarvis_logging
        Qt6::Gui
)
target_include_directories(tst_tool_live PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_tool_live PROPERTIES AUTOMOC ON)
set_tests_properties(tst_tool_live PROPERTIES TIMEOUT 1800)

# The model-compromise suite: every attack from the Phase 5 specification, run
# against the registry the application actually builds.
jarvis_add_test(tst_tool_security
    SOURCES tst_tool_security.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)

# Confirmation, audit log and the executor. Also Qt-free in substance: the
# security rules are testable without an event loop, which is the point of
# keeping them out of the Qt layer.
jarvis_add_test(tst_tool_pipeline
    SOURCES tst_tool_pipeline.cpp
    LIBRARIES JARVIS::jarvis_tools Qt6::Core
)

# The whole pipeline with every engine real: Piper, Whisper, Qwen3, Piper again
# and the speaker. Loads two multi-gigabyte models, so it gets a long timeout.
jarvis_add_test(tst_voice_integration
    SOURCES
        tst_voice_integration.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        JARVIS::jarvis_llm
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_voice_integration PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_voice_integration PROPERTIES AUTOMOC ON)
set_tests_properties(tst_voice_integration PROPERTIES TIMEOUT 1200)

# A spoken question all the way to a tool and back, with every engine real:
# Piper speaks it, Whisper hears it, the agent plans it, the real tool answers
# it, and Piper says the answer. Loads two multi-gigabyte models.
jarvis_add_test(tst_voice_agent_live
    SOURCES
        tst_voice_agent_live.cpp
        "${CMAKE_SOURCE_DIR}/src/app/VoiceController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioPlayer.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AudioCapture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AiCoreModel.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LlmController.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/AgentLoop.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ToolCoordinator.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ConfirmationManager.cpp"
    LIBRARIES
        JARVIS::jarvis_voice
        JARVIS::jarvis_llm
        JARVIS::jarvis_config
        JARVIS::jarvis_agent
        JARVIS::jarvis_tools
        JARVIS::jarvis_system
        Qt6::Multimedia
        Qt6::Gui
)
target_include_directories(tst_voice_agent_live PRIVATE "${CMAKE_SOURCE_DIR}/src/app")
set_target_properties(tst_voice_agent_live PROPERTIES AUTOMOC ON)
set_tests_properties(tst_voice_agent_live PROPERTIES TIMEOUT 1800)
