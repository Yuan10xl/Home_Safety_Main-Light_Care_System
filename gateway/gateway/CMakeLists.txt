#===============================================================================
# Gateway-side radar packet receiver and final judgment modules.
#===============================================================================
set(SOURCES
    "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/care_event.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_cloud_report.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_huawei_iot.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_app_interface.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fall_detect_algo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/convulsion_detect_algo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_sle_rx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_radar_receiver.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway_main.c"
    PARENT_SCOPE
)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
