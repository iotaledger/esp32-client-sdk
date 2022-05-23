// Copyright 2022 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "argtable3/argtable3.h"
#include "cli_restful.h"
#include "client/api/restful/get_message.h"
#include "client/api/restful/get_message_children.h"
#include "client/api/restful/get_message_metadata.h"
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

/* 'api_get_msg' command */
static struct {
  struct arg_str *msg_id;
  struct arg_end *end;
} api_get_msg_args;

static int fn_api_get_msg(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&api_get_msg_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, api_get_msg_args.end, argv[0]);
    return -1;
  }

  res_message_t *msg = res_message_new();
  if (msg == NULL) {
    return -1;
  }

  nerrors = get_message_by_id(&ctx, api_get_msg_args.msg_id->sval[0], msg);
  if (nerrors == 0) {
    if (msg->is_error) {
      printf("Get message   API response: %s\n", msg->u.error->msg);
    } else {
      switch (msg->u.msg->payload_type) {
        case CORE_MESSAGE_PAYLOAD_TRANSACTION:
          printf("it's a transaction message\n");
          core_message_print(msg->u.msg, 0);
          break;
        case CORE_MESSAGE_PAYLOAD_INDEXATION:
          printf("it's an indexation message\n");
          break;
        case CORE_MESSAGE_PAYLOAD_MILESTONE:
          printf("it's a milestone message\n");
          core_message_print(msg->u.msg, 0);
          break;
        case CORE_MESSAGE_PAYLOAD_RECEIPT:
          printf("it's a receipt message\n");
          break;
        case CORE_MESSAGE_PAYLOAD_TREASURY:
          printf("it's a treasury message\n");
          break;
        case CORE_MESSAGE_PAYLOAD_TAGGED:
          printf("it's a tagged message\n");
          core_message_print(msg->u.msg, 0);
          break;
        case CORE_MESSAGE_PAYLOAD_DEPRECATED_0:
        case CORE_MESSAGE_PAYLOAD_DEPRECATED_1:
        case CORE_MESSAGE_PAYLOAD_UNKNOWN:
        default:
          printf("unsupported message\n");
          break;
      }
    }
  } else {
    printf("get_message_by_id API error\n");
  }

  res_message_free(msg);
  return nerrors;
}

static void register_api_get_msg() {
  api_get_msg_args.msg_id = arg_str1(NULL, NULL, "<Message ID>", "Message ID");
  api_get_msg_args.end = arg_end(2);
  const esp_console_cmd_t api_get_msg_cmd = {
      .command = "api_get_msg",
      .help = "Get a message from a given message ID",
      .hint = " <Message ID>",
      .func = &fn_api_get_msg,
      .argtable = &api_get_msg_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_get_msg_cmd));
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

  res_msg_meta_t *res = msg_meta_new();
  if (!res) {
    printf("Allocate metadata response failed\n");
    return -1;
  } else {
    nerrors = get_message_metadata(&ctx, api_msg_meta_args.msg_id->sval[0], res);
    if (nerrors) {
      printf("get_message_metadata error %d\n", nerrors);
    } else {
      if (res->is_error) {
        printf("%s\n", res->u.error->msg);
      } else {
        print_message_metadata(res, 0);
      }
    }
    msg_meta_free(res);
  }
  return nerrors;
}

static void register_api_msg_meta() {
  api_msg_meta_args.msg_id = arg_str1(NULL, NULL, "<Message ID>", "Message ID");
  api_msg_meta_args.end = arg_end(2);
  const esp_console_cmd_t api_msg_meta_cmd = {
      .command = "api_msg_meta",
      .help = "Get metadata from a given message ID",
      .hint = " <Message ID>",
      .func = &fn_api_msg_meta,
      .argtable = &api_msg_meta_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&api_msg_meta_cmd));
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

  res_msg_children_t *res = res_msg_children_new();
  if (!res) {
    printf("Allocate message children response failed\n");
    return -1;
  } else {
    nerrors = get_message_children(&ctx, api_msg_children_args.msg_id->sval[0], res);
    if (nerrors) {
      printf("get_message_children error %d\n", nerrors);
    } else {
      if (res->is_error) {
        printf("Err: %s\n", res->u.error->msg);
      } else {
        print_message_children(res, 0);
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

/* 'api_send_msg' command */
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
  res_send_message_t res = {};
  nerrors = send_tagged_data_message(&ctx, 2, (byte_t *)tag, strlen(tag), (byte_t *)data, strlen(data), &res);
  if (nerrors != 0) {
    printf("send_tagged_data_message error\n");
  } else {
    if (res.is_error) {
      printf("%s\n", res.u.error->msg);
      res_err_free(res.u.error);
    } else {
      printf("Message ID: %s\n", res.u.msg_id);
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
  register_api_get_msg();
  register_api_msg_meta();
  register_api_msg_children();
  register_api_get_output();
  register_api_send_tagged_data_str();
}

void set_resftul_node_endpoint() {
  strcpy(ctx.host, NODE_HOST);
  ctx.port = NODE_PORT;
  ctx.use_tls = NODE_USE_TLS;
}