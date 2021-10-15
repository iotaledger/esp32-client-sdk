# Copyright 2021 IOTA Stiftung
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
  SRCS
  "utf8proc/utf8proc.c"
  "utf8proc/utf8proc.h"
  INCLUDE_DIRS
  "utf8proc")

add_definitions(-DUTF8PROC_INSTALL=OFF)