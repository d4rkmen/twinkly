/*
 * Copyright 2020 d4rkmen <darkmen@i.ua>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MGOS_TWINKLY_EV_BASE   MGOS_EVENT_BASE('T', 'W', 'K')
#define MGOS_EVENT_GRP_TWINKLY MGOS_TWINKLY_EV_BASE

#define MGOS_TWINKLY_HTTP_TIMEOUT_S 10.0

#define MGOS_TWINKLY_ERROR_OK       0
#define MGOS_TWINKLY_ERROR_EXISTS   1
#define MGOS_TWINKLY_ERROR_TIMEOUT  2
#define MGOS_TWINKLY_ERROR_MEM      3
#define MGOS_TWINKLY_ERROR_RESPONSE 4
#define MGOS_TWINKLY_ERROR_JSTORE   5

enum mgos_twinkly_event {
    MGOS_TWINKLY_EV_INITIALIZED = MGOS_TWINKLY_EV_BASE,
    MGOS_TWINKLY_EV_STATUS,     // online / offline
    MGOS_TWINKLY_EV_MODE,       // on / off
    MGOS_TWINKLY_EV_BRIGHTNESS, // brightness 0-100 %
    MGOS_TWINKLY_EV_ADDED,      // New device added
    MGOS_TWINKLY_EV_REMOVED
};

// Twinkly event data item
typedef struct mgos_twinkly_ev_data {
    int index; // Device index in jstore
    int value; // Value
} mgos_twinkly_ev_data_t;

struct mgos_twinkly_product {
    const char* product_code;
    const char* commercial_name;
    const char* firmware_family;
    const char* device_family;
    const char* led_profile;
    uint16_t led_number;
    const char* default_name;
    const char* layout_type;
    const char* pixel_shape;
    bool mapping_allowed;
    const char* join_fml;
    const char* sync_fml;
    bool bluetooth;
    bool microphone;
    const char* icon;
};

// The callback for twinkly async actions. res - result data for callback,
typedef void (*tw_cb_t)(void* data, void* arg);
// Devices list iteration callback
typedef bool (*mgos_twinkly_iterate_cb_t)(int idx, const struct mg_str* ip, const struct mg_str* json);

struct async_ctx {
    char* method;
    const char* post_data;
    tw_cb_t cb;
    void* arg;
    struct http_message* hm;
    struct mg_str auth_token;
    struct mg_str ip;
};

// Iterate through device s list
bool mgos_twinkly_iterate(mgos_twinkly_iterate_cb_t cb);
// Number of devices in store
int mgos_twinkly_count();
// Turn on / off
bool mgos_twinkly_set_mode(int idx, bool mode);
// Set brightness
bool mgos_twinkly_set_brightness(int idx, int value);
// Get product info by given product code
bool mgos_twinkly_get_product(char* code, struct mgos_twinkly_product** product);

// library
bool mgos_twinkly_init(void);
void mgos_twinkly_deinit(void);

#ifdef __cplusplus
}
#endif
