# The UI is split into four QML modules so each directory is a real, importable
# module rather than a subfolder whose types leak into a parent namespace:
#
#   Jarvis.Theme      - design tokens (singleton)
#   Jarvis.Components - reusable building blocks, no application singletons
#   Jarvis.Pages      - the eleven destinations
#   Jarvis.Ui         - the window shell that ties them together
#
# Order matters: dependents are added after the modules they import.

add_subdirectory(themes)
add_subdirectory(components)
add_subdirectory(pages)
add_subdirectory(qml)
