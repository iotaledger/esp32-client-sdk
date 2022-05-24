// Copyright 2022 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#pragma once

#define WALLET_VERSION_MAJOR 0
#define WALLET_VERSION_MINOR 4
#define WALLET_VERSION_MICRO 0

#define VER_STR0(s) #s
#define VER_STR(s) VER_STR0(s)

#define APP_WALLET_VERSION      \
  VER_STR(WALLET_VERSION_MAJOR) \
  "." VER_STR(WALLET_VERSION_MINOR) "." VER_STR(WALLET_VERSION_MICRO)

/**
 * @brief Register system cli commands
 */
void register_system_commands();
