set(GATEWAY_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES_LIST
    ${GATEWAY_ROOT}/common/radar_protocol/radar_protocol.c
    ${GATEWAY_ROOT}/common/radar_protocol/radar_packet.c
    ${GATEWAY_ROOT}/gateway/care_event.c
    ${GATEWAY_ROOT}/gateway/gateway_cloud_report.c
    ${GATEWAY_ROOT}/gateway/gateway_huawei_iot.c
    ${GATEWAY_ROOT}/gateway/gateway_app_interface.c
    ${GATEWAY_ROOT}/gateway/fall_detect_algo.c
    ${GATEWAY_ROOT}/gateway/convulsion_detect_algo.c
    ${GATEWAY_ROOT}/gateway/gateway_sle_rx.c
    ${GATEWAY_ROOT}/gateway/gateway_radar_receiver.c
    ${GATEWAY_ROOT}/gateway/gateway_main.c
    ${GATEWAY_ROOT}/wifi/wifi_connect.c
    ${GATEWAY_ROOT}/gateway_runtime.c
)

set(HEADER_LIST
    ${GATEWAY_ROOT}
    ${GATEWAY_ROOT}/common/radar_protocol
    ${GATEWAY_ROOT}/gateway
    ${GATEWAY_ROOT}/sle
    ${GATEWAY_ROOT}/wifi
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
