# jarvis_agent - the agent's own logic, free of Qt.
#
# Task state, planning and the execution graph are decisions, not presentation.
# Keeping them here means they can be tested exhaustively without an event loop,
# and it keeps the Qt orchestration in src/app thin enough to read.
#
# This module must never gain the ability to execute anything. It decides what
# should happen; jarvis_tools decides whether it may, and does it.

jarvis_add_module(jarvis_agent
    SOURCES
        src/AgentState.cpp
        src/ContextManager.cpp
        src/Memory.cpp
        src/Planner.cpp
        src/Recovery.cpp
        src/Task.cpp
    HEADERS
        include/jarvis/agent/AgentState.h
        include/jarvis/agent/ContextManager.h
        include/jarvis/agent/Memory.h
        include/jarvis/agent/Planner.h
        include/jarvis/agent/Recovery.h
        include/jarvis/agent/Task.h
    PUBLIC
        JARVIS::jarvis_core
        JARVIS::jarvis_i18n
        JARVIS::jarvis_tools
    PRIVATE
        # QJsonDocument only, for the same reason jarvis_tools links it: a
        # reviewed JSON parser on hostile input beats a hand-rolled one. No
        # other part of Qt is used, and none belongs here.
        Qt6::Core
)
