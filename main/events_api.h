// Copyright 2021 IOTA Stiftung
// SPDX-License-Identifier: Apache-2.0

#ifndef __EVENTS_API_H__
#define __EVENTS_API_H__

#define EVENTS_HOST "mqtt.lb-0.h.chrysalis-devnet.iota.cafe"
#define EVENTS_PORT 1883
#define EVENTS_CLIENT_ID "iota_test_123"
#define EVENTS_KEEP_ALIVE 60

/**
 * @brief Subscribe and receive data from node events api
 */
int node_events(int event_select);

#endif
