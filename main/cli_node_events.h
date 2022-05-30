// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#ifndef __EVENTS_API_H__
#define __EVENTS_API_H__

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
