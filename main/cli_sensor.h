// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * @brief Initialize on board temperature sensor
 */
void init_tempsensor();

/**
 * @brief Get current temperature from sensor
 */
float get_temp();

/**
 * @brief Register sensor cli commands
 */
void register_sensor_commands();
