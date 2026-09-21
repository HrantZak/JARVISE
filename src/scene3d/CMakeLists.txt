qt_add_library(jarvis_scene3d STATIC
    SceneGeometry.cpp SceneGeometry.h SceneController.cpp SceneController.h
    Mesh.cpp Mesh.h Expression.h Drawing.cpp)
target_link_libraries(jarvis_scene3d PUBLIC Qt6::Quick3D Qt6::Concurrent PRIVATE JARVIS::CompileOptions)
target_include_directories(jarvis_scene3d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
