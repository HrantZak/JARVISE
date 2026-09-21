# Helper functions shared by the JARVIS build.

# Collect every target defined at or below <dir> into <out_var>.
function(jarvis_collect_targets out_var dir)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(sub IN LISTS subdirs)
        jarvis_collect_targets(sub_targets "${sub}")
        list(APPEND targets ${sub_targets})
    endforeach()
    set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

# Fail the configure step if any JARVIS target links a Qt module that this
# project has ruled out. This turns a project rule into a build-breaking
# guarantee rather than a comment somebody can forget.
#
#   Qt6::Widgets - JARVIS is a pure Qt Quick application; QtWidgets would pull
#                  in a second widget stack and its own style machinery.
#   Qt6::Charts  - asserts "No style available without QApplication" and crashes
#                  under QGuiApplication (verified during the environment audit).
function(jarvis_assert_no_forbidden_qt_modules)
    set(forbidden Qt6::Widgets Qt::Widgets Qt6::Charts Qt::Charts)

    jarvis_collect_targets(all_targets "${CMAKE_SOURCE_DIR}")

    set(violations "")
    foreach(target IN LISTS all_targets)
        if(NOT TARGET ${target})
            continue()
        endif()

        get_target_property(target_type ${target} TYPE)
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            set(props INTERFACE_LINK_LIBRARIES)
        else()
            set(props LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        endif()

        foreach(prop IN LISTS props)
            get_target_property(libs ${target} ${prop})
            if(NOT libs)
                continue()
            endif()
            foreach(lib IN LISTS libs)
                if(lib IN_LIST forbidden)
                    list(APPEND violations "${target} -> ${lib}")
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(violations)
        list(JOIN violations "\n    " pretty)
        message(FATAL_ERROR
            "Forbidden Qt module linked by JARVIS target(s):\n    ${pretty}\n"
            "QtWidgets and QtCharts are excluded by project policy. "
            "Draw charts with QtQuick.Shapes instead.")
    endif()

    message(STATUS "  Qt policy     : no QtWidgets / QtCharts (verified)")
endfunction()

# Declare a JARVIS static library module with the project's conventions applied.
#
#   jarvis_add_module(<name>
#       SOURCES  <files...>
#       HEADERS  <files...>          # for IDE grouping only
#       PUBLIC   <deps...>
#       PRIVATE  <deps...>
#   )
#
# The public include directory is always <module-dir>/include.
function(jarvis_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;HEADERS;PUBLIC;PRIVATE" ${ARGN})

    add_library(${name} STATIC ${ARG_SOURCES} ${ARG_HEADERS})
    add_library(JARVIS::${name} ALIAS ${name})

    target_include_directories(${name}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${name}
        PUBLIC JARVIS::CompileOptions ${ARG_PUBLIC}
        PRIVATE ${ARG_PRIVATE}
    )

    set_target_properties(${name} PROPERTIES
        FOLDER "Modules"
        AUTOMOC OFF
    )
endfunction()
