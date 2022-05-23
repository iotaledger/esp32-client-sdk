// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#ifndef __EVENTS_API_H__
#define __EVENTS_API_H__

#define EVENTS_HOST CONFIG_EVENTS_HOST
#define EVENTS_PORT CONFIG_EVENTS_PORT
#define EVENTS_CLIENT_ID CONFIG_EVENTS_CLIENT_ID
#define EVENTS_KEEP_ALIVE CONFIG_EVENTS_KEEP_ALIVE

/**
 * @brief Subscribe and receive data from node events api
 * @param[in] event_select Bit positions of event_select will define events to be subscribed
 */
int node_events(int event_select);

/**
 * @brief Register node events cli commands
 */
void register_node_events();

#endif
