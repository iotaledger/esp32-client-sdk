#[[
Copyright 2020 IOTA Stiftung
SPDX-License-Identifier: Apache-2.0
]]

if(CONFIG_IOTA_UNIT_TESTS)
  idf_component_register(SRCS "../test/test_main.c" INCLUDE_DIRS ".")
else()
  # TODO : Add cli_sensor.c
  idf_component_register(SRCS "main.c" "cli_restful.c" "cli_system.c" "cli_wallet.c" "cli_node_events" INCLUDE_DIRS
                         ".")
endif()
