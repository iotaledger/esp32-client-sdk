// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include "argtable3/argtable3.h"
#include "core/address.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cli_wallet.h"
#include "core/utils/bech32.h"
#include "sdkconfig.h"
#include "wallet/bip39.h"
#include "wallet/output_basic.h"
#include "wallet/wallet.h"

#define NODE_HOST CONFIG_IOTA_NODE_URL
#define NODE_PORT CONFIG_IOTA_NODE_PORT

#ifdef CONFIG_IOTA_NODE_USE_TLS
#define NODE_USE_TLS true
#else
#define NODE_USE_TLS false
#endif

#define WALLET_COIN_TYPE SLIP44_COIN_TYPE_IOTA

#define Mi 1000000

static const char *TAG = "wallet";

iota_wallet_t *wallet = NULL;

static void dump_address(iota_wallet_t *w, uint32_t index, bool is_change) {
  char bech32_addr[BECH32_MAX_STRING_LEN + 1];
  address_t address;

  if (wallet_ed25519_address_from_index(w, is_change, index, &address) != 0) {
    ESP_LOGE(TAG, "Failed to generate address from the index!\n");
    return;
  }

  if (address_to_bech32(&address, w->bech32HRP, bech32_addr, sizeof(bech32_addr)) != 0) {
    ESP_LOGE(TAG, "Failed to convert address to bech32!\n");
    return;
  }

  printf("Addr[%" PRIu32 "]\n", index);
  // print ed25519 address without version filed.
  printf("\t");
  dump_hex_str(address.address, sizeof(address.address));
  // print out
  printf("\t%s\n", bech32_addr);
}

/* 'address' command */
static struct {
  struct arg_dbl *idx_start;
  struct arg_dbl *idx_count;
  struct arg_int *is_change;
  struct arg_end *end;
} get_addr_args;

static int fn_get_address(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&get_addr_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, get_addr_args.end, argv[0]);
    return -1;
  }
  uint32_t start = (uint32_t)get_addr_args.idx_start->dval[0];
  uint32_t count = (uint32_t)get_addr_args.idx_count->dval[0];
  bool is_change = get_addr_args.is_change->ival[0];

  printf("list addresses with change %d\n", is_change);
  for (uint32_t i = start; i < start + count; i++) {
    dump_address(wallet, i, is_change);
  }
  return 0;
}

static void register_wallet_get_address() {
  get_addr_args.idx_start = arg_dbl1(NULL, NULL, "<start>", "start index");
  get_addr_args.idx_count = arg_dbl1(NULL, NULL, "<count>", "number of addresses");
  get_addr_args.is_change = arg_int1(NULL, NULL, "<is_change>", "0 or 1");
  get_addr_args.end = arg_end(5);
  const esp_console_cmd_t get_address_cmd = {
      .command = "wallet_address",
      .help = "Get ed25519 addresses from index",
      .hint = " <start> <count> <is_change>",
      .func = &fn_get_address,
      .argtable = &get_addr_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&get_address_cmd));
}

/* 'wallet_send_token' command */
static struct {
  struct arg_dbl *sender_index;
  struct arg_dbl *receiver_index;
  struct arg_int *amount;
  struct arg_end *end;
} wallet_send_token_args;

static int fn_wallet_send_token(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&wallet_send_token_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, wallet_send_token_args.end, argv[0]);
    return -1;
  }

  uint32_t const sender_addr_index = wallet_send_token_args.sender_index->dval[0];      // address index of a sender
  uint32_t const receiver_addr_index = wallet_send_token_args.receiver_index->dval[0];  // address index of a receiver
  uint64_t const amount = wallet_send_token_args.amount->ival[0];                       // amount to transfer

  address_t sender, receiver;
  if (wallet_ed25519_address_from_index(wallet, false, sender_addr_index, &sender) != 0) {
    ESP_LOGE(TAG, "Failed to generate a sender address from an index!\n");
    return -1;
  }
  if (wallet_ed25519_address_from_index(wallet, false, receiver_addr_index, &receiver) != 0) {
    ESP_LOGE(TAG, "Failed to generate a receiver address from an index!\n");
    return -1;
  }

  // convert sender address to bech32 format
  char bech32_sender[BECH32_MAX_STRING_LEN + 1] = {};
  if (address_to_bech32(&sender, wallet->bech32HRP, bech32_sender, sizeof(bech32_sender)) != 0) {
    ESP_LOGE(TAG, "Failed converting sender address to bech32 format!\n");
    return -1;
  }

  // convert receiver address to bech32 format
  char bech32_receiver[BECH32_MAX_STRING_LEN + 1] = {};
  if (address_to_bech32(&receiver, wallet->bech32HRP, bech32_receiver, sizeof(bech32_receiver)) != 0) {
    ESP_LOGE(TAG, "Failed converting receiver address to bech32 format!\n");
    return -1;
  }

  ESP_LOGI(TAG, "Sender address: %s\n", bech32_sender);
  ESP_LOGI(TAG, "Receiver address: %s\n", bech32_receiver);
  ESP_LOGI(TAG, "Amount to send: %" PRIu64 "\n", amount * Mi);

  // transfer tokens
  ESP_LOGI(TAG, "Sending transaction block to the Tangle...\n");
  res_send_block_t blk_res = {};
  if (wallet_basic_output_send(wallet, false, sender_addr_index, amount * Mi, NULL, &receiver, &blk_res) != 0) {
    ESP_LOGE(TAG, "Sending block to the Tangle failed!\n");
    return -1;
  }

  if (blk_res.is_error) {
    ESP_LOGE(TAG, "Error: %s\n", blk_res.u.error->msg);
    res_err_free(blk_res.u.error);
    return -1;
  }

  ESP_LOGI(TAG, "Block successfully sent.\n");
  ESP_LOGI(TAG, "Block ID: %s\n", blk_res.u.blk_id);

  return nerrors;
}

static void register_wallet_send_token() {
  wallet_send_token_args.sender_index = arg_dbl1(NULL, NULL, "<sender_index>", "sender index");
  wallet_send_token_args.receiver_index = arg_dbl1(NULL, NULL, "<receiver_index>", "receiver index");
  wallet_send_token_args.amount = arg_int1(NULL, NULL, "<amount>", "token amount");
  wallet_send_token_args.end = arg_end(2);
  const esp_console_cmd_t wallet_send_token_cmd = {
      .command = "wallet_send_token",
      .help = "Send tokens from sender address to receiver address",
      .hint = " <sender index> <receiver index> <amount>",
      .func = &fn_wallet_send_token,
      .argtable = &wallet_send_token_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wallet_send_token_cmd));
}

//============= Public functions====================

void register_wallet_commands() {
  // wallet APIs
  register_wallet_send_token();
  register_wallet_get_address();
}

int init_wallet() {
  if (strcmp("random", CONFIG_WALLET_MNEMONIC) == 0) {
    wallet = wallet_create(NULL, "", WALLET_COIN_TYPE, 0);
  } else {
    wallet = wallet_create(CONFIG_WALLET_MNEMONIC, "", WALLET_COIN_TYPE, 0);
  }
  if (!wallet) {
    ESP_LOGE(TAG, "Failed to create a wallet object!\n");
    return -1;
  }

  if (wallet_set_endpoint(wallet, NODE_HOST, NODE_PORT, NODE_USE_TLS) != 0) {
    ESP_LOGE(TAG, "Failed to set a wallet endpoint!\n");
    wallet_destroy(wallet);
    return -1;
  }

  if (wallet_update_node_config(wallet) != 0) {
    ESP_LOGE(TAG, "Failed to update a node configuration!\n");
    wallet_destroy(wallet);
    return -1;
  }

  return 0;
}
