#===============================================================================
# Radar light-packet protocol shared by radar node and gateway.
#===============================================================================
set(SOURCES
    "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_packet.c"
    PARENT_SCOPE
)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
