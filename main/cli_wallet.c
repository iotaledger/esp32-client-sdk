// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "argtable3/argtable3.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp32/rom/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_parser.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"

#include "cli_wallet.h"
#include "sensor.h"

#include "client/api/v1/find_message.h"
#include "client/api/v1/get_balance.h"
#include "client/api/v1/get_message_children.h"
#include "client/api/v1/get_message_metadata.h"
#include "client/api/v1/get_node_info.h"
#include "client/api/v1/get_outputs_from_address.h"
#include "client/api/v1/send_message.h"
#include "wallet/wallet.h"

#define APP_WALLET_SEED CONFIG_WALLET_SEED
#define APP_NODE_URL CONFIG_IOTA_NODE_URL
#define APP_NODE_PORT CONFIG_IOTA_NODE_PORT

static const char *TAG = "wallet";

iota_wallet_t *wallet = NULL;

#if 0
#define DIM(x) (sizeof(x) / sizeof(*(x)))
static const char *iota_unit[] = {"Pi", "Ti", "Gi", "Mi", "Ki", "i"};
static const uint64_t peta_i = 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL;

void print_iota(uint64_t value) {
  char value_str[32] = {};
  uint64_t multiplier = peta_i;
  int i = 0;
  for (i = 0; i < DIM(iota_unit); i++, multiplier /= 1000) {
    if (value < multiplier) {
      continue;
    }
    if (value % multiplier == 0) {
      sprintf(value_str, "%" PRIu64 "%s", value / multiplier, iota_unit[i]);
    } else {
      sprintf(value_str, "%.2f%s", (float)value / multiplier, iota_unit[i]);
    }
    break;
  }
  printf("%s", value_str);
}
#endif

// json buffer for simple sensor data
char sensor_json[256];
char *get_sensor_json() {
  snprintf(sensor_json, sizeof(sensor_json), "{\"Device\":\"%s\",\"Temp\":%.2f,\"timestamp\":%" PRId64 "}",
           CONFIG_IDF_TARGET, get_temp(), timestamp());
  return sensor_json;
}

// 0 on success
static int endpoint_validation(iota_wallet_t *w) {
  // URL parsing
  struct http_parser_url u;
  char const *const url = APP_NODE_URL;
  http_parser_url_init(&u);
  int parse_ret = http_parser_parse_url(url, strlen(url), 0, &u);
  if (parse_ret != 0) {
    ESP_LOGE(TAG, "invalid URL of the endpoint\n");
    return -1;
  }

  // get hostname
  if (u.field_set & (1 << UF_HOST)) {
    if (sizeof(w->endpoint.host) > u.field_data[UF_HOST].len) {
      strncpy(w->endpoint.host, url + u.field_data[UF_HOST].off, u.field_data[UF_HOST].len);
      w->endpoint.host[u.field_data[UF_HOST].len] = '\0';
    } else {
      ESP_LOGE(TAG, "hostname is too long\n");
      return -1;
    }
  }

  // get port number
  if (u.field_set & (1 << UF_PORT)) {
    w->endpoint.port = u.port;
  } else {
    w->endpoint.port = APP_NODE_PORT;
  }

  // TLS?
  if (strncmp(url, "https", strlen("https")) == 0) {
    w->endpoint.use_tls = true;
  } else {
    w->endpoint.use_tls = false;
  }

  return 0;
}

/* 'version' command */
static int fn_get_version(int argc, char **argv) {
  esp_chip_info_t info;
  esp_chip_info(&info);
  printf("IDF Version:%s\r\n", esp_get_idf_version());
  printf("Chip info:\r\n");
  printf("\tmodel:%s\r\n", CONFIG_IDF_TARGET);
  printf("\tcores:%d\r\n", info.cores);
  printf("\tfeature:%s%s%s%s%d%s\r\n", info.features & CHIP_FEATURE_WIFI_BGN ? ", 802.11bgn" : "",
         info.features & CHIP_FEATURE_BLE ? ", BLE" : "", info.features & CHIP_FEATURE_BT ? ", BT" : "",
         info.features & CHIP_FEATURE_EMB_FLASH ? ", Embedded-Flash:" : ", External-Flash:",
         spi_flash_get_chip_size() / (1024 * 1024), " MB");
  printf("\trevision number:%d\r\n", info.revision);
  printf("Wallet version: v%s\n", APP_WALLET_VERSION);
  return 0;
}

static void register_version() {
  const esp_console_cmd_t cmd = {
      .command = "version",
      .help = "Show the esp32 and wallet versions",
      .hint = NULL,
      .func = &fn_get_version,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* 'restart' command */
static int fn_restart(int argc, char **argv) {
  ESP_LOGI(TAG, "Restarting");
  esp_restart();
}

static void register_restart() {
  const esp_console_cmd_t cmd = {
      .command = "restart",
      .help = "System reboot",
      .hint = NULL,
      .func = &fn_restart,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* 'free' command */
static int fn_free_mem(int argc, char **argv) {
  printf("%d\n", esp_get_free_heap_size());
  return 0;
}

static void register_free() {
  const esp_console_cmd_t cmd = {
      .command = "free",
      .help = "Get the size of available heap.",
      .hint = NULL,
      .func = &fn_free_mem,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* 'heap' command */
static int fn_heap_size(int argc, char **argv) {
  printf("heap info (SPI RAM): \n");
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
  printf("\nheap info (DEFAULT): \n");
  heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
  return 0;
}

static void register_heap() {
  const esp_console_cmd_t heap_cmd = {
      .command = "heap",
      .help = "Get heap memory info",
      .hint = NULL,
      .func = &fn_heap_size,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));
}

/* 'stack' command */
static int fn_stack_info(int argc, char **argv) {
  printf("%u tasks are running on the system\n", uxTaskGetNumberOfTasks());
  printf("Main stack size: %d, remaining %d bytes\n", CONFIG_MAIN_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(NULL));
  return 0;
}

static void register_stack_info() {
  const esp_console_cmd_t stack_info_cmd = {
      .command = "stack",
      .help = "Get system stack info",
      .hint = NULL,
      .func = &fn_stack_info,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&stack_info_cmd));
}

/* 'address' command */
static struct {
  struct arg_dbl *idx_start;
  struct arg_dbl *idx_end;
  struct arg_end *end;
} get_addr_args;

static int fn_get_address(int argc, char **argv) {
  byte_t addr_wit_version[IOTA_ADDRESS_BYTES] = {};
  char tmp_bech32_addr[100] = {};
  int nerrors = arg_parse(argc, argv, (void **)&get_addr_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, get_addr_args.end, argv[0]);
    return -1;
  }
  uint32_t start = (uint32_t)get_addr_args.idx_start->dval[0];
  uint32_t end = (uint32_t)get_addr_args.idx_end->dval[0];
  if (end < start) {
    ESP_LOGI(TAG, "invalid address range\n");
    return -1;
  }

  while (end >= start) {
    addr_wit_version[0] = ADDRESS_VER_ED25519;
    wallet_address_by_index(wallet, start, addr_wit_version + 1);
    address_2_bech32(addr_wit_version, wallet->bech32HRP, tmp_bech32_addr);
    printf("Addr[%" PRIu32 "]\n", start);
    // print ed25519 address without version filed.
    printf("\t");
    dump_hex_str(addr_wit_version + 1, ED25519_ADDRESS_BYTES);
    // print out
    printf("\t%s\n", tmp_bech32_addr);
    start++;
  }
  return 0;
}

static void register_get_address() {
  get_addr_args.idx_start = arg_dbl1(NULL, NULL, "<start>", "start index");
  get_addr_args.idx_end = arg_dbl1(NULL, NULL, "<end>", "end index");
  get_addr_args.end = arg_end(2);
  const esp_console_cmd_t get_address_cmd = {
      .command = "address",
      .help = "Get the address from an index range",
      .hint = " <start> <end>",
      .func = &fn_get_address,
      .argtable = &get_addr_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&get_address_cmd));
}

/* 'balance' command */
static struct {
  struct arg_dbl *idx_start;
  struct arg_dbl *idx_end;
  struct arg_end *end;
} get_balance_args;

static int fn_get_balance(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&get_balance_args);
  uint64_t balance = 0;
  uint32_t addr_start = 0;
  uint32_t addr_end = 0;
  if (nerrors != 0) {
    arg_print_errors(stderr, get_balance_args.end, argv[0]);
    return -1;
  }

  addr_start = get_balance_args.idx_start->dval[0];
  addr_end = get_balance_args.idx_end->dval[0];

  if (addr_end < addr_start) {
    ESP_LOGI(TAG, "invalid address range\n");
    return -1;
  }

  while (addr_end >= addr_start) {
    if (wallet_balance_by_index(wallet, addr_start, &balance) != 0) {
      ESP_LOGI(TAG, "get balance failed on %zu\n", addr_start);
      return -1;
    }
    printf("balance on address [%" PRIu32 "]: %" PRIu64 "i\n", addr_start, balance);
    addr_start++;
  }

  return 0;
}

static void register_get_balance() {
  get_balance_args.idx_start = arg_dbl1(NULL, NULL, "<start>", "start index");
  get_balance_args.idx_end = arg_dbl1(NULL, NULL, "<end>", "end index");
  get_balance_args.end = arg_end(2);
  const esp_console_cmd_t get_balance_cmd = {
      .command = "balance",
      .help = "Get the balance from a range of address index",
      .hint = " <start> <end>",
      .func = &fn_get_balance,
      .argtable = &get_balance_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&get_balance_cmd));
}

/* 'send' command */
static struct {
  struct arg_dbl *sender;
  struct arg_str *receiver;
  struct arg_dbl *balance;
  struct arg_end *end;
} send_msg_args;

static int fn_send_msg(int argc, char **argv) {
  int err = 0;
  char msg_id[IOTA_MESSAGE_ID_HEX_BYTES + 1] = {};
  char data[] = "sent from esp32 via iota.c";
  int nerrors = arg_parse(argc, argv, (void **)&send_msg_args);
  byte_t recv[IOTA_ADDRESS_BYTES] = {};
  if (nerrors != 0) {
    arg_print_errors(stderr, send_msg_args.end, argv[0]);
    return -1;
  }

  char const *const recv_addr = send_msg_args.receiver->sval[0];
  // validating receiver address
  if (strncmp(recv_addr, wallet->bech32HRP, strlen(wallet->bech32HRP)) == 0) {
    // convert bech32 address to binary
    if ((err = address_from_bech32(wallet->bech32HRP, recv_addr, recv))) {
      ESP_LOGE(TAG, "invalid bech32 address\n");
      return -1;
    }
  } else if (strlen(recv_addr) == IOTA_ADDRESS_HEX_BYTES) {
    // convert ed25519 string to binary
    if (hex_2_bin(recv_addr, strlen(recv_addr), recv + 1, ED25519_ADDRESS_BYTES) != 0) {
      ESP_LOGE(TAG, "invalid ed25519 address\n");
      return -1;
    }

  } else {
    ESP_LOGE(TAG, "invalid receiver address\n");
    return -1;
  }

  // balance = number * Mi
  uint64_t balance = (uint64_t)send_msg_args.balance->dval[0] * 1000000;

  if (balance > 0) {
    printf("send %" PRIu64 "Mi to %s\n", (uint64_t)send_msg_args.balance->dval[0], recv_addr);
  } else {
    printf("send indexation payload to tangle\n");
  }

  err = wallet_send(wallet, (uint32_t)send_msg_args.sender->dval[0], recv + 1, balance, "ESP32 Wallet", (byte_t *)data,
                    sizeof(data), msg_id, sizeof(msg_id));
  if (err) {
    printf("send message failed\n");
    return -1;
  }
  printf("Message Hash: %s\n", msg_id);
  return 0;
}

static void register_send_tokens() {
  send_msg_args.sender = arg_dbl1(NULL, NULL, "<index>", "Address index");
  send_msg_args.receiver = arg_str1(NULL, NULL, "<receiver>", "Receiver address");
  send_msg_args.balance = arg_dbl1(NULL, NULL, "<balance>", "balance");
  send_msg_args.end = arg_end(20);
  const esp_console_cmd_t send_msg_cmd = {
      .command = "send",
      .help = "send message to tangle",
      .hint = " <addr_index> <receiver> <balance>",
      .func = &fn_send_msg,
      .argtable = &send_msg_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&send_msg_cmd));
}

/* 'node_info' command */
static int fn_node_info(int argc, char **argv) {
  int err = 0;
  res_node_info_t *info = res_node_info_new();
  if (!info) {
    ESP_LOGE(TAG, "Create node info object failed\n");
    return -1;
  }

  if ((err = get_node_info(&wallet->endpoint, info)) == 0) {
    if (info->is_error) {
      printf("Error: %s\n", info->u.error->msg);
    } else {
      printf("Name: %s\n", info->u.output_node_info->name);
      printf("Version: %s\n", info->u.output_node_info->version);
      printf("isHealthy: %s\n", info->u.output_node_info->is_healthy ? "true" : "false");
      printf("Network ID: %s\n", info->u.output_node_info->network_id);
      printf("bech32HRP: %s\n", info->u.output_node_info->bech32hrp);
      printf("minPoWScore: %" PRIu64 "\n", info->u.output_node_info->min_pow_score);
      printf("Latest Milestone Index: %" PRIu64 "\n", info->u.output_node_info->latest_milestone_index);
      printf("Latest Milestone Timestamp: %" PRIu64 "\n", info->u.output_node_info->latest_milestone_timestamp);
      printf("Confirmed Milestone Index: %" PRIu64 "\n", info->u.output_node_info->confirmed_milestone_index);
      printf("Pruning Index: %" PRIu64 "\n", info->u.output_node_info->pruning_milestone_index);
      printf("MSP: %0.2f\n", info->u.output_node_info->msg_pre_sec);
      printf("Referenced MPS: %0.2f\n", info->u.output_node_info->referenced_msg_pre_sec);
      printf("Reference Rate: %0.2f%%\n", info->u.output_node_info->referenced_rate);
    }
  }
  res_node_info_free(info);
  return err;
}

static void register_api_node_info() {
  const esp_console_cmd_t node_info_cmd = {
      .command = "node_info",
      .help = "Show node info",
      .hint = NULL,
      .func = &fn_node_info,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&node_info_cmd));
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

/* 'api_msg_index' command */
static struct {
  struct arg_str *index;
  struct arg_end *end;
} api_find_msg_index_args;

static int fn_api_find_msg_index(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_find_msg_index_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_find_msg_index_args.end, argv[0]);
    return -1;
  }

  res_find_msg_t *res = res_find_msg_new();
  if (!res) {
    ESP_LOGE(TAG, "new object failed\n");
    return -1;
  }

  int err = find_message_by_index(&wallet->endpoint, api_find_msg_index_args.index->sval[0], res);
  if (err) {
    ESP_LOGE(TAG, "find message by index API failed");
  } else {
    if (res->is_error) {
      printf("%s\n", res->u.error->msg);
    } else {
      size_t count = res_find_msg_get_id_len(res);
      for (size_t i = 0; i < count; i++) {
        printf("%s\n", res_find_msg_get_id(res, i));
      }
      printf("message ID count %zu\n", count);
    }
  }

  res_find_msg_free(res);
  return err;
}

static void register_api_find_msg_index() {
  api_find_msg_index_args.index = arg_str1(NULL, NULL, "<index>", "Index string");
  api_find_msg_index_args.end = arg_end(2);
  const esp_console_cmd_t api_find_msg_cmd = {
      .command = "api_msg_index",
      .help = "Find messages by a given index",
      .hint = " <index>",
      .func = &fn_api_find_msg_index,
      .argtable = &api_find_msg_index_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_find_msg_cmd));
}

/* 'api_get_balance' command */
static struct {
  struct arg_str *addr;
  struct arg_end *end;
} api_get_balance_args;

static int fn_api_get_balance(int argc, char **argv) {
  byte_t address[IOTA_ADDRESS_BYTES] = {};
  char hex_addr[IOTA_ADDRESS_HEX_BYTES + 1] = {};
  int nerrors = arg_parse(argc, argv, (void **)&api_get_balance_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_get_balance_args.end, argv[0]);
    return -1;
  }

  char const *const bech32_add_str = api_get_balance_args.addr->sval[0];
  if (strncmp(bech32_add_str, wallet->bech32HRP, strlen(wallet->bech32HRP)) != 0) {
    printf("Invalid address hash\n");
    return -1;
  } else {
    // convert bech32 address to ed25519 address
    if (address_from_bech32(wallet->bech32HRP, bech32_add_str, address) != 0) {
      printf("Convert bech32 address to ed25519 failed\n");
      return -1;
    }

    // ed25519 address to hex string
    if (bin_2_hex(address + 1, IOTA_ADDRESS_BYTES - 1, hex_addr, sizeof(hex_addr)) != 0) {
      printf("Convert ed25519 address to hex string failed\n");
      return -1;
    }

    // get balance from connected node
    res_balance_t *res = res_balance_new();
    if (!res) {
      printf("Create res_balance_t object failed\n");
      return -1;
    } else {
      nerrors = get_balance(&wallet->endpoint, hex_addr, res);
      if (nerrors != 0) {
        printf("get_balance API failed\n");
      } else {
        if (res->is_error) {
          printf("Err: %s\n", res->u.error->msg);
        } else {
          printf("balance: %" PRIu64 "\n", res->u.output_balance->balance);
        }
      }
      res_balance_free(res);
    }
  }
  return nerrors;
}

static void register_api_get_balance() {
  api_get_balance_args.addr = arg_str1(NULL, NULL, "<address>", "Address HASH");
  api_get_balance_args.end = arg_end(2);
  const esp_console_cmd_t api_get_balance_cmd = {
      .command = "api_get_balance",
      .help = "Get balance from a given address",
      .hint = " <address>",
      .func = &fn_api_get_balance,
      .argtable = &api_get_balance_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_get_balance_cmd));
}

/* 'api_msg_children' command */
static struct {
  struct arg_str *msg_id;
  struct arg_end *end;
} api_msg_children_args;

static int fn_api_msg_children(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_msg_children_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_msg_children_args.end, argv[0]);
    return -1;
  }

  // check message id length
  char const *const msg_id_str = api_msg_children_args.msg_id->sval[0];
  if (strlen(msg_id_str) != IOTA_MESSAGE_ID_HEX_BYTES) {
    printf("Invalid message ID length\n");
    return -1;
  }

  res_msg_children_t *res = res_msg_children_new();
  if (!res) {
    printf("Allocate response failed\n");
    return -1;
  } else {
    nerrors = get_message_children(&wallet->endpoint, msg_id_str, res);
    if (nerrors) {
      printf("get_message_children error %d\n", nerrors);
    } else {
      if (res->is_error) {
        printf("Err: %s\n", res->u.error->msg);
      } else {
        size_t count = res_msg_children_len(res);
        if (count == 0) {
          printf("Message not found\n");
        } else {
          for (size_t i = 0; i < count; i++) {
            printf("%s\n", res_msg_children_get(res, i));
          }
        }
      }
    }
    res_msg_children_free(res);
  }

  return nerrors;
}

static void register_api_msg_children() {
  api_msg_children_args.msg_id = arg_str1(NULL, NULL, "<ID>", "Message ID");
  api_msg_children_args.end = arg_end(2);
  const esp_console_cmd_t api_msg_children_cmd = {
      .command = "api_msg_children",
      .help = "Get children from a given message ID",
      .hint = " <ID>",
      .func = &fn_api_msg_children,
      .argtable = &api_msg_children_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_msg_children_cmd));
}

/* 'api_msg_meta' command */
static struct {
  struct arg_str *msg_id;
  struct arg_end *end;
} api_msg_meta_args;

static int fn_api_msg_meta(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_msg_meta_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_msg_meta_args.end, argv[0]);
    return -1;
  }

  // check message id length
  char const *const msg_id_str = api_msg_meta_args.msg_id->sval[0];
  if (strlen(msg_id_str) != IOTA_MESSAGE_ID_HEX_BYTES) {
    printf("Invalid message ID length\n");
    return -1;
  }

  res_msg_meta_t *res = res_msg_meta_new();
  if (!res) {
    printf("Allocate response failed\n");
    return -1;
  } else {
    nerrors = get_message_metadata(&wallet->endpoint, msg_id_str, res);
    if (nerrors) {
      printf("get_message_metadata error %d\n", nerrors);
    } else {
      if (res->is_error) {
        printf("%s\n", res->u.error->msg);
      } else {
        printf("Message ID: %s\nisSolid: %s\n", res->u.meta->msg_id, res->u.meta->is_solid ? "True" : "False");
        size_t parents = res_msg_meta_parents_len(res);
        printf("%zu parents:\n", parents);
        for (size_t i = 0; i < parents; i++) {
          printf("\t%s\n", res_msg_meta_parent_get(res, i));
        }
        printf("ledgerInclusionState: %s\n", res->u.meta->inclusion_state);

        // check milestone index
        if (res->u.meta->milestone_idx != 0) {
          printf("milestoneIndex: %" PRIu64 "\n", res->u.meta->milestone_idx);
        }

        // check referenced milestone index
        if (res->u.meta->referenced_milestone != 0) {
          printf("referencedByMilestoneIndex: %" PRIu64 "\n", res->u.meta->referenced_milestone);
        }

        // check should promote
        if (res->u.meta->should_promote >= 0) {
          printf("shouldPromote: %s\n", res->u.meta->should_promote ? "True" : "False");
        }
        // check should reattach
        if (res->u.meta->should_reattach >= 0) {
          printf("shouldReattach: %s\n", res->u.meta->should_reattach ? "True" : "False");
        }
      }
    }
    res_msg_meta_free(res);
  }
  return nerrors;
}

static void register_api_msg_meta() {
  api_msg_meta_args.msg_id = arg_str1(NULL, NULL, "<ID>", "Message ID");
  api_msg_meta_args.end = arg_end(2);
  const esp_console_cmd_t api_msg_meta_cmd = {
      .command = "api_msg_meta",
      .help = "Get metadata from a given message ID",
      .hint = " <ID>",
      .func = &fn_api_msg_meta,
      .argtable = &api_msg_meta_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_msg_meta_cmd));
}

//============= Public functions====================

void register_wallet_commands() {
  // system info
  register_free();
  register_heap();
  register_stack_info();
  register_version();
  register_restart();

  // wallet APIs
  register_get_balance();
  register_send_tokens();
  register_get_address();
  register_sensor();

  // client APIs
  register_api_node_info();
  register_api_find_msg_index();
  register_api_get_balance();
  register_api_msg_children();
  register_api_msg_meta();
  // TODO
  // register_api_get_msg();
  // register_api_get_output();
  // register_api_get_outputs_from_addr();
  // register_api_get_tips();
}

int init_wallet() {
  byte_t seed[IOTA_SEED_BYTES] = {};

  // seed validation
  if (strcmp(CONFIG_WALLET_SEED, "random") == 0) {
    random_seed(seed);
  } else {
    size_t seed_len = strlen(CONFIG_WALLET_SEED);
    if (seed_len != IOTA_SEED_HEX_BYTES) {
      ESP_LOGI(TAG, "invalid seed length: %zu, expect 64 characters", seed_len);
      return -1;
    }
    hex_2_bin(CONFIG_WALLET_SEED, strlen(CONFIG_WALLET_SEED), seed, sizeof(seed));
  }

  // create wallet instance
  wallet = wallet_create(seed, "m/44'/4218'/0'/0'");
  if (!wallet) {
    ESP_LOGE(TAG, "wallet create failed\n");
    return -1;
  }

  if (endpoint_validation(wallet) != 0) {
    wallet_destroy(wallet);
    return -1;
  }

  // config wallet
  ESP_LOGI(TAG, "Connect to node: %s:%d tls:%s", wallet->endpoint.host, wallet->endpoint.port,
           wallet->endpoint.use_tls ? "true" : "false");
  if (wallet_set_endpoint(wallet, wallet->endpoint.host, wallet->endpoint.port, wallet->endpoint.use_tls) == 0) {
    if (wallet_update_bech32HRP(wallet) != 0) {
      ESP_LOGE(TAG, "update bech32HRP failed");
      wallet_destroy(wallet);
      return -1;
    }

    ESP_LOGI(TAG, "Bech32HRP: %s", wallet->bech32HRP);
    return 0;
  }

  ESP_LOGE(TAG, "config endpoint failed");
  wallet_destroy(wallet);
  return -1;
}

uint64_t timestamp() {
  struct timeval tv = {0, 0};
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec;
}
