// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cli_wallet.h"
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

/* 'api_get_balance' command */
static struct {
  struct arg_str *addr;
  struct arg_end *end;
} wallet_get_balance_args;

static int fn_wallet_get_balance(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&wallet_get_balance_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, wallet_get_balance_args.end, argv[0]);
    return -1;
  }
  uint64_t balance = 0;
  char const *const bech32_add_str = wallet_get_balance_args.addr->sval[0];
  if (strncmp(bech32_add_str, wallet->bech32HRP, strlen(wallet->bech32HRP)) != 0) {
    ESP_LOGE(TAG, "invalid prefix of the bech32 address\n");
    return -1;
  } else {
    // get balance from connected node
    nerrors = wallet_balance_by_bech32(wallet, bech32_add_str, &balance);
    if (nerrors != 0) {
      ESP_LOGE(TAG, "wallet get_balance failed\n");
    } else {
      ESP_LOGI(TAG, "balance: %" PRIu64 "\n", balance);
    }
  }
  return nerrors;
}

static void register_wallet_get_balance() {
  wallet_get_balance_args.addr = arg_str1(NULL, NULL, "<bech32>", "Bech32 Address");
  wallet_get_balance_args.end = arg_end(2);
  const esp_console_cmd_t wallet_get_balance_cmd = {
      .command = "wallet_get_balance",
      .help = "Get balance from a given bech32 address",
      .hint = " <bech32 address>",
      .func = &fn_wallet_get_balance,
      .argtable = &wallet_get_balance_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wallet_get_balance_cmd));
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
  char bech32_sender[BIN_TO_HEX_STR_BYTES(ADDRESS_MAX_BYTES)] = {};
  if (address_to_bech32(&sender, wallet->bech32HRP, bech32_sender, sizeof(bech32_sender)) != 0) {
    ESP_LOGE(TAG, "Failed converting sender address to bech32 format!\n");
    return -1;
  }

  // convert receiver address to bech32 format
  char bech32_receiver[BIN_TO_HEX_STR_BYTES(ADDRESS_MAX_BYTES)] = {};
  if (address_to_bech32(&receiver, wallet->bech32HRP, bech32_receiver, sizeof(bech32_receiver)) != 0) {
    ESP_LOGE(TAG, "Failed converting receiver address to bech32 format!\n");
    return -1;
  }

  ESP_LOGI(TAG, "Sender address: %s\n", bech32_sender);
  ESP_LOGI(TAG, "Receiver address: %s\n", bech32_receiver);
  ESP_LOGI(TAG, "Amount to send: %" PRIu64 "\n", amount * Mi);

  // transfer tokens
  ESP_LOGI(TAG, "Sending transaction message to the Tangle...\n");
  res_send_message_t msg_res = {};
  if (wallet_basic_output_send(wallet, false, sender_addr_index, amount * Mi, &receiver, &msg_res) != 0) {
    ESP_LOGE(TAG, "Sending message to the Tangle failed!\n");
    return -1;
  }

  if (msg_res.is_error) {
    ESP_LOGE(TAG, "Error: %s\n", msg_res.u.error->msg);
    res_err_free(msg_res.u.error);
    return -1;
  }

  ESP_LOGI(TAG, "Message successfully sent.\n");
  ESP_LOGI(TAG, "Message ID: %s\n", msg_res.u.msg_id);

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
  register_wallet_get_balance();
  register_wallet_send_token();
}

int init_wallet() {
  wallet = wallet_create(CONFIG_WALLET_MNEMONIC, "", WALLET_COIN_TYPE, 0);
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
