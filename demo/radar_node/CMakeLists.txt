#===============================================================================
# Radar node feature packet builder and SLE TX wrapper.
#===============================================================================
set(SOURCES
    "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_feature.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_sle_tx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_node_main.c"
    PARENT_SCOPE
)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
