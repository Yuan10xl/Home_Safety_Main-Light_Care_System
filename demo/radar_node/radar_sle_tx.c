#include "radar_sle_tx.h"

#include "common_def.h"
#include "errcode.h"
#include "radar_lamp_switch.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"

#ifndef RADAR_NODE_LOG
#define RADAR_NODE_LOG(...) osal_printk(__VA_ARGS__)
#endif

#define RADAR_SLE_ADV_HANDLE                  1
#define RADAR_SLE_NODE_NAME                   "RADAR_NODE_01"
#define RADAR_SLE_NODE_NAME_LEN               (sizeof(RADAR_SLE_NODE_NAME) - 1)
#define RADAR_SLE_SERVICE_UUID                0xABCD
#define RADAR_SLE_CHAR_UUID                   0xBCDE
#define RADAR_SLE_APP_UUID_LEN                2
#define RADAR_SLE_UUID16_LEN                  2
#define RADAR_SLE_MTU_SIZE                    256
#define RADAR_SLE_CONN_INTERVAL               0xA0
#define RADAR_SLE_CONN_SUPERVISION_TIMEOUT    0x1F4
#define RADAR_SLE_CONN_MAX_LATENCY            0
#define RADAR_SLE_ADV_TX_POWER                20
#define RADAR_SLE_ADV_INTERVAL_MIN            0xC8
#define RADAR_SLE_ADV_INTERVAL_MAX            0xC8
#define RADAR_SLE_ADV_CHANNEL_MAP             0x07

#define SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL                  0x01
#define SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_16BIT_SERVICE_UUIDS 0x05
#define SLE_ADV_DATA_TYPE_TX_POWER_LEVEL                   0x0C
#define SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME              0x0B

#define RADAR_SAFE_LIGHT_CMD_MAGIC0          0xA5
#define RADAR_SAFE_LIGHT_CMD_MAGIC1          0x5A
#define RADAR_SAFE_LIGHT_CMD_VERSION         1
#define RADAR_SAFE_LIGHT_CMD_SIZE            6
#define RADAR_SAFE_LIGHT_CMD_INDEX_VERSION   2
#define RADAR_SAFE_LIGHT_CMD_INDEX_CMD       3
#define RADAR_SAFE_LIGHT_CMD_INDEX_ARG       4
#define RADAR_SAFE_LIGHT_CMD_INDEX_CHECKSUM  5

static radar_sle_send_bytes_fn g_radar_sle_send_bytes;
static uint8_t g_radar_sle_init_started;
static uint8_t g_radar_sle_connected;
static uint8_t g_radar_sle_notify_enabled;
static uint8_t g_radar_sle_wait_conn_logged;
static uint8_t g_radar_sle_wait_notify_logged;
static uint8_t g_server_id;
static uint16_t g_conn_id;
static uint16_t g_service_handle;
static uint16_t g_property_handle;

static uint8_t g_radar_sle_adv_data[] = {
    SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
    1,
    SLE_ANNOUNCE_LEVEL_NORMAL,
    SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_16BIT_SERVICE_UUIDS,
    RADAR_SLE_UUID16_LEN,
    (uint8_t)(RADAR_SLE_SERVICE_UUID & 0xFF),
    (uint8_t)((RADAR_SLE_SERVICE_UUID >> 8) & 0xFF),
};

static uint8_t g_radar_sle_scan_rsp_data[] = {
    SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
    1,
    RADAR_SLE_ADV_TX_POWER,
    SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME,
    RADAR_SLE_NODE_NAME_LEN,
    'R', 'A', 'D', 'A', 'R', '_', 'N', 'O', 'D', 'E', '_', '0', '1',
};

static uint8_t g_radar_sle_property_value[RADAR_LIGHT_PACKET_WIRE_SIZE];

static void radar_sle_uuid_set_base(sle_uuid_t *out)
{
    static const uint8_t uuid_base[SLE_UUID_LEN] = {
        0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
        0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    (void)memcpy_s(out->uuid, SLE_UUID_LEN, uuid_base, sizeof(uuid_base));
    out->len = RADAR_SLE_UUID16_LEN;
}

static void radar_sle_uuid_set_u16(uint16_t uuid16, sle_uuid_t *out)
{
    radar_sle_uuid_set_base(out);
    out->uuid[0] = (uint8_t)(uuid16 & 0xFF);
    out->uuid[1] = (uint8_t)((uuid16 >> 8) & 0xFF);
    out->uuid[14] = (uint8_t)(uuid16 & 0xFF);
    out->uuid[15] = (uint8_t)((uuid16 >> 8) & 0xFF);
}

static errcode_t radar_sle_add_service(void)
{
    errcode_t ret;
    sle_uuid_t service_uuid = {0};

    radar_sle_uuid_set_u16(RADAR_SLE_SERVICE_UUID, &service_uuid);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] add service failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    RADAR_NODE_LOG("[RADAR_SLE] service added uuid=0x%04x handle=0x%x\r\n",
                   RADAR_SLE_SERVICE_UUID,
                   (unsigned int)g_service_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t radar_sle_add_property(void)
{
    errcode_t ret;
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t desc_value[] = {0x01, 0x00};

    radar_sle_uuid_set_u16(RADAR_SLE_CHAR_UUID, &property.uuid);
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                                  SSAP_OPERATE_INDICATION_BIT_WRITE |
                                  SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    property.value = g_radar_sle_property_value;
    property.value_len = sizeof(g_radar_sle_property_value);

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] add characteristic failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                                    SSAP_OPERATE_INDICATION_BIT_WRITE |
                                    SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE;
    descriptor.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    descriptor.value = desc_value;
    descriptor.value_len = sizeof(desc_value);

    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] add descriptor failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    RADAR_NODE_LOG("[RADAR_SLE] characteristic added uuid=0x%04x handle=0x%x notify=1 ccc=1\r\n",
                   RADAR_SLE_CHAR_UUID,
                   (unsigned int)g_property_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t radar_sle_add_server(void)
{
    errcode_t ret;
    sle_uuid_t app_uuid = {0};
    uint8_t app_uuid_value[RADAR_SLE_APP_UUID_LEN] = {0x00, 0x00};

    app_uuid.len = sizeof(app_uuid_value);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, app_uuid_value, sizeof(app_uuid_value)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] register server failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    ret = radar_sle_add_service();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    ret = radar_sle_add_property();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] start service failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    RADAR_NODE_LOG("[RADAR_SLE] server ready service=0x%04x char=0x%04x\r\n",
                   RADAR_SLE_SERVICE_UUID,
                   RADAR_SLE_CHAR_UUID);
    return ERRCODE_SLE_SUCCESS;
}

static void radar_sle_set_ssap_info(void)
{
    ssap_exchange_info_t info = {0};

    info.mtu_size = RADAR_SLE_MTU_SIZE;
    info.version = 1;
    (void)ssaps_set_info(g_server_id, &info);
}

static void radar_sle_set_local_addr(void)
{
    sle_addr_t addr = {0};
    uint8_t mac[SLE_ADDR_LEN] = {0x21, 0x22, 0x33, 0x44, 0x55, 0x66};

    addr.type = 0;
    (void)memcpy_s(addr.addr, SLE_ADDR_LEN, mac, sizeof(mac));
    (void)sle_set_local_addr(&addr);
}

static errcode_t radar_sle_start_advertising(void)
{
    errcode_t ret;
    sle_announce_param_t param = {0};
    sle_announce_data_t data = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = RADAR_SLE_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = RADAR_SLE_ADV_CHANNEL_MAP;
    param.announce_interval_min = RADAR_SLE_ADV_INTERVAL_MIN;
    param.announce_interval_max = RADAR_SLE_ADV_INTERVAL_MAX;
    param.conn_interval_min = RADAR_SLE_CONN_INTERVAL;
    param.conn_interval_max = RADAR_SLE_CONN_INTERVAL;
    param.conn_max_latency = RADAR_SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = RADAR_SLE_CONN_SUPERVISION_TIMEOUT;
    param.announce_tx_power = RADAR_SLE_ADV_TX_POWER;
    param.own_addr.type = 0;
    param.own_addr.addr[0] = 0x21;
    param.own_addr.addr[1] = 0x22;
    param.own_addr.addr[2] = 0x33;
    param.own_addr.addr[3] = 0x44;
    param.own_addr.addr[4] = 0x55;
    param.own_addr.addr[5] = 0x66;

    ret = sle_set_announce_param(RADAR_SLE_ADV_HANDLE, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] set advertise param failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    data.announce_data = g_radar_sle_adv_data;
    data.announce_data_len = sizeof(g_radar_sle_adv_data);
    data.seek_rsp_data = g_radar_sle_scan_rsp_data;
    data.seek_rsp_data_len = sizeof(g_radar_sle_scan_rsp_data);

    ret = sle_set_announce_data(RADAR_SLE_ADV_HANDLE, &data);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] set advertise data failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    ret = sle_start_announce(RADAR_SLE_ADV_HANDLE);
    if (ret != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] start advertise failed ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    RADAR_NODE_LOG("[RADAR_SLE] advertising %s service=0x%04x char=0x%04x\r\n",
                   RADAR_SLE_NODE_NAME,
                   RADAR_SLE_SERVICE_UUID,
                   RADAR_SLE_CHAR_UUID);
    return ERRCODE_SLE_SUCCESS;
}

static void radar_sle_start_service_after_enable(void)
{
    if (radar_sle_add_server() != ERRCODE_SLE_SUCCESS) {
        return;
    }

    radar_sle_set_ssap_info();
    radar_sle_set_local_addr();
    (void)radar_sle_start_advertising();
}

static void radar_sle_announce_enable_cbk(uint32_t announce_id, errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] announce enable id=0x%x status=0x%x\r\n",
                   (unsigned int)announce_id,
                   (unsigned int)status);
}

static void radar_sle_announce_disable_cbk(uint32_t announce_id, errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] announce disable id=0x%x status=0x%x\r\n",
                   (unsigned int)announce_id,
                   (unsigned int)status);
}

static void radar_sle_announce_terminal_cbk(uint32_t announce_id)
{
    RADAR_NODE_LOG("[RADAR_SLE] announce terminal id=0x%x\r\n", (unsigned int)announce_id);
}

static void radar_sle_enable_cbk(errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] enable status=0x%x\r\n", (unsigned int)status);
    if (status == ERRCODE_SLE_SUCCESS) {
        radar_sle_start_service_after_enable();
    }
}

static void radar_sle_register_announce_callbacks(void)
{
    sle_announce_seek_callbacks_t cbks = {0};

    cbks.announce_enable_cb = radar_sle_announce_enable_cbk;
    cbks.announce_disable_cb = radar_sle_announce_disable_cbk;
    cbks.announce_terminal_cb = radar_sle_announce_terminal_cbk;
    cbks.sle_enable_cb = radar_sle_enable_cbk;
    (void)sle_announce_seek_register_callbacks(&cbks);
}

static void radar_sle_connect_state_changed_cbk(uint16_t conn_id,
                                                const sle_addr_t *addr,
                                                sle_acb_state_t conn_state,
                                                sle_pair_state_t pair_state,
                                                sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);

    RADAR_NODE_LOG("[RADAR_SLE] connect state conn_id=0x%x state=0x%x reason=0x%x\r\n",
                   (unsigned int)conn_id,
                   (unsigned int)conn_state,
                   (unsigned int)disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        sle_connection_param_update_t param = {0};

        g_conn_id = conn_id;
        g_radar_sle_connected = 1;
        g_radar_sle_notify_enabled = 0;
        g_radar_sle_wait_conn_logged = 0;
        g_radar_sle_wait_notify_logged = 0;
        RADAR_NODE_LOG("[RADAR_SLE] gateway connected conn_id=0x%x\r\n", (unsigned int)conn_id);

        param.conn_id = conn_id;
        param.interval_min = RADAR_SLE_CONN_INTERVAL;
        param.interval_max = RADAR_SLE_CONN_INTERVAL;
        param.max_latency = RADAR_SLE_CONN_MAX_LATENCY;
        param.supervision_timeout = RADAR_SLE_CONN_SUPERVISION_TIMEOUT;
        (void)sle_update_connect_param(&param);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_conn_id = 0;
        g_radar_sle_connected = 0;
        g_radar_sle_notify_enabled = 0;
        g_radar_sle_wait_conn_logged = 0;
        g_radar_sle_wait_notify_logged = 0;
        RADAR_NODE_LOG("[RADAR_SLE] gateway disconnected, restart advertising\r\n");
        (void)sle_start_announce(RADAR_SLE_ADV_HANDLE);
    }
}

static void radar_sle_auth_complete_cbk(uint16_t conn_id,
                                        const sle_addr_t *addr,
                                        errcode_t status,
                                        const sle_auth_info_evt_t *evt)
{
    unused(conn_id);
    unused(addr);
    unused(evt);
    RADAR_NODE_LOG("[RADAR_SLE] auth complete status=0x%x\r\n", (unsigned int)status);
}

static void radar_sle_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(addr);
    RADAR_NODE_LOG("[RADAR_SLE] pair complete conn_id=0x%x status=0x%x\r\n",
                   (unsigned int)conn_id,
                   (unsigned int)status);
}

static void radar_sle_register_connection_callbacks(void)
{
    sle_connection_callbacks_t cbks = {0};

    cbks.connect_state_changed_cb = radar_sle_connect_state_changed_cbk;
    cbks.auth_complete_cb = radar_sle_auth_complete_cbk;
    cbks.pair_complete_cb = radar_sle_pair_complete_cbk;
    (void)sle_connection_register_callbacks(&cbks);
}

static void radar_sle_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] start service cb server=0x%x handle=0x%x status=0x%x\r\n",
                   (unsigned int)server_id,
                   (unsigned int)handle,
                   (unsigned int)status);
}

static uint8_t radar_sle_safe_cmd_checksum(const uint8_t *data)
{
    uint16_t sum = 0;

    for (uint8_t i = 0; i < RADAR_SAFE_LIGHT_CMD_INDEX_CHECKSUM; i++) {
        sum += data[i];
    }

    return (uint8_t)(sum & 0xFFu);
}

static void radar_sle_handle_safe_light_command(const ssaps_req_write_cb_t *write_cb_para)
{
    const uint8_t *value;
    uint8_t cmd;
    uint8_t arg;
    uint8_t checksum;
    errcode_t ret;

    if ((write_cb_para == NULL) || (write_cb_para->value == NULL) ||
        (write_cb_para->length < RADAR_SAFE_LIGHT_CMD_SIZE)) {
        RADAR_NODE_LOG("[RADAR_SLE] safe cmd invalid len=%u\r\n",
                       (write_cb_para == NULL) ? 0u : (unsigned int)write_cb_para->length);
        return;
    }

    value = write_cb_para->value;
    if ((value[0] != RADAR_SAFE_LIGHT_CMD_MAGIC0) ||
        (value[1] != RADAR_SAFE_LIGHT_CMD_MAGIC1) ||
        (value[RADAR_SAFE_LIGHT_CMD_INDEX_VERSION] != RADAR_SAFE_LIGHT_CMD_VERSION)) {
        RADAR_NODE_LOG("[RADAR_SLE] safe cmd ignored magic=%02x %02x ver=%u\r\n",
                       (unsigned int)value[0],
                       (unsigned int)value[1],
                       (unsigned int)value[RADAR_SAFE_LIGHT_CMD_INDEX_VERSION]);
        return;
    }

    checksum = radar_sle_safe_cmd_checksum(value);
    if (checksum != value[RADAR_SAFE_LIGHT_CMD_INDEX_CHECKSUM]) {
        RADAR_NODE_LOG("[RADAR_SLE] safe cmd checksum failed got=0x%02x expect=0x%02x\r\n",
                       (unsigned int)value[RADAR_SAFE_LIGHT_CMD_INDEX_CHECKSUM],
                       (unsigned int)checksum);
        return;
    }

    cmd = value[RADAR_SAFE_LIGHT_CMD_INDEX_CMD];
    arg = value[RADAR_SAFE_LIGHT_CMD_INDEX_ARG];
    ret = radar_lamp_safe_lighting_request(cmd);
    RADAR_NODE_LOG("[RADAR_SLE] safe cmd cmd=%u arg=%u ret=%d\r\n",
                   (unsigned int)cmd,
                   (unsigned int)arg,
                   (int)ret);
}

static void radar_sle_write_request_cbk(uint8_t server_id,
                                        uint16_t conn_id,
                                        ssaps_req_write_cb_t *write_cb_para,
                                        errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] write req server=0x%x conn=0x%x status=0x%x handle=0x%x type=0x%x len=%u\r\n",
                   (unsigned int)server_id,
                   (unsigned int)conn_id,
                   (unsigned int)status,
                   (write_cb_para == NULL) ? 0u : (unsigned int)write_cb_para->handle,
                   (write_cb_para == NULL) ? 0u : (unsigned int)write_cb_para->type,
                   (write_cb_para == NULL) ? 0u : (unsigned int)write_cb_para->length);
    if ((status != ERRCODE_SLE_SUCCESS) || (write_cb_para == NULL)) {
        return;
    }

    if (write_cb_para->type == SSAP_DESCRIPTOR_CLIENT_CONFIGURATION) {
        g_radar_sle_notify_enabled = 1;
        g_radar_sle_wait_notify_logged = 0;
        RADAR_NODE_LOG("[RADAR_SLE] notify enabled\r\n");
        return;
    }

    if ((write_cb_para->handle == g_property_handle) &&
        (write_cb_para->type == SSAP_PROPERTY_TYPE_VALUE)) {
        radar_sle_handle_safe_light_command(write_cb_para);
        return;
    }

    RADAR_NODE_LOG("[RADAR_SLE] write ignored handle=0x%x type=0x%x\r\n",
                   (unsigned int)write_cb_para->handle,
                   (unsigned int)write_cb_para->type);
}

static void radar_sle_read_request_cbk(uint8_t server_id,
                                       uint16_t conn_id,
                                       ssaps_req_read_cb_t *read_cb_para,
                                       errcode_t status)
{
    unused(read_cb_para);
    RADAR_NODE_LOG("[RADAR_SLE] read req server=0x%x conn=0x%x status=0x%x\r\n",
                   (unsigned int)server_id,
                   (unsigned int)conn_id,
                   (unsigned int)status);
}

static void radar_sle_mtu_changed_cbk(uint8_t server_id,
                                      uint16_t conn_id,
                                      ssap_exchange_info_t *mtu_size,
                                      errcode_t status)
{
    RADAR_NODE_LOG("[RADAR_SLE] mtu changed server=0x%x conn=0x%x mtu=%u status=0x%x\r\n",
                   (unsigned int)server_id,
                   (unsigned int)conn_id,
                   (unsigned int)mtu_size->mtu_size,
                   (unsigned int)status);
}

static void radar_sle_register_ssaps_callbacks(void)
{
    ssaps_callbacks_t cbks = {0};

    cbks.start_service_cb = radar_sle_start_service_cbk;
    cbks.mtu_changed_cb = radar_sle_mtu_changed_cbk;
    cbks.read_request_cb = radar_sle_read_request_cbk;
    cbks.write_request_cb = radar_sle_write_request_cbk;
    (void)ssaps_register_callbacks(&cbks);
}

int radar_sle_tx_init(void)
{
    if (g_radar_sle_init_started != 0u) {
        return 0;
    }

    g_radar_sle_init_started = 1;
    RADAR_NODE_LOG("[RADAR_SLE] tx init as server name=%s service=0x%04x char=0x%04x\r\n",
                   RADAR_SLE_NODE_NAME,
                   RADAR_SLE_SERVICE_UUID,
                   RADAR_SLE_CHAR_UUID);

    radar_sle_register_announce_callbacks();
    radar_sle_register_connection_callbacks();
    radar_sle_register_ssaps_callbacks();

    if (enable_sle() != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] enable_sle failed\r\n");
        return -1;
    }

    return 0;
}

void radar_sle_tx_set_send_bytes(radar_sle_send_bytes_fn send_bytes)
{
    /*
     * Kept for test injection. Formal radar-board firmware sends by SLE
     * server notify directly in this file.
     */
    g_radar_sle_send_bytes = send_bytes;
}

int radar_sle_send_packet(const radar_light_packet_t *packet)
{
    uint8_t encoded[RADAR_LIGHT_PACKET_WIRE_SIZE];
    size_t encoded_len = 0u;
    radar_packet_status_t packet_status;
    errcode_t notify_status;
    ssaps_ntf_ind_t param = {0};

    packet_status = radar_packet_encode(packet, encoded, sizeof(encoded), &encoded_len);
    if (packet_status != RADAR_PACKET_OK) {
        RADAR_NODE_LOG("[RADAR_NODE] packet encode failed status=%s\r\n",
                       radar_packet_status_name(packet_status));
        return -1;
    }

    RADAR_NODE_LOG("[RADAR_NODE] sle send packet seq=%u len=%u\r\n",
                   (unsigned int)packet->seq,
                   (unsigned int)encoded_len);

    if (g_radar_sle_send_bytes != NULL) {
        return g_radar_sle_send_bytes(encoded, encoded_len);
    }

    if (g_radar_sle_connected == 0u) {
        if (g_radar_sle_wait_conn_logged == 0u) {
            RADAR_NODE_LOG("[RADAR_SLE] wait gateway connection\r\n");
            g_radar_sle_wait_conn_logged = 1u;
        }
        return -2;
    }

    if ((g_radar_sle_notify_enabled == 0u) && (g_radar_sle_wait_notify_logged == 0u)) {
        RADAR_NODE_LOG("[RADAR_SLE] notify subscribe not observed yet, try notify anyway\r\n");
        g_radar_sle_wait_notify_logged = 1u;
    }

    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = encoded;
    param.value_len = (uint16_t)encoded_len;

    notify_status = ssaps_notify_indicate(g_server_id, g_conn_id, &param);
    if (notify_status != ERRCODE_SLE_SUCCESS) {
        RADAR_NODE_LOG("[RADAR_SLE] notify failed ret=0x%x\r\n", (unsigned int)notify_status);
        return -3;
    }

    return 0;
}
