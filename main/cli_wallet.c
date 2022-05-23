// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
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

#include "events_api.h"

#include "wallet/bip39.h"
#include "wallet/wallet.h"

#define NODE_HOST CONFIG_IOTA_NODE_URL
#define NODE_PORT CONFIG_IOTA_NODE_PORT

#ifdef CONFIG_IOTA_NODE_USE_TLS
#define NODE_USE_TLS true
#else
#define NODE_USE_TLS false
#endif

#define TEST_COIN_TYPE SLIP44_COIN_TYPE_IOTA

static const char *TAG = "wallet";

iota_wallet_t *wallet = NULL;

#if 0  // FIXME
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
      return -2;
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

static void dump_address(iota_wallet_t *w, uint32_t index, bool is_change) {
  char tmp_bech32_addr[65];
  byte_t tmp_addr[ED25519_ADDRESS_BYTES];

  wallet_bech32_from_index(w, is_change, index, tmp_bech32_addr);
  wallet_address_from_index(w, is_change, index, tmp_addr);

  printf("Addr[%" PRIu32 "]\n", index);
  // print ed25519 address without version filed.
  printf("\t");
  dump_hex_str(tmp_addr, ED25519_ADDRESS_BYTES);
  // print out
  printf("\t%s\n", tmp_bech32_addr);
}

/* 'balance' command */
static struct {
  struct arg_dbl *idx_start;
  struct arg_dbl *idx_count;
  struct arg_int *is_change;
  struct arg_end *end;
} get_balance_args;

static int fn_get_balance(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&get_balance_args);
  uint64_t balance = 0;
  if (nerrors != 0) {
    arg_print_errors(stderr, get_balance_args.end, argv[0]);
    return -1;
  }

  uint32_t start = get_balance_args.idx_start->dval[0];
  uint32_t count = get_balance_args.idx_count->dval[0];
  bool is_change = get_balance_args.is_change->ival[0];

  for (uint32_t i = start; i < start + count; i++) {
    if (wallet_balance_by_index(wallet, is_change, i, &balance) != 0) {
      printf("Err: get balance failed on index %u\n", i);
      return -2;
    }
    dump_address(wallet, i, is_change);
    printf("balance: %" PRIu64 "\n", balance);
  }
  return 0;
}

static void register_get_balance() {
  get_balance_args.idx_start = arg_dbl1(NULL, NULL, "<start>", "start index");
  get_balance_args.idx_count = arg_dbl1(NULL, NULL, "<count>", "number of address");
  get_balance_args.is_change = arg_int1(NULL, NULL, "<is_change>", "0 or 1");
  get_balance_args.end = arg_end(5);
  const esp_console_cmd_t get_balance_cmd = {
      .command = "balance",
      .help = "Get the balance from a range of address index",
      .hint = " <start> <count> <is_change>",
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
    if ((nerrors = address_from_bech32(wallet->bech32HRP, recv_addr, recv))) {
      ESP_LOGE(TAG, "invalid bech32 address\n");
      return -2;
    }
  } else if (strlen(recv_addr) == IOTA_ADDRESS_HEX_BYTES) {
    // convert ed25519 string to binary
    if (hex_2_bin(recv_addr, strlen(recv_addr), recv + 1, ED25519_ADDRESS_BYTES) != 0) {
      ESP_LOGE(TAG, "invalid ed25519 address\n");
      return -3;
    }

  } else {
    ESP_LOGE(TAG, "invalid receiver address\n");
    return -4;
  }

  // balance = number * Mi
  uint64_t balance = (uint64_t)send_msg_args.balance->dval[0] * 1000000;

  if (balance > 0) {
    printf("send %" PRIu64 "Mi to %s\n", (uint64_t)send_msg_args.balance->dval[0], recv_addr);
  } else {
    printf("send indexation payload to tangle\n");
  }

  // send from address that change is 0
  nerrors = wallet_send(wallet, false, (uint32_t)send_msg_args.sender->dval[0], recv + 1, balance, "ESP32 Wallet",
                        (byte_t *)data, sizeof(data), msg_id, sizeof(msg_id));
  if (nerrors) {
    printf("send message failed\n");
    return -5;
  }
  printf("Message Hash: %s\n", msg_id);
  return nerrors;
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

/* 'api_get_balance' command */
static struct {
  struct arg_str *addr;
  struct arg_end *end;
} api_get_balance_args;

static int fn_api_get_balance(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_get_balance_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_get_balance_args.end, argv[0]);
    return -1;
  }

  char const *const bech32_add_str = api_get_balance_args.addr->sval[0];
  if (strncmp(bech32_add_str, wallet->bech32HRP, strlen(wallet->bech32HRP)) != 0) {
    printf("Invalid prefix of the bech32 address\n");
    return -2;
  } else {
    // get balance from connected node
    res_balance_t *res = res_balance_new();
    if (!res) {
      printf("Create res_balance_t object failed\n");
      return -4;
    } else {
      nerrors = get_balance(&wallet->endpoint, true, bech32_add_str, res);
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
  api_get_balance_args.addr = arg_str1(NULL, NULL, "<bech32>", "Bech32 Address");
  api_get_balance_args.end = arg_end(2);
  const esp_console_cmd_t api_get_balance_cmd = {
      .command = "api_get_balance",
      .help = "Get balance from a given bech32 address",
      .hint = " <bech32 address>",
      .func = &fn_api_get_balance,
      .argtable = &api_get_balance_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_get_balance_cmd));
}

/* 'mnemonic_gen' command */
static struct {
  struct arg_int *language_id;
  struct arg_end *end;
} mnemonic_gen_args;

static int fn_mnemonic_gen(int argc, char **argv) {
  char buf[256] = {};

  int nerrors = arg_parse(argc, argv, (void **)&mnemonic_gen_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mnemonic_gen_args.end, argv[0]);
    return -1;
  }

#if CONFIG_ENG_MNEMONIC_ONLY
  mnemonic_generator(MS_ENTROPY_256, MS_LAN_EN, buf, sizeof(buf));
#else
  int lan_id = mnemonic_gen_args.language_id->ival[0];
  // validate id
  if (lan_id < MS_LAN_EN || lan_id > MS_LAN_PT) {
    printf("invalid language id, id value is %d to %d\n", MS_LAN_EN, MS_LAN_PT);
    return -2;
  }
  mnemonic_generator(MS_ENTROPY_256, lan_id, buf, sizeof(buf));
#endif
  printf("%s\n", buf);
  return 0;
}

static void register_mnemonic_gen() {
  mnemonic_gen_args.language_id = arg_int1(NULL, NULL, "<language id>", "0 to 8");
  mnemonic_gen_args.end = arg_end(2);
  const esp_console_cmd_t mnemonic_gen_cmd = {
      .command = "mnemonic_gen",
      .help = "generate a random mnemonic sentence",
      .hint = " <Language ID>",
      .func = &fn_mnemonic_gen,
      .argtable = &mnemonic_gen_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&mnemonic_gen_cmd));
}

/* 'mnemonic_update' command */

static struct {
  struct arg_str *ms;
  struct arg_end *end;
} ms_update_args;

static int fn_mnemonic_update(int argc, char **argv) {
  byte_t new_seed[64] = {};

  int nerrors = arg_parse(argc, argv, (void **)&ms_update_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, ms_update_args.end, argv[0]);
    return -1;
  }

  char const *const ms = ms_update_args.ms->sval[0];

  if (mnemonic_to_seed(ms, "", new_seed, sizeof(new_seed)) == 0) {
    // dump_hex_str(new_seed, sizeof(new_seed));
    // replace seed
    memcpy(wallet->seed, new_seed, IOTA_SEED_BYTES);
    printf("mnemonic is changed to\n%s\n", ms);
    return 0;
  }

  printf("Update mnemonic seed failed..\n");

  return -1;
}

static void register_mnemonic_update() {
  ms_update_args.ms = arg_str1(NULL, NULL, "<mnemonic>", "Mnemonic sentence");
  ms_update_args.end = arg_end(2);

  const esp_console_cmd_t mnemonic_update_cmd = {
      .command = "mnemonic_update",
      .help = "Replace wallet mnemonic",
      .hint = " <mnemonic>",
      .func = &fn_mnemonic_update,
      .argtable = &ms_update_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&mnemonic_update_cmd));
}

//============= Public functions====================

void register_wallet_commands() {
  // // wallet APIs
  register_get_balance();
  register_send_tokens();
  register_mnemonic_gen();
  register_mnemonic_update();
}

int init_wallet() {
  wallet = wallet_create(CONFIG_WALLET_MNEMONIC, "", TEST_COIN_TYPE, 0);
  if (!wallet) {
    printf("Failed to create a wallet object!\n");
    return -1;
  }

  if (wallet_set_endpoint(wallet, NODE_HOST, NODE_PORT, NODE_USE_TLS) != 0) {
    printf("Failed to set a wallet endpoint!\n");
    wallet_destroy(wallet);
    return -1;
  }

  if (wallet_update_node_config(wallet) != 0) {
    printf("Failed to update a node configuration!\n");
    wallet_destroy(wallet);
    return -1;
  }

  return 0;
}
#endif
