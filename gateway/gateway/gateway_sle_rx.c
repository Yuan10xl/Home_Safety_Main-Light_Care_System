#include "gateway_sle_rx.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common_def.h"
#include "errcode.h"
#include "securec.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_client.h"

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

#define GATEWAY_RADAR_NODE_NAME "RADAR_NODE_01"
#define GATEWAY_SLE_SERVICE_UUID 0xABCD
#define GATEWAY_SLE_NOTIFY_PROPERTY_UUID 0xBCDE
#define GATEWAY_SLE_MTU_SIZE 512
#define GATEWAY_SLE_SEEK_INTERVAL 100
#define GATEWAY_SLE_SEEK_WINDOW 100
#define GATEWAY_SLE_STREAM_BUFFER_SIZE (RADAR_LIGHT_PACKET_WIRE_SIZE * 4u)
#define GATEWAY_SLE_ADV_NAME_MAX 32u
#define GATEWAY_SLE_ADV_LOG_RAW_MAX 40u
#define GATEWAY_SLE_ADV_TYPE_SERVICE_DATA_16BIT_UUID 0x03u
#define GATEWAY_SLE_ADV_TYPE_COMPLETE_LIST_16BIT_UUID 0x05u
#define GATEWAY_SLE_ADV_TYPE_INCOMPLETE_LIST_16BIT_UUID 0x07u
#define GATEWAY_SLE_ADV_TYPE_SHORT_LOCAL_NAME 0x0Au
#define GATEWAY_SLE_ADV_TYPE_COMPLETE_LOCAL_NAME 0x0Bu
#define GATEWAY_BLE_ADV_TYPE_INCOMPLETE_LIST_16BIT_UUID 0x02u
#define GATEWAY_BLE_ADV_TYPE_COMPLETE_LIST_16BIT_UUID 0x03u
#define GATEWAY_BLE_ADV_TYPE_SHORT_LOCAL_NAME 0x08u
#define GATEWAY_BLE_ADV_TYPE_COMPLETE_LOCAL_NAME 0x09u
#define GATEWAY_BLE_ADV_TYPE_SERVICE_DATA_16BIT_UUID 0x16u
#define GATEWAY_SLE_ENABLE_PAIRING 0

#define GATEWAY_SAFE_LIGHT_CMD_MAGIC0          0xA5
#define GATEWAY_SAFE_LIGHT_CMD_MAGIC1          0x5A
#define GATEWAY_SAFE_LIGHT_CMD_VERSION         1
#define GATEWAY_SAFE_LIGHT_CMD_SIZE            6
#define GATEWAY_SAFE_LIGHT_CMD_SAFE_PERCENT    15u
#define GATEWAY_SAFE_LIGHT_CMD_INDEX_VERSION   2
#define GATEWAY_SAFE_LIGHT_CMD_INDEX_CMD       3
#define GATEWAY_SAFE_LIGHT_CMD_INDEX_ARG       4
#define GATEWAY_SAFE_LIGHT_CMD_INDEX_CHECKSUM  5

typedef struct {
    char name[GATEWAY_SLE_ADV_NAME_MAX];
    bool has_name;
    bool has_target_uuid;
} gateway_sle_adv_info_t;

static gateway_radar_packet_callback_t g_gateway_packet_callback;
static uint8_t g_gateway_stream_buffer[GATEWAY_SLE_STREAM_BUFFER_SIZE];
static size_t g_gateway_stream_len;

static sle_announce_seek_callbacks_t g_gateway_seek_cbks;
static sle_connection_callbacks_t g_gateway_connect_cbks;
static ssapc_callbacks_t g_gateway_ssapc_cbks;
static sle_addr_t g_gateway_radar_addr;
static uint16_t g_gateway_conn_id;
static uint16_t g_gateway_notify_handle;
static uint16_t g_gateway_service_start_handle;
static uint16_t g_gateway_service_end_handle;
static bool g_gateway_target_found;
static bool g_gateway_property_found;
static bool g_gateway_service_found;
static bool g_gateway_exchange_started;
static bool g_gateway_property_find_started;
static bool g_gateway_connected;
static uint8_t g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_SIZE];

static bool gateway_sle_property_has_descriptor(const ssapc_find_property_result_t *property,
                                                uint8_t descriptor_type)
{
    uint16_t i;

    if (property == NULL) {
        return false;
    }

    for (i = 0; i < property->descriptors_count; i++) {
        if (property->descriptors_type[i] == descriptor_type) {
            return true;
        }
    }

    return false;
}

static void gateway_sle_log_property_descriptors(const ssapc_find_property_result_t *property)
{
    uint16_t i;

    if (property == NULL) {
        return;
    }

    for (i = 0; i < property->descriptors_count; i++) {
        GATEWAY_LOG("[GATEWAY] property descriptor[%u]=0x%x\r\n",
                    (unsigned int)i,
                    (unsigned int)property->descriptors_type[i]);
    }
}

static void gateway_sle_stream_shift(size_t consumed)
{
    if (consumed >= g_gateway_stream_len) {
        g_gateway_stream_len = 0u;
        return;
    }

    memmove(g_gateway_stream_buffer,
            &g_gateway_stream_buffer[consumed],
            g_gateway_stream_len - consumed);
    g_gateway_stream_len -= consumed;
}

static void gateway_sle_dispatch_one_packet(const radar_light_packet_t *packet)
{
    GATEWAY_LOG("[GATEWAY] crc ok\r\n");
    GATEWAY_LOG("[GATEWAY] packet seq=%u motion=%u static=%u\r\n",
                (unsigned int)packet->seq,
                (unsigned int)packet->motion_level,
                (unsigned int)packet->static_duration_ms);

    if (g_gateway_packet_callback != NULL) {
        g_gateway_packet_callback(packet);
    }
}

static void gateway_sle_try_decode_stream(void)
{
    while (g_gateway_stream_len >= RADAR_LIGHT_PACKET_WIRE_SIZE) {
        radar_light_packet_t packet;
        radar_packet_status_t status;

        status = radar_packet_decode(g_gateway_stream_buffer,
                                     RADAR_LIGHT_PACKET_WIRE_SIZE,
                                     &packet);
        if (status == RADAR_PACKET_OK) {
            gateway_sle_dispatch_one_packet(&packet);
            gateway_sle_stream_shift(RADAR_LIGHT_PACKET_WIRE_SIZE);
            continue;
        }

        GATEWAY_LOG("[GATEWAY] packet drop reason=%s\r\n",
                    radar_packet_status_name(status));
        gateway_sle_stream_shift(1u);
    }
}

static bool gateway_sle_adv_contains_name(const uint8_t *data, uint8_t data_len, const char *name)
{
    size_t name_len;

    if ((data == NULL) || (name == NULL)) {
        return false;
    }

    name_len = strlen(name);
    if ((name_len == 0u) || (data_len < name_len)) {
        return false;
    }

    for (uint8_t i = 0; (size_t)i + name_len <= data_len; i++) {
        if (memcmp(&data[i], name, name_len) == 0) {
            return true;
        }
    }

    return false;
}

static void gateway_sle_adv_copy_name(gateway_sle_adv_info_t *info,
                                      const uint8_t *value,
                                      uint8_t value_len)
{
    uint8_t copy_len;

    if ((info == NULL) || (value == NULL) || (value_len == 0u)) {
        return;
    }

    copy_len = value_len;
    if (copy_len >= GATEWAY_SLE_ADV_NAME_MAX) {
        copy_len = GATEWAY_SLE_ADV_NAME_MAX - 1u;
    }

    if (memcpy_s(info->name, sizeof(info->name), value, copy_len) != EOK) {
        return;
    }
    info->name[copy_len] = '\0';
    info->has_name = true;
}

static void gateway_sle_adv_parse_uuid16_list(gateway_sle_adv_info_t *info,
                                              const uint8_t *value,
                                              uint8_t value_len)
{
    uint16_t uuid16;
    uint16_t i;

    if ((info == NULL) || (value == NULL)) {
        return;
    }

    for (i = 0; (i + 1u) < value_len; i += 2u) {
        uuid16 = (uint16_t)value[i] | ((uint16_t)value[i + 1u] << 8);
        GATEWAY_LOG("[GATEWAY] seek service uuid=0x%04x\r\n", (unsigned int)uuid16);
        if (uuid16 == GATEWAY_SLE_SERVICE_UUID) {
            info->has_target_uuid = true;
        }
    }
}

static void gateway_sle_adv_parse_field(gateway_sle_adv_info_t *info,
                                        uint8_t type,
                                        const uint8_t *value,
                                        uint8_t value_len)
{
    if ((info == NULL) || (value == NULL)) {
        return;
    }

    if ((type == GATEWAY_SLE_ADV_TYPE_COMPLETE_LOCAL_NAME) ||
        (type == GATEWAY_SLE_ADV_TYPE_SHORT_LOCAL_NAME) ||
        (type == GATEWAY_BLE_ADV_TYPE_COMPLETE_LOCAL_NAME) ||
        (type == GATEWAY_BLE_ADV_TYPE_SHORT_LOCAL_NAME)) {
        gateway_sle_adv_copy_name(info, value, value_len);
        return;
    }

    if ((type == GATEWAY_SLE_ADV_TYPE_COMPLETE_LIST_16BIT_UUID) ||
        (type == GATEWAY_SLE_ADV_TYPE_INCOMPLETE_LIST_16BIT_UUID) ||
        (type == GATEWAY_SLE_ADV_TYPE_SERVICE_DATA_16BIT_UUID) ||
        (type == GATEWAY_BLE_ADV_TYPE_COMPLETE_LIST_16BIT_UUID) ||
        (type == GATEWAY_BLE_ADV_TYPE_INCOMPLETE_LIST_16BIT_UUID) ||
        (type == GATEWAY_BLE_ADV_TYPE_SERVICE_DATA_16BIT_UUID)) {
        gateway_sle_adv_parse_uuid16_list(info, value, value_len);
    }
}

static void gateway_sle_adv_parse_type_len(const uint8_t *data,
                                           uint8_t data_len,
                                           gateway_sle_adv_info_t *info)
{
    uint16_t offset = 0;

    while ((offset + 2u) <= data_len) {
        uint8_t type = data[offset];
        uint8_t value_len = data[offset + 1u];

        if ((value_len == 0u) || (offset + 2u + value_len > data_len)) {
            break;
        }

        gateway_sle_adv_parse_field(info, type, &data[offset + 2u], value_len);
        offset += 2u + value_len;
    }
}

static void gateway_sle_adv_parse_len_type(const uint8_t *data,
                                           uint8_t data_len,
                                           gateway_sle_adv_info_t *info)
{
    uint16_t offset = 0;

    while (offset < data_len) {
        uint8_t ad_len = data[offset];
        uint8_t type;
        uint8_t value_len;

        if ((ad_len < 1u) || (offset + 1u + ad_len > data_len)) {
            break;
        }

        type = data[offset + 1u];
        value_len = (uint8_t)(ad_len - 1u);
        gateway_sle_adv_parse_field(info, type, &data[offset + 2u], value_len);
        offset += 1u + ad_len;
    }
}

static void gateway_sle_adv_parse(const uint8_t *data,
                                  uint8_t data_len,
                                  gateway_sle_adv_info_t *info)
{
    if ((data == NULL) || (info == NULL) || (data_len == 0u)) {
        return;
    }

    gateway_sle_adv_parse_type_len(data, data_len, info);
    gateway_sle_adv_parse_len_type(data, data_len, info);
}

static void gateway_sle_log_raw_adv(const uint8_t *data, uint8_t data_len)
{
    uint8_t log_len = data_len;

    if (data == NULL) {
        return;
    }

    if (log_len > GATEWAY_SLE_ADV_LOG_RAW_MAX) {
        log_len = GATEWAY_SLE_ADV_LOG_RAW_MAX;
    }

    GATEWAY_LOG("[GATEWAY] seek raw:");
    for (uint8_t i = 0; i < log_len; i++) {
        GATEWAY_LOG(" %02x", (unsigned int)data[i]);
    }
    if (log_len < data_len) {
        GATEWAY_LOG(" ...");
    }
    GATEWAY_LOG("\r\n");
}

static void gateway_sle_log_seek_result(const sle_seek_result_info_t *seek_result_data,
                                        const gateway_sle_adv_info_t *info)
{
    if ((seek_result_data == NULL) || (info == NULL)) {
        return;
    }

    GATEWAY_LOG("[GATEWAY] seek device addr=%02x:%02x:%02x:%02x:%02x:%02x "
                "rssi=%d len=%u name=%s uuid_0x%04x=%u\r\n",
                (unsigned int)seek_result_data->addr.addr[0],
                (unsigned int)seek_result_data->addr.addr[1],
                (unsigned int)seek_result_data->addr.addr[2],
                (unsigned int)seek_result_data->addr.addr[3],
                (unsigned int)seek_result_data->addr.addr[4],
                (unsigned int)seek_result_data->addr.addr[5],
                (int8_t)seek_result_data->rssi,
                (unsigned int)seek_result_data->data_length,
                info->has_name ? info->name : "<none>",
                (unsigned int)GATEWAY_SLE_SERVICE_UUID,
                info->has_target_uuid ? 1u : 0u);
    gateway_sle_log_raw_adv(seek_result_data->data, seek_result_data->data_length);
}

static bool gateway_sle_uuid_is_u16(const sle_uuid_t *uuid, uint16_t uuid16)
{
    uint16_t head_le;
    uint16_t head_be;
    uint16_t tail_le;
    uint16_t tail_be;

    if (uuid == NULL) {
        return false;
    }

    head_le = (uint16_t)uuid->uuid[0] | ((uint16_t)uuid->uuid[1] << 8);
    head_be = ((uint16_t)uuid->uuid[0] << 8) | (uint16_t)uuid->uuid[1];
    tail_le = (uint16_t)uuid->uuid[14] | ((uint16_t)uuid->uuid[15] << 8);
    tail_be = ((uint16_t)uuid->uuid[14] << 8) | (uint16_t)uuid->uuid[15];

    if ((uuid->len == 2u) || (uuid->len == 16u)) {
        return (head_le == uuid16) || (head_be == uuid16) ||
               (tail_le == uuid16) || (tail_be == uuid16);
    }

    return false;
}

static uint16_t gateway_sle_uuid_log_value(const sle_uuid_t *uuid)
{
    uint16_t head_le;
    uint16_t tail_le;

    if (uuid == NULL) {
        return 0u;
    }

    tail_le = (uint16_t)uuid->uuid[14] | ((uint16_t)uuid->uuid[15] << 8);
    if (tail_le != 0u) {
        return tail_le;
    }

    head_le = (uint16_t)uuid->uuid[0] | ((uint16_t)uuid->uuid[1] << 8);
    return head_le;
}

static void gateway_sle_log_uuid_detail(const char *label, const sle_uuid_t *uuid)
{
    uint16_t head_le;
    uint16_t head_be;
    uint16_t tail_le;
    uint16_t tail_be;

    if ((label == NULL) || (uuid == NULL)) {
        return;
    }

    head_le = (uint16_t)uuid->uuid[0] | ((uint16_t)uuid->uuid[1] << 8);
    head_be = ((uint16_t)uuid->uuid[0] << 8) | (uint16_t)uuid->uuid[1];
    tail_le = (uint16_t)uuid->uuid[14] | ((uint16_t)uuid->uuid[15] << 8);
    tail_be = ((uint16_t)uuid->uuid[14] << 8) | (uint16_t)uuid->uuid[15];
    GATEWAY_LOG("[GATEWAY] %s uuid detail len=%u head_le=0x%04x head_be=0x%04x tail_le=0x%04x tail_be=0x%04x\r\n",
                label,
                (unsigned int)uuid->len,
                (unsigned int)head_le,
                (unsigned int)head_be,
                (unsigned int)tail_le,
                (unsigned int)tail_be);
}

static void gateway_sle_start_scan(void)
{
    sle_seek_param_t param = {0};
    errcode_t ret;

    g_gateway_target_found = false;
    g_gateway_service_found = false;
    g_gateway_property_found = false;
    g_gateway_exchange_started = false;
    g_gateway_property_find_started = false;
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = GATEWAY_SLE_SEEK_INTERVAL;
    param.seek_window[0] = GATEWAY_SLE_SEEK_WINDOW;

    ret = sle_set_seek_param(&param);
    GATEWAY_LOG("[GATEWAY] set seek param ret=0x%x\r\n", (unsigned int)ret);
    ret = sle_start_seek();
    GATEWAY_LOG("[GATEWAY] start seek ret=0x%x\r\n", (unsigned int)ret);
    GATEWAY_LOG("[GATEWAY] scan start\r\n");
}

static void gateway_sle_exchange_info(uint16_t conn_id)
{
    ssap_exchange_info_t info = {0};

    info.mtu_size = GATEWAY_SLE_MTU_SIZE;
    info.version = 1;
    (void)ssapc_exchange_info_req(0, conn_id, &info);
}

static void gateway_sle_subscribe_notify(uint8_t client_id, uint16_t conn_id, uint16_t handle)
{
    static uint8_t enable_notify[] = {0x01u, 0x00u};
    ssapc_write_param_t param = {0};
    errcode_t ret;

    param.handle = handle;
    param.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    param.data_len = sizeof(enable_notify);
    param.data = enable_notify;

    ret = ssapc_write_req(client_id, conn_id, &param);
    GATEWAY_LOG("[GATEWAY] notify subscribe req handle=0x%x ret=0x%x\r\n",
                (unsigned int)handle,
                (unsigned int)ret);
}

static uint8_t gateway_sle_safe_cmd_checksum(const uint8_t *data)
{
    uint16_t sum = 0;

    for (uint8_t i = 0; i < GATEWAY_SAFE_LIGHT_CMD_INDEX_CHECKSUM; i++) {
        sum += data[i];
    }

    return (uint8_t)(sum & 0xFFu);
}

static void gateway_sle_try_exchange_info(uint16_t conn_id)
{
    if (g_gateway_exchange_started) {
        return;
    }

    g_gateway_exchange_started = true;
    gateway_sle_exchange_info(conn_id);
}

static errcode_t gateway_sle_find_structure(uint8_t client_id,
                                            uint16_t conn_id,
                                            uint8_t type,
                                            uint16_t start_hdl,
                                            uint16_t end_hdl)
{
    ssapc_find_structure_param_t find_param = {0};
    errcode_t ret;

    find_param.type = type;
    find_param.start_hdl = start_hdl;
    find_param.end_hdl = end_hdl;
    ret = ssapc_find_structure(client_id, conn_id, &find_param);
    GATEWAY_LOG("[GATEWAY] find req type=%u start=0x%x end=0x%x ret=0x%x\r\n",
                (unsigned int)type,
                (unsigned int)start_hdl,
                (unsigned int)end_hdl,
                (unsigned int)ret);
    return ret;
}

static void gateway_sle_start_property_discovery(uint8_t client_id, uint16_t conn_id)
{
    uint16_t start_hdl = g_gateway_service_found ? g_gateway_service_start_handle : 1u;
    uint16_t end_hdl = g_gateway_service_found ? g_gateway_service_end_handle : 0xFFFFu;

    if (g_gateway_property_find_started) {
        return;
    }

    g_gateway_property_find_started = true;
    (void)gateway_sle_find_structure(client_id, conn_id, SSAP_FIND_TYPE_PROPERTY, start_hdl, end_hdl);
}

static void gateway_sle_enable_cbk(errcode_t status)
{
    GATEWAY_LOG("[GATEWAY] sle client init status=0x%x\r\n", (unsigned int)status);
    if (status == ERRCODE_SUCC) {
        gateway_sle_start_scan();
    }
}

static void gateway_sle_seek_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        GATEWAY_LOG("[GATEWAY] scan enable failed status=0x%x\r\n", (unsigned int)status);
    }
}

static void gateway_sle_seek_result_cbk(sle_seek_result_info_t *seek_result_data)
{
    gateway_sle_adv_info_t adv_info = {0};
    bool name_match;

    if ((seek_result_data == NULL) || g_gateway_target_found) {
        return;
    }

    gateway_sle_adv_parse(seek_result_data->data, seek_result_data->data_length, &adv_info);
    gateway_sle_log_seek_result(seek_result_data, &adv_info);

    name_match = gateway_sle_adv_contains_name(seek_result_data->data,
                                               seek_result_data->data_length,
                                               GATEWAY_RADAR_NODE_NAME);
    if (!adv_info.has_target_uuid && !name_match) {
        return;
    }

    if (memcpy_s(&g_gateway_radar_addr,
                 sizeof(g_gateway_radar_addr),
                 &seek_result_data->addr,
                 sizeof(seek_result_data->addr)) != EOK) {
        GATEWAY_LOG("[GATEWAY] radar addr copy failed\r\n");
        return;
    }

    g_gateway_target_found = true;
    if (name_match) {
        GATEWAY_LOG("[GATEWAY] found RADAR_NODE_01 uuid_match=%u name_match=1\r\n",
                    adv_info.has_target_uuid ? 1u : 0u);
    } else {
        GATEWAY_LOG("[GATEWAY] found radar service uuid=0x%04x name_match=0\r\n",
                    (unsigned int)GATEWAY_SLE_SERVICE_UUID);
    }
    (void)sle_stop_seek();
}

static void gateway_sle_seek_disable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        GATEWAY_LOG("[GATEWAY] scan stop failed status=0x%x\r\n", (unsigned int)status);
        gateway_sle_start_scan();
        return;
    }

    if (!g_gateway_target_found) {
        gateway_sle_start_scan();
        return;
    }

    (void)sle_remove_paired_remote_device(&g_gateway_radar_addr);
    (void)sle_connect_remote_device(&g_gateway_radar_addr);
}

static void gateway_sle_connect_state_changed_cbk(uint16_t conn_id,
                                                  const sle_addr_t *addr,
                                                  sle_acb_state_t conn_state,
                                                  sle_pair_state_t pair_state,
                                                  sle_disc_reason_t disc_reason)
{
    (void)addr;
    GATEWAY_LOG("[GATEWAY] conn state=%u pair=%u reason=0x%x\r\n",
                (unsigned int)conn_state,
                (unsigned int)pair_state,
                (unsigned int)disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_gateway_conn_id = conn_id;
        g_gateway_connected = true;
        GATEWAY_LOG("[GATEWAY] connected\r\n");
#if GATEWAY_SLE_ENABLE_PAIRING
        if (pair_state == SLE_PAIR_NONE) {
            GATEWAY_LOG("[GATEWAY] pairing start\r\n");
            (void)sle_pair_remote_device(&g_gateway_radar_addr);
            return;
        }
#endif
        gateway_sle_try_exchange_info(conn_id);
        return;
    }

    if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_gateway_conn_id = 0;
        g_gateway_connected = false;
        g_gateway_notify_handle = 0;
        g_gateway_service_start_handle = 0;
        g_gateway_service_end_handle = 0;
        g_gateway_service_found = false;
        g_gateway_property_found = false;
        g_gateway_exchange_started = false;
        g_gateway_property_find_started = false;
        (void)sle_remove_paired_remote_device(&g_gateway_radar_addr);
        gateway_sle_start_scan();
    }
}

static void gateway_sle_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    (void)addr;
    GATEWAY_LOG("[GATEWAY] pair complete conn=%u status=0x%x\r\n",
                (unsigned int)conn_id,
                (unsigned int)status);
    if (status == ERRCODE_SUCC) {
        gateway_sle_try_exchange_info(conn_id);
    }
}

static void gateway_sle_exchange_info_cbk(uint8_t client_id,
                                          uint16_t conn_id,
                                          ssap_exchange_info_t *param,
                                          errcode_t status)
{
    GATEWAY_LOG("[GATEWAY] exchange info client=%u conn=%u mtu=%u status=0x%x\r\n",
                (unsigned int)client_id,
                (unsigned int)conn_id,
                (param == NULL) ? 0u : (unsigned int)param->mtu_size,
                (unsigned int)status);
    if (status != ERRCODE_SUCC) {
        return;
    }

    g_gateway_service_found = false;
    g_gateway_property_found = false;
    g_gateway_property_find_started = false;
    (void)gateway_sle_find_structure(client_id, conn_id, SSAP_FIND_TYPE_PRIMARY_SERVICE, 1u, 0xFFFFu);
}

static void gateway_sle_find_structure_cbk(uint8_t client_id,
                                           uint16_t conn_id,
                                           ssapc_find_service_result_t *service,
                                           errcode_t status)
{
    (void)client_id;
    (void)conn_id;

    if ((status == ERRCODE_SUCC) && (service != NULL)) {
        gateway_sle_log_uuid_detail("service", &service->uuid);
        if (gateway_sle_uuid_is_u16(&service->uuid, GATEWAY_SLE_SERVICE_UUID)) {
            g_gateway_service_start_handle = service->start_hdl;
            g_gateway_service_end_handle = service->end_hdl;
            g_gateway_service_found = true;
            GATEWAY_LOG("[GATEWAY] service found handle=0x%x uuid=0x%04x\r\n",
                        (unsigned int)service->start_hdl,
                        (unsigned int)gateway_sle_uuid_log_value(&service->uuid));
        } else {
            GATEWAY_LOG("[GATEWAY] service found handle=0x%x uuid=0x%04x uuid_len=%u\r\n",
                        (unsigned int)service->start_hdl,
                        (unsigned int)gateway_sle_uuid_log_value(&service->uuid),
                        (unsigned int)service->uuid.len);
        }
    }
}

static void gateway_sle_find_property_cbk(uint8_t client_id,
                                          uint16_t conn_id,
                                          ssapc_find_property_result_t *property,
                                          errcode_t status)
{
    bool uuid_match;
    bool notify_supported;

    (void)client_id;
    (void)conn_id;

    if ((status != ERRCODE_SUCC) || (property == NULL)) {
        return;
    }

    gateway_sle_log_uuid_detail("property", &property->uuid);
    uuid_match = gateway_sle_uuid_is_u16(&property->uuid, GATEWAY_SLE_NOTIFY_PROPERTY_UUID);
    notify_supported = ((property->operate_indication & SSAP_OPERATE_INDICATION_BIT_NOTIFY) != 0u);
    GATEWAY_LOG("[GATEWAY] property handle=0x%x uuid=0x%04x op=0x%x desc=%u notify=%u uuid_match=%u\r\n",
                (unsigned int)property->handle,
                (unsigned int)gateway_sle_uuid_log_value(&property->uuid),
                (unsigned int)property->operate_indication,
                (unsigned int)property->descriptors_count,
                notify_supported ? 1u : 0u,
                uuid_match ? 1u : 0u);
    gateway_sle_log_property_descriptors(property);
    if (g_gateway_service_found &&
        ((property->handle < g_gateway_service_start_handle) ||
         (property->handle > g_gateway_service_end_handle))) {
        return;
    }

    if (!notify_supported || !uuid_match) {
        return;
    }

    if (g_gateway_property_found) {
        return;
    }

    g_gateway_notify_handle = property->handle;
    g_gateway_property_found = true;
    GATEWAY_LOG("[GATEWAY] characteristic found handle=0x%x uuid_match=%u\r\n",
                (unsigned int)g_gateway_notify_handle,
                uuid_match ? 1u : 0u);
    if (((property->operate_indication &
          SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE) != 0u) ||
        gateway_sle_property_has_descriptor(property, SSAP_DESCRIPTOR_CLIENT_CONFIGURATION)) {
        gateway_sle_subscribe_notify(client_id, conn_id, g_gateway_notify_handle);
    } else {
        GATEWAY_LOG("[GATEWAY] notify has no client config descriptor, waiting direct notify\r\n");
    }
}

static void gateway_sle_find_structure_cmp_cbk(uint8_t client_id,
                                               uint16_t conn_id,
                                               ssapc_find_structure_result_t *structure_result,
                                               errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)structure_result;

    if (status != ERRCODE_SUCC) {
        GATEWAY_LOG("[GATEWAY] service discovery failed status=0x%x\r\n", (unsigned int)status);
        return;
    }
    if ((structure_result != NULL) && (structure_result->type == SSAP_FIND_TYPE_PRIMARY_SERVICE)) {
        if (!g_gateway_service_found) {
            GATEWAY_LOG("[GATEWAY] target service 0x%04x not found, fallback property scan\r\n",
                        (unsigned int)GATEWAY_SLE_SERVICE_UUID);
        }
        gateway_sle_start_property_discovery(client_id, conn_id);
        return;
    }
    if ((structure_result != NULL) && (structure_result->type == SSAP_FIND_TYPE_PROPERTY) &&
        !g_gateway_property_found) {
        GATEWAY_LOG("[GATEWAY] notify characteristic not found\r\n");
    }
}

static void gateway_sle_write_cfm_cbk(uint8_t client_id,
                                      uint16_t conn_id,
                                      ssapc_write_result_t *write_result,
                                      errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if ((write_result != NULL) && (write_result->type == SSAP_DESCRIPTOR_CLIENT_CONFIGURATION)) {
        GATEWAY_LOG("[GATEWAY] notify subscribed status=0x%x\r\n", (unsigned int)status);
        return;
    }

    GATEWAY_LOG("[GATEWAY] write cfm handle=0x%x type=0x%x status=0x%x\r\n",
                (write_result == NULL) ? 0u : (unsigned int)write_result->handle,
                (write_result == NULL) ? 0u : (unsigned int)write_result->type,
                (unsigned int)status);
}

static void gateway_sle_notification_cbk(uint8_t client_id,
                                         uint16_t conn_id,
                                         ssapc_handle_value_t *data,
                                         errcode_t status)
{
    (void)client_id;
    (void)conn_id;

    if ((status != ERRCODE_SUCC) || (data == NULL) || (data->data == NULL) || (data->data_len == 0u)) {
        return;
    }

    GATEWAY_LOG("[GATEWAY] rx len=%u\r\n", (unsigned int)data->data_len);
    gateway_on_sle_data_received(data->data, data->data_len);
}

static void gateway_sle_indication_cbk(uint8_t client_id,
                                       uint16_t conn_id,
                                       ssapc_handle_value_t *data,
                                       errcode_t status)
{
    gateway_sle_notification_cbk(client_id, conn_id, data, status);
}

static void gateway_sle_register_seek_cbks(void)
{
    g_gateway_seek_cbks.sle_enable_cb = gateway_sle_enable_cbk;
    g_gateway_seek_cbks.seek_enable_cb = gateway_sle_seek_enable_cbk;
    g_gateway_seek_cbks.seek_result_cb = gateway_sle_seek_result_cbk;
    g_gateway_seek_cbks.seek_disable_cb = gateway_sle_seek_disable_cbk;
    (void)sle_announce_seek_register_callbacks(&g_gateway_seek_cbks);
}

static void gateway_sle_register_connection_cbks(void)
{
    g_gateway_connect_cbks.connect_state_changed_cb = gateway_sle_connect_state_changed_cbk;
    g_gateway_connect_cbks.pair_complete_cb = gateway_sle_pair_complete_cbk;
    (void)sle_connection_register_callbacks(&g_gateway_connect_cbks);
}

static void gateway_sle_register_ssapc_cbks(void)
{
    g_gateway_ssapc_cbks.exchange_info_cb = gateway_sle_exchange_info_cbk;
    g_gateway_ssapc_cbks.find_structure_cb = gateway_sle_find_structure_cbk;
    g_gateway_ssapc_cbks.ssapc_find_property_cbk = gateway_sle_find_property_cbk;
    g_gateway_ssapc_cbks.find_structure_cmp_cb = gateway_sle_find_structure_cmp_cbk;
    g_gateway_ssapc_cbks.write_cfm_cb = gateway_sle_write_cfm_cbk;
    g_gateway_ssapc_cbks.notification_cb = gateway_sle_notification_cbk;
    g_gateway_ssapc_cbks.indication_cb = gateway_sle_indication_cbk;
    (void)ssapc_register_callbacks(&g_gateway_ssapc_cbks);
}

int gateway_sle_rx_init(void)
{
    GATEWAY_LOG("[GATEWAY] sle client init\r\n");
    gateway_sle_register_seek_cbks();
    gateway_sle_register_connection_cbks();
    gateway_sle_register_ssapc_cbks();

    if (enable_sle() != ERRCODE_SUCC) {
        GATEWAY_LOG("[GATEWAY] sle enable failed\r\n");
        return -1;
    }
    return 0;
}

void gateway_sle_register_packet_callback(gateway_radar_packet_callback_t callback)
{
    g_gateway_packet_callback = callback;
}

void gateway_on_sle_data_received(const uint8_t *data, size_t len)
{
    size_t copy_len;

    if ((data == NULL) || (len == 0u)) {
        return;
    }

    if (len > RADAR_LIGHT_PACKET_WIRE_SIZE) {
        data = &data[len - RADAR_LIGHT_PACKET_WIRE_SIZE];
        len = RADAR_LIGHT_PACKET_WIRE_SIZE;
    }

    if ((g_gateway_stream_len + len) > sizeof(g_gateway_stream_buffer)) {
        copy_len = sizeof(g_gateway_stream_buffer) - g_gateway_stream_len;
        if (copy_len > 0u) {
            (void)memcpy_s(&g_gateway_stream_buffer[g_gateway_stream_len],
                           copy_len,
                           data,
                           copy_len);
            g_gateway_stream_len += copy_len;
        }
        GATEWAY_LOG("[GATEWAY] sle stream overflow, reset\r\n");
        g_gateway_stream_len = 0u;
        return;
    }

    (void)memcpy_s(&g_gateway_stream_buffer[g_gateway_stream_len],
                   sizeof(g_gateway_stream_buffer) - g_gateway_stream_len,
                   data,
                   len);
    g_gateway_stream_len += len;
    gateway_sle_try_decode_stream();
}

int gateway_sle_send_safe_lighting_cmd(uint8_t cmd, const char *reason)
{
    ssapc_write_param_t param = {0};
    errcode_t ret;

    if ((cmd != GATEWAY_SAFE_LIGHT_CMD_ON) && (cmd != GATEWAY_SAFE_LIGHT_CMD_OFF)) {
        GATEWAY_LOG("[GATEWAY_LIGHT] reject cmd=%u reason=%s\r\n",
                    (unsigned int)cmd,
                    (reason == NULL) ? "none" : reason);
        return -1;
    }

    if (!g_gateway_connected || !g_gateway_property_found || (g_gateway_notify_handle == 0u)) {
        GATEWAY_LOG("[GATEWAY_LIGHT] not ready cmd=%u conn=0x%x handle=0x%x found=%u reason=%s\r\n",
                    (unsigned int)cmd,
                    (unsigned int)g_gateway_conn_id,
                    (unsigned int)g_gateway_notify_handle,
                    g_gateway_property_found ? 1u : 0u,
                    (reason == NULL) ? "none" : reason);
        return -2;
    }

    g_gateway_safe_light_payload[0] = GATEWAY_SAFE_LIGHT_CMD_MAGIC0;
    g_gateway_safe_light_payload[1] = GATEWAY_SAFE_LIGHT_CMD_MAGIC1;
    g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_INDEX_VERSION] = GATEWAY_SAFE_LIGHT_CMD_VERSION;
    g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_INDEX_CMD] = cmd;
    g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_INDEX_ARG] =
        (cmd == GATEWAY_SAFE_LIGHT_CMD_ON) ? GATEWAY_SAFE_LIGHT_CMD_SAFE_PERCENT : 0u;
    g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_INDEX_CHECKSUM] =
        gateway_sle_safe_cmd_checksum(g_gateway_safe_light_payload);

    param.handle = g_gateway_notify_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = sizeof(g_gateway_safe_light_payload);
    param.data = g_gateway_safe_light_payload;
    ret = ssapc_write_req(0, g_gateway_conn_id, &param);
    GATEWAY_LOG("[GATEWAY_LIGHT] send cmd=%u arg=%u reason=%s ret=0x%x\r\n",
                (unsigned int)cmd,
                (unsigned int)g_gateway_safe_light_payload[GATEWAY_SAFE_LIGHT_CMD_INDEX_ARG],
                (reason == NULL) ? "none" : reason,
                (unsigned int)ret);
    return (ret == ERRCODE_SUCC) ? 0 : -3;
}
