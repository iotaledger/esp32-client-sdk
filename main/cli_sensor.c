// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sensor.h"

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
#include "driver/temp_sensor.h"
#define ENABLE_TEMP 1
#else
#define ENABLE_TEMP 0
#endif

static const char *TAG = "TempSensor";

#if 0  // FIXME

void init_tempsensor() {
#if ENABLE_TEMP
  // Initialize touch pad peripheral, it will start a timer to run a filter
  ESP_LOGI(TAG, "Initializing Temperature sensor");
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor_get_config(&temp_sensor);
  ESP_LOGI(TAG, "default dac %d, clk_div %d", temp_sensor.dac_offset, temp_sensor.clk_div);
  temp_sensor.dac_offset = TSENS_DAC_DEFAULT;  // DEFAULT: range:-10℃ ~  80℃, error < 1℃.
  temp_sensor_set_config(temp_sensor);
#else
  ESP_LOGE(TAG, "Temperature sensor is not supported on this hardware");
#endif
}

float get_temp() {
#if ENABLE_TEMP
  float temp = 0.0;
  temp_sensor_start();
  vTaskDelay(1000 / portTICK_RATE_MS);
  temp_sensor_read_celsius(&temp);
  temp_sensor_stop();
  return temp;
#else
  ESP_LOGE(TAG, "Temperature sensor is not supported on this hardware");
  return 0.0;
#endif
}

// json buffer for simple sensor data
char sensor_json[256];
char *get_sensor_json() {
  snprintf(sensor_json, sizeof(sensor_json), "{\"Device\":\"%s\",\"Temp\":%.2f,\"timestamp\":%" PRId64 "}",
           CONFIG_IDF_TARGET, get_temp(), timestamp());
  return sensor_json;
}

/* 'sensor' command */
static struct {
  struct arg_dbl *repeat;
  struct arg_end *end;
} send_sensor_args;

int send_sensor_data() {
  indexation_t *idx = NULL;
  core_message_t *msg = NULL;
  res_send_message_t msg_res = {};
  char *data = get_sensor_json();

  if ((idx = indexation_create("ESP32 Sensor", (byte_t *)data, strlen(data))) == NULL) {
    ESP_LOGE(TAG, "create data payload failed\n");
    return -1;
  }

  if ((msg = core_message_new()) == NULL) {
    ESP_LOGE(TAG, "create message failed\n");
    indexation_free(idx);
    return -1;
  }

  msg->payload = idx;
  msg->payload_type = MSG_PAYLOAD_INDEXATION;  // indexation playload

  if (send_core_message(&wallet->endpoint, msg, &msg_res) != 0) {
    ESP_LOGE(TAG, "send message failed\n");
    goto err;
  }

  if (msg_res.is_error == true) {
    printf("Error: %s\n", msg_res.u.error->msg);
    goto err;
  }

  printf("Message ID: %s\ndata: %s\n", msg_res.u.msg_id, data);
  return 0;

err:
  core_message_free(msg);
  return -1;
}

static int fn_sensor(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&send_sensor_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, send_sensor_args.end, argv[0]);
    return -1;
  }
  uint32_t repeat = (uint32_t)send_sensor_args.repeat->dval[0];
  if (repeat == 0) {
    return send_sensor_data();
  } else {
    send_sensor_data();

    uint32_t delay_arg = (uint32_t)send_sensor_args.repeat->dval[1];
    delay_arg = delay_arg ? delay_arg : 1;
    TickType_t delay_ticks = delay_arg * CONFIG_SENSOR_DELAY_SCALE * (1000 / portTICK_PERIOD_MS);
    for (uint32_t i = 1; i < repeat; i++) {
      vTaskDelay(delay_ticks);
      send_sensor_data();
    }
  }
  return 0;
}

static void register_sensor() {
  send_sensor_args.repeat = arg_dbln(NULL, NULL, "<repeat> <delay>", 1, 2, "repeat and delay");
  send_sensor_args.end = arg_end(2);
  const esp_console_cmd_t sensor_cmd = {
      .command = "sensor",
      .help = "Sent sensor data to the Tangle",
      .hint = " <repeat> <delay>",
      .func = &fn_sensor,
      .argtable = &send_sensor_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&sensor_cmd));
}

void register_sensor_commands() {
  // sensor commands
  register_sensor();
}

#endif
