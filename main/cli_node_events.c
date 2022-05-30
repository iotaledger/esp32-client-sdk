// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "client/api/events/node_event.h"
#include "client/api/events/sub_blocks_metadata.h"
#include "client/api/events/sub_milestone_payload.h"
#include "client/api/events/sub_outputs_payload.h"
#include "client/api/events/sub_serialized_output.h"

#include "client/api/restful/get_block_metadata.h"
#include "client/api/restful/get_output.h"

#include "cli_node_events.h"

// Update test data in menuconfig while testing
#define TEST_BLOCK_ID CONFIG_EVENT_BLOCK_ID
#define TEST_OUTPUT_ID CONFIG_EVENT_OUTPUT_ID
#define TEST_TXN_ID CONFIG_EVENT_TXN_ID

#define EVENTS_HOST CONFIG_EVENTS_HOST
#define EVENTS_PORT CONFIG_EVENTS_PORT
#define EVENTS_CLIENT_ID CONFIG_EVENTS_CLIENT_ID
#define EVENTS_KEEP_ALIVE CONFIG_EVENTS_KEEP_ALIVE

event_client_handle_t client;
bool is_client_running = false;
int event_select_g = 0;

void process_event_data(event_client_event_t *event);

void callback(event_client_event_t *event) {
  switch (event->event_id) {
    case NODE_EVENT_ERROR:
      printf("Node event network error : %s\n", (char *)event->data);
      break;
    case NODE_EVENT_CONNECTED:
      printf("Node event network connected\n");
      /* Making subscriptions in the on_connect() callback means that if the
       * connection drops and is automatically resumed by the client, then the
       * subscriptions will be recreated when the client reconnects. */
      // Check if LSB bit is set
      if (event_select_g & 1) {
        event_subscribe(event->client, NULL, TOPIC_MILESTONE_LATEST, 1);
        event_subscribe(event->client, NULL, TOPIC_MILESTONE_CONFIRMED, 1);
      }
      // Check if 2nd bit from LSB is set
      if (event_select_g & (1 << 1)) {
        event_subscribe(event->client, NULL, TOPIC_BLOCKS, 1);
      }
      // Check if 3rd bit from LSB is set
      if (event_select_g & (1 << 2)) {
        event_subscribe(event->client, NULL, TOPIC_BLK_TAGGED_DATA, 1);
      }
      // Check if 4th bit from LSB is set
      if (event_select_g & (1 << 3)) {
        event_subscribe(event->client, NULL, TOPIC_MILESTONES, 1);
      }
      // Check if 5th bit from LSB is set
      if (event_select_g & (1 << 4)) {
        if (strlen(TEST_BLOCK_ID) > 0) {
          event_subscribe_blk_metadata(event->client, NULL, TEST_BLOCK_ID, 1);
        }
      }
      // Check if 6th bit from LSB is set
      if (event_select_g & (1 << 5)) {
        if (strlen(TEST_OUTPUT_ID) > 0) {
          event_sub_outputs_id(event->client, NULL, TEST_OUTPUT_ID, 1);
        }
      }
      // Check if 7th bit from LSB is set
      if (event_select_g & (1 << 6)) {
        if (strlen(TEST_TXN_ID) > 0) {
          event_sub_txn_included_blk(event->client, NULL, TEST_TXN_ID, 1);
        }
      }
      // Check if 8th bit from LSB is set
      if (event_select_g & (1 << 7)) {
        event_subscribe(event->client, NULL, TOPIC_BLK_TRANSACTION, 1);
      }
      break;
    case NODE_EVENT_DISCONNECTED:
      printf("Node event network disconnected\n");
      break;
    case NODE_EVENT_SUBSCRIBED:
      printf("Subscribed topic\n");
      break;
    case NODE_EVENT_UNSUBSCRIBED:
      printf("Unsubscribed topic\n");
      break;
    case NODE_EVENT_PUBLISHED:
      // To Do : Handle publish callback
      break;
    case NODE_EVENT_DATA:
      printf("Message Received\nTopic : %.*s\n", event->topic_len, event->topic);
      process_event_data(event);
      break;
    default:
      break;
  }
}

static void parse_and_print_block_metadata(char const *const data, uint32_t len) {
  // Create and allocate memory for response object
  block_meta_t *res = metadata_new();
  if (res) {
    parse_blocks_metadata(data, res);

    // Print received data
    printf("Block Id :%s\n", res->blk_id);
    // Get parent id count
    size_t parents_count = block_meta_parents_count(res);
    for (size_t i = 0; i < parents_count; i++) {
      printf("Parent Id %zu : %s\n", i + 1, block_meta_parent_get(res, i));
    }
    printf("Inclusion State : %s\n", res->inclusion_state);
    printf("Is Solid : %s\n", res->is_solid ? "true" : "false");
    printf("Should Promote : %s\n", res->should_promote ? "true" : "false");
    printf("Should Reattach : %s\n", res->should_reattach ? "true" : "false");
    printf("Referenced Milestone : %u\n", res->referenced_milestone);

    // Free response object
    metadata_free(res);
  }
}

static void parse_and_print_output_payload(char const *const data, uint32_t len) {
  get_output_t *output = get_output_new();
  if (output) {
    parse_get_output(data, output);
    print_get_output(output, 0);
    get_output_free(output);
  }
}

static void print_serialized_data(unsigned char *data, uint32_t len) {
  printf("Received Serialized Data : ");
  for (uint32_t i = 0; i < len; i++) {
    printf("%02x", data[i]);
  }
  printf("\n");
}

void process_event_data(event_client_event_t *event) {
  char *topic_buff = (char *)calloc(event->topic_len + 1, sizeof(char));
  memcpy(topic_buff, event->topic, event->topic_len);

  char *data_buff = (char *)calloc(event->data_len + 1, sizeof(char));
  memcpy(data_buff, event->data, event->data_len);

  if (!strcmp(topic_buff, TOPIC_MILESTONE_LATEST) || !strcmp(topic_buff, TOPIC_MILESTONE_CONFIRMED)) {
    events_milestone_payload_t res = {};
    if (parse_milestone_payload((char *)data_buff, &res) == 0) {
      printf("Index :%u\nTimestamp : %u\n", res.index, res.timestamp);
    }
  }
  // check for topic blocks
  else if (!strcmp(topic_buff, TOPIC_BLOCKS)) {
    print_serialized_data((unsigned char *)data_buff, event->data_len);
  }
  // check for topic blocks/tagged-data
  else if (!strcmp(topic_buff, TOPIC_BLK_TAGGED_DATA)) {
    print_serialized_data((unsigned char *)data_buff, event->data_len);
  }
  // check for topic milestones
  else if (!strcmp(topic_buff, TOPIC_MILESTONES)) {
    print_serialized_data((unsigned char *)data_buff, event->data_len);
  }
  // check for topic block-metadata/{blockId} and block-metadata/referenced
  else if (!strcmp(topic_buff, "block-metadata/")) {
    parse_and_print_block_metadata(data_buff, event->data_len);
  }
  // check for outputs/{outputId}
  else if (strstr(topic_buff, "outputs/") != NULL) {
    parse_and_print_output_payload(data_buff, event->data_len);
  }
  // check for topic transactions/{transactionId}/included-block
  else if ((strstr(topic_buff, "transactions/") != NULL) && (strstr(topic_buff, "/included-block") != NULL)) {
    print_serialized_data((unsigned char *)data_buff, event->data_len);
  }
  // check for topic blocks/transaction
  else if (!strcmp(topic_buff, TOPIC_BLK_TRANSACTION)) {
    print_serialized_data((unsigned char *)data_buff, event->data_len);
  }

  free(topic_buff);
  free(data_buff);
}

int node_events(int event_select) {
  if ((event_select == 0) && is_client_running) {
    event_destroy(client);
    is_client_running = false;
  } else if ((event_select > 0) && !is_client_running) {
    event_select_g = event_select;
    event_client_config_t config = {
        .host = EVENTS_HOST, .port = EVENTS_PORT, .client_id = EVENTS_CLIENT_ID, .keepalive = EVENTS_KEEP_ALIVE};
    client = event_init(&config);
    event_register_cb(client, &callback);
    int rc = event_start(client);
    if (rc == -1) {
      event_destroy(client);
      return -1;
    }
    is_client_running = true;
  } else {
    return -1;
  }
  return 0;
}

/* 'get_events_data' command */
static struct {
  struct arg_str *event_select;
  struct arg_end *end;
} node_events_args;

static int fn_get_node_events(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&node_events_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, node_events_args.end, argv[0]);
    return -1;
  }
  const char *event_select_str = node_events_args.event_select->sval[0];
  int event_select_len = strlen(event_select_str);
  // Check if only two characters are received
  if (event_select_len > 2) {
    printf("Invalid input.\n");
    return -1;
  }
  // Check if received string is hex encoded
  for (int i = 0; i < event_select_len; i++) {
    char ch = event_select_str[i];
    if ((ch < '0' || ch > '9') && (ch < 'A' || ch > 'F')) {
      printf("Invalid input.\n");
      return -1;
    }
  }
  // Convert the received hex string to integer
  int event_select_int = (int)strtol(event_select_str, NULL, 16);

  node_events(event_select_int);
  return 0;
}

void register_node_events() {
  node_events_args.event_select = arg_str1(NULL, NULL, "<Events Select>", "Events Select");
  node_events_args.end = arg_end(2);
  const esp_console_cmd_t node_events_cmd = {
      .command = "node_events",
      .help = "Get node events data",
      .hint = NULL,
      .func = &fn_get_node_events,
      .argtable = &node_events_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&node_events_cmd));
}
