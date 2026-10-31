#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S31 && CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include "esp_a2dp_api.h"
#include "bt_app_sink.h"
esp_err_t bt_sink_codec_init(bt_data_cb_t output, bt_data_cb_t output32);
esp_err_t bt_sink_codec_register_endpoints(void);
unsigned bt_sink_codec_endpoint_count(void);
int bt_sink_codec_configure(const esp_a2d_mcc_t *mcc);
bool bt_sink_codec_start(void);
void bt_sink_codec_stop(bool forget_config);
void bt_sink_codec_receive(esp_a2d_conn_hdl_t connection, esp_a2d_audio_buff_t *buffer);
#endif
