// Copyright 2022 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "argtable3/argtable3.h"
#include "cli_restful.h"
#include "client/api/restful/get_block.h"
#include "client/api/restful/get_block_metadata.h"
#include "client/api/restful/get_node_info.h"
#include "client/api/restful/get_output.h"
#include "client/api/restful/get_tips.h"
#include "client/api/restful/send_tagged_data.h"
#include "client/client_service.h"

static const char *TAG = "restful";

#define NODE_HOST CONFIG_IOTA_NODE_URL
#define NODE_PORT CONFIG_IOTA_NODE_PORT

#ifdef CONFIG_IOTA_NODE_USE_TLS
#define NODE_USE_TLS true
#else
#define NODE_USE_TLS false
#endif

// Node config
iota_client_conf_t ctx;

/* 'node_info' command */
static int fn_node_info(int argc, char **argv) {
  int err = 0;
  res_node_info_t *info = res_node_info_new();
  if (!info) {
    ESP_LOGE(TAG, "Create node info object failed\n");
    return -1;
  }

  if ((err = get_node_info(&ctx, info)) == 0) {
    if (info->is_error) {
      printf("Error: %s\n", info->u.error->msg);
    } else {
      node_info_print(info, 0);
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

/* 'api_tips' command */
static int fn_api_tips(int argc, char **argv) {
  int err = 0;
  res_tips_t *res = res_tips_new();
  if (!res) {
    ESP_LOGE(TAG, "Allocate tips object failed\n");
    return -1;
  }

  err = get_tips(&ctx, res);
  if (err != 0) {
    printf("get_tips error\n");
  } else {
    if (res->is_error) {
      printf("%s\n", res->u.error->msg);
    } else {
      for (size_t i = 0; i < get_tips_id_count(res); i++) {
        printf("%s\n", get_tips_id(res, i));
      }
    }
  }

  res_tips_free(res);
  return err;
}

static void register_api_tips() {
  const esp_console_cmd_t api_tips_cmd = {
      .command = "api_tips",
      .help = "Get tips from connected node",
      .hint = NULL,
      .func = &fn_api_tips,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_tips_cmd));
}

/* 'api_get_blk' command */
static struct {
  struct arg_str *blk_id;
  struct arg_end *end;
} api_get_blk_args;

static int fn_api_get_blk(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_get_blk_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_get_blk_args.end, argv[0]);
    return -1;
  }

  res_block_t *blk = res_block_new();
  if (blk == NULL) {
    return -1;
  }

  nerrors = get_block_by_id(&ctx, api_get_blk_args.blk_id->sval[0], blk);
  if (nerrors == 0) {
    if (blk->is_error) {
      printf("Get block API response: %s\n", blk->u.error->msg);
    } else {
      switch (blk->u.blk->payload_type) {
        case CORE_BLOCK_PAYLOAD_TRANSACTION:
          printf("it's a transaction block\n");
          core_block_print(blk->u.blk, 0);
          break;
        case CORE_BLOCK_PAYLOAD_INDEXATION:
          printf("it's an indexation block\n");
          break;
        case CORE_BLOCK_PAYLOAD_MILESTONE:
          printf("it's a milestone block\n");
          core_block_print(blk->u.blk, 0);
          break;
        case CORE_BLOCK_PAYLOAD_RECEIPT:
          printf("it's a receipt block\n");
          break;
        case CORE_BLOCK_PAYLOAD_TREASURY:
          printf("it's a treasury block\n");
          break;
        case CORE_BLOCK_PAYLOAD_TAGGED:
          printf("it's a tagged block\n");
          core_block_print(blk->u.blk, 0);
          break;
        case CORE_BLOCK_PAYLOAD_DEPRECATED_0:
        case CORE_BLOCK_PAYLOAD_DEPRECATED_1:
        case CORE_BLOCK_PAYLOAD_UNKNOWN:
        default:
          printf("unsupported block\n");
          break;
      }
    }
  } else {
    printf("get_block_by_id API error\n");
  }

  res_block_free(blk);
  return nerrors;
}

static void register_api_get_blk() {
  api_get_blk_args.blk_id = arg_str1(NULL, NULL, "<Block ID>", "Block ID");
  api_get_blk_args.end = arg_end(2);
  const esp_console_cmd_t api_get_blk_cmd = {
      .command = "api_get_blk",
      .help = "Get a block from a given block ID",
      .hint = " <Block ID>",
      .func = &fn_api_get_blk,
      .argtable = &api_get_blk_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_get_blk_cmd));
}

/* 'api_blk_meta' command */
static struct {
  struct arg_str *blk_id;
  struct arg_end *end;
} api_blk_meta_args;

static int fn_api_blk_meta(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_blk_meta_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_blk_meta_args.end, argv[0]);
    return -1;
  }

  res_block_meta_t *res = block_meta_new();
  if (!res) {
    printf("Allocate metadata response failed\n");
    return -1;
  } else {
    nerrors = get_block_metadata(&ctx, api_blk_meta_args.blk_id->sval[0], res);
    if (nerrors) {
      printf("get_block_metadata error %d\n", nerrors);
    } else {
      if (res->is_error) {
        printf("%s\n", res->u.error->msg);
      } else {
        print_block_metadata(res, 0);
      }
    }
    block_meta_free(res);
  }
  return nerrors;
}

static void register_api_blk_meta() {
  api_blk_meta_args.blk_id = arg_str1(NULL, NULL, "<Block ID>", "Block ID");
  api_blk_meta_args.end = arg_end(2);
  const esp_console_cmd_t api_blk_meta_cmd = {
      .command = "api_blk_meta",
      .help = "Get metadata from a given block ID",
      .hint = " <Block ID>",
      .func = &fn_api_blk_meta,
      .argtable = &api_blk_meta_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_blk_meta_cmd));
}

/* 'api_get_output' command */
static struct {
  struct arg_str *output_id;
  struct arg_end *end;
} api_get_output_args;

static int fn_api_get_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_get_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_get_output_args.end, argv[0]);
    return -1;
  }

  res_output_t *res = get_output_response_new();
  if (!res) {
    printf("Allocate output response failed\n");
    return -1;
  } else {
    nerrors = get_output(&ctx, api_get_output_args.output_id->sval[0], res);
    if (nerrors != 0) {
      printf("get_output error\n");
      return -1;
    } else {
      if (res->is_error) {
        printf("%s\n", res->u.error->msg);
      } else {
        dump_get_output_response(res, 0);
      }
    }
    get_output_response_free(res);
  }

  return nerrors;
}

static void register_api_get_output() {
  api_get_output_args.output_id = arg_str1(NULL, NULL, "<Output ID>", "An output ID");
  api_get_output_args.end = arg_end(2);
  const esp_console_cmd_t api_get_output_cmd = {
      .command = "api_get_output",
      .help = "Get the output object from a given output ID",
      .hint = " <Output ID>",
      .func = &fn_api_get_output,
      .argtable = &api_get_output_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_get_output_cmd));
}

/* 'api_send_blk' command */
static struct {
  struct arg_str *tag;
  struct arg_str *data;
  struct arg_end *end;
} api_send_tag_args;

static int fn_api_send_tagged_data_str(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_send_tag_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_send_tag_args.end, argv[0]);
    return -1;
  }

  const char *tag = api_send_tag_args.tag->sval[0];
  const char *data = api_send_tag_args.data->sval[0];

  // send indexaction payload
  res_send_block_t res = {};
  nerrors = send_tagged_data_block(&ctx, 2, (byte_t *)tag, strlen(tag), (byte_t *)data, strlen(data), &res);
  if (nerrors != 0) {
    printf("send_tagged_data_block error\n");
  } else {
    if (res.is_error) {
      printf("%s\n", res.u.error->msg);
      res_err_free(res.u.error);
    } else {
      printf("Block ID: %s\n", res.u.blk_id);
    }
  }
  return nerrors;
}

static void register_api_send_tagged_data_str() {
  api_send_tag_args.tag = arg_str1(NULL, NULL, "<Tag>", "Tag");
  api_send_tag_args.data = arg_str1(NULL, NULL, "<Data>", "Tagged Data");
  api_send_tag_args.end = arg_end(3);
  const esp_console_cmd_t api_send_tag_cmd = {
      .command = "api_send_tagged_str",
      .help = "Send out tagged data string to the Tangle",
      .hint = " <Tag> <Data>",
      .func = &fn_api_send_tagged_data_str,
      .argtable = &api_send_tag_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_send_tag_cmd));
}

void register_restful_commands() {
  // restful api's
  register_api_node_info();
  register_api_tips();
  register_api_get_blk();
  register_api_blk_meta();
  register_api_get_output();
  register_api_send_tagged_data_str();
}

void set_resftul_node_endpoint() {
  strcpy(ctx.host, NODE_HOST);
  ctx.port = NODE_PORT;
  ctx.use_tls = NODE_USE_TLS;
}
