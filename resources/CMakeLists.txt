# PassVault resources - qss + icons - packed into a single Qt resource.
#
# Icons are optional: only .svg files that actually exist on disk are packed.
# See resources/icons/README.md for the expected Lucide set.

set(_qss_files
    qss/tokens_dark.qss
    qss/tokens_light.qss
    qss/components.qss
    qss/app.qss
)

file(GLOB _icon_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")

set(_resource_files ${_qss_files} ${_icon_files})

qt_add_resources(passvault "passvault_resources"
    PREFIX "/"
    BASE   "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES  ${_resource_files}
)

message(STATUS "PassVault resources: ${_qss_files}")
list(LENGTH _icon_files _icon_count)
message(STATUS "PassVault icons packed: ${_icon_count}")
