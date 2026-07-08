#===============================================================================
# @brief    cmake file
#===============================================================================
set(SOURCES
    "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_sensor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_sensor_selftest.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_lamp_switch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_node_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_node/radar_feature.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_node/radar_node_main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/radar_node/radar_sle_tx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/radar_protocol/radar_packet.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/radar_protocol/radar_protocol.c"
    PARENT_SCOPE
)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
