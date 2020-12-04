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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// #include "common/queue.h"

#include "mgos.h"
#include "mgos_rpc.h"
#include "mgos_jstore.h"
#include "mgos_mqtt.h"
#include "mgos_twinkly.h"

#define JSON_PATH                 "twinkly.json"
#define METHOD_GESTALT            "gestalt"
#define METHOD_LOGIN              "login"
#define METHOD_LOGOUT             "logout"
#define METHOD_VERIFY             "verify"
#define METHOD_STATUS             "status"
#define METHOD_SUMMARY            "summary"
#define METHOD_DEVICE_NAME        "device_name"
#define METHOD_LED_MODE           "led/mode"
#define METHOD_LED_CONFIG         "led/config"
#define METHOD_LED_DRIVER_PARAMS  "led/driver_params"
#define METHOD_LED_RESET          "led/reset"
#define METHOD_LED_OUT_BRIGHTNESS "led/out/brightness"
#define METHOD_LED_MODE_CONFIG    "led/movie/config"
#define METHOD_FW_VERSION         "fw/version"
#define METHOD_TIMER              "timer"
#define METHOD_NETWORK_SCAN       "network/scan"
#define METHOD_NETWORK_STATUS     "network/status"
#define METHOD_MQTT_CONFIG        "mqtt/config"

static int s_devices_number = 0;
static bool s_cloud_connected = false;

static void
        twinkly_device_request(struct async_ctx* device, char* method, const char* post_data, tw_cb_t cb, void* arg);
static void twinkly_login_request(struct async_ctx* device);
static void twinkly_verify_request(struct async_ctx* device, char* data);

struct cb_ctx {
    tw_cb_t cb;
    void* arg;
    void* userdata;
};

static struct async_ctx* twinkly_device_new(struct mg_str ip) {
    struct async_ctx* device = calloc(1, sizeof(struct async_ctx));
    if (!device)
        return NULL;
    memset(device, 0, sizeof(struct async_ctx));
    device->ip = mg_strdup(ip);
    return device;
}

static void twinkly_device_free(struct async_ctx* device) {
    LOG(LL_DEBUG, ("%s %.*s", __func__, device->ip.len, device->ip.p));
    mg_strfree(&device->ip);
    mg_strfree(&device->auth_token);
    free(device);
}

static int jstore_add_device(struct mg_str* ip, struct mg_str json) {
    LOG(LL_DEBUG, ("%s %.*s %.*s", __func__, ip->len, ip->p, json.len, json.p));
    char* mac = NULL;
    if (json_scanf(json.p, json.len, "{mac: %Q}", &mac) == 0) {
        LOG(LL_ERROR, ("Invalid response"));
        return MGOS_TWINKLY_ERROR_RESPONSE;
    }

    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        return MGOS_TWINKLY_ERROR_JSTORE;
    }
    int res = MGOS_TWINKLY_ERROR_OK;
    mgos_jstore_item_hnd_t hnd;
    if (mgos_jstore_item_get(store, MGOS_JSTORE_REF_BY_ID(*ip), NULL, NULL, &hnd, NULL, NULL)) {
        res = MGOS_TWINKLY_ERROR_EXISTS;
        goto clean;
    }
    char* err;
    mgos_jstore_item_add(store, *ip, json, MGOS_JSTORE_OWN_FOREIGN, MGOS_JSTORE_OWN_FOREIGN, NULL, NULL, &err);
    if (err) {
        res = MGOS_TWINKLY_ERROR_JSTORE;
        goto clean;
    }
    if (!mgos_jstore_save(store, JSON_PATH, NULL))
        res = MGOS_TWINKLY_ERROR_JSTORE;
clean:
    mgos_jstore_free(store);
    free(err);
    return res;
}

static int jstore_remove_device(struct mg_str* ip) {
    LOG(LL_DEBUG, ("%s %.*s", __func__, ip->len, ip->p));

    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        return MGOS_TWINKLY_ERROR_JSTORE;
    }
    int res = MGOS_TWINKLY_ERROR_OK;
    mgos_jstore_item_hnd_t hnd;
    if (!mgos_jstore_item_get(store, MGOS_JSTORE_REF_BY_ID(*ip), NULL, NULL, &hnd, NULL, NULL)) {
        res = MGOS_TWINKLY_ERROR_EXISTS;
        goto clean;
    }
    if (!mgos_jstore_item_remove(store, MGOS_JSTORE_REF_BY_ID(*ip), NULL)) {
        res = MGOS_TWINKLY_ERROR_JSTORE;
        goto clean;
    }
    if (!mgos_jstore_save(store, JSON_PATH, NULL))
        res = MGOS_TWINKLY_ERROR_JSTORE;
clean:
    mgos_jstore_free(store);
    return res;
}

// HTTP
static void ev_handler(struct mg_connection* c, int ev, void* p, void* user_data) {
    struct cb_ctx* cc = user_data;
    switch (ev) {
        case MG_EV_CONNECT: {
            int err = *(int*) p;
            if (err) {
                // Calling user callback now
                if (cc && cc->cb)
                    cc->cb(NULL, cc->arg); // twinkly_add_cb(hm, cb_ctx)
                free(cc);
                c->user_data = NULL;
            } else {
                mg_set_timer(c, mg_time() + MGOS_TWINKLY_HTTP_TIMEOUT_S);
            }
            break;
        };
        case MG_EV_HTTP_REPLY: {
            struct http_message* hm = (struct http_message*) p;
            c->flags |= MG_F_CLOSE_IMMEDIATELY;
            LOG(LL_DEBUG, ("%.*s", hm->body.len, hm->body.p));
            // Calling user callback now
            if (cc && cc->cb)
                cc->cb(hm, cc->arg); // twinkly_add_cb(hm, cb_ctx)
            free(cc);
            c->user_data = NULL;
            break;
        };
        case MG_EV_TIMER: {
            c->flags |= MG_F_CLOSE_IMMEDIATELY;
            char addr[32];
            mg_sock_addr_to_str(&c->sa, (char*) addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
            LOG(LL_INFO, ("%s - timed out, closing", addr));
            // Calling user callback now
            if (cc && cc->cb)
                cc->cb(NULL, cc->arg); // twinkly_add_cb(hm, cb_ctx)
            free(cc);
            c->user_data = NULL;
            break;
        };
        case MG_EV_CLOSE: {
        };
    }
}

static void http_request(
        struct mg_str* ip,
        char* method,
        void* user_data,
        const char* extra_headers,
        const char* post_data) {
    char* url = NULL;
    mg_asprintf(&url, 0, "http://%.*s/xled/v1/%s", ip->len, ip->p, method);
    LOG(LL_DEBUG,
        ("%s %s\r\n%s\r\n%s",
         __func__,
         url,
         extra_headers ? extra_headers : "[no extra headers]",
         post_data ? post_data : "[no data]"));
    struct mg_connection* c = mg_connect_http(mgos_get_mgr(), ev_handler, user_data, url, extra_headers, post_data);
    free(url);
    (void) c;
}

static int status_to_int(char* status) {
    if (!status || (strcmp("offline", status) == 0))
        return 0;
    else
        return 1;
}

static int mode_to_int(char* mode) {
    if (!mode || (strcmp("off", mode) == 0))
        return 0;
    else
        return 1;
}

static void twinkly_verify_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    if (!arg) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    }
    struct http_message* hm = data;
    struct async_ctx* device = arg;
    if (!data) {
        LOG(LL_ERROR, ("%s error", __func__));
        goto exit;
    }
    LOG(LL_DEBUG, ("resp %ld: %.*s", (long) hm->resp_code, hm->body.len, hm->body.p));
    if (hm->resp_code != 200) {
        LOG(LL_ERROR, ("verify error %ld", (long) hm->resp_code));
        hm = NULL;
        goto exit;
    }
    struct mg_str json = hm->body;
    int code = 0;
    if (json_scanf(json.p, json.len, "{code: %d}", &code) == 1) {
        if (code != 1000) {
            LOG(LL_ERROR, ("verify error, code %ld", (long) code));
            hm = NULL;
            goto exit;
        }
        twinkly_device_request(device, device->method, device->post_data, device->cb, device->arg);
    }
    // clearing async context
    // device->method = NULL;
    // device->post_data = NULL;
    // device->cb = NULL;
    return;
exit:
    device->hm = hm;
    if (device->cb)
        device->cb(hm, device); // method_cb
}

static void twinkly_logout_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    if (!arg) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    }
    struct http_message* hm = data;
    struct async_ctx* device = arg;
    if (!data) {
        LOG(LL_ERROR, ("%s error", __func__));
        goto exit;
    }
    LOG(LL_INFO, ("resp %ld: %.*s", (long) hm->resp_code, hm->body.len, hm->body.p));
    if (hm->resp_code != 200) {
        LOG(LL_ERROR, ("logout error %ld", (long) hm->resp_code));
        hm = NULL;
        goto exit;
    }
    struct mg_str json = hm->body;
    int code = 0;
    if (json_scanf(json.p, json.len, "{code: %d}", &code) == 1) {
        if (code != 1000) {
            LOG(LL_ERROR, ("logout error, code %ld", (long) code));
            hm = NULL;
            goto exit;
        }
    }
    // clearing async context
    // device->method = NULL;
    // device->post_data = NULL;
    // device->cb = NULL;
exit:
    device->hm = hm;
    if (device->cb)
        device->cb(hm, device); // method_cb
}

static void twinkly_login_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    if (!arg) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    }
    struct http_message* hm = data;
    struct async_ctx* device = arg;
    if (!data) {
        LOG(LL_ERROR, ("%s error", __func__));
        goto exit;
    }
    LOG(LL_INFO, ("resp %ld: %.*s", (long) hm->resp_code, hm->body.len, hm->body.p));
    if (hm->resp_code != 200) {
        LOG(LL_ERROR, ("login error %ld", (long) hm->resp_code));
        hm = NULL;
        goto exit;
    }
    struct mg_str json = hm->body;
    int code = 0;
    if (json_scanf(json.p, json.len, "{code: %d}", &code) == 1) {
        if (code != 1000) {
            LOG(LL_ERROR, ("login error, code %ld", (long) code));
            hm = NULL;
            goto exit;
        }
    }
    char* at = NULL;
    if (json_scanf(json.p, json.len, "{authentication_token: %Q}", &at) == 1) {
        mg_strfree(&device->auth_token);
        device->auth_token = mg_strdup(mg_mk_str(at));
    }
    char* cr = NULL;
    if (json_scanf(json.p, json.len, "{challenge-response: %Q}", &cr) == 1) {
        char* data = NULL;
        mg_asprintf(&data, 0, "{\"challenge-response\": \"%s\"}", cr);
        twinkly_verify_request(device, data); // we dont have to wait here
        free(data);
        return;
    }
exit:
    device->hm = hm;
    if (device->cb)
        device->cb(hm, device); // method_cb
}

static void twinkly_device_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct http_message* hm = data;
    struct async_ctx* device = arg;
    if (!data)
        goto exit;
    LOG(LL_DEBUG, ("resp %ld: %.*s", (long) hm->resp_code, hm->body.len, hm->body.p));
    if (hm->resp_code == 401) {
        twinkly_login_request(device);
        return;
    } else if (hm->resp_code == 200) {
        // ALL OK
    } else {
        // not expect json answer here
    }
exit:
    device->hm = hm;
    if (device->cb)
        device->cb(hm, device); // method_cb
    else
        twinkly_device_free(device);
}

static void
        twinkly_device_request(struct async_ctx* device, char* method, const char* post_data, tw_cb_t cb, void* arg) {
    LOG(LL_DEBUG, ("%s %s",__func__, method));
    // async context fill
    device->method = method;
    device->post_data = post_data;
    device->cb = cb;
    device->arg = arg;
    if (device->auth_token.p) {
        struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
        if (!cadd) {
            if (cb)
                cb(NULL, device);
            return;
        };
        cadd->cb = twinkly_device_cb;
        cadd->arg = device; // ev_handler: cc->cb(hm, cc->arg);

        char* headers = NULL;
        mg_asprintf(
                &headers,
                0,
                "X-Auth-Token: %.*s\r\nContent-Type: application/json\r\n",
                device->auth_token.len,
                device->auth_token.p);
        http_request(&device->ip, method, cadd, headers, post_data);
        free(headers);
    } else {
        // No token yet, need to login first
        twinkly_login_request(device);
    }
}

static void twinkly_login_request(struct async_ctx* device) {
    LOG(LL_DEBUG, (__func__));
    struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
    if (!cadd) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    };
    cadd->cb = twinkly_login_cb;
    cadd->arg = device;

    http_request(
            &device->ip,
            METHOD_LOGIN,
            cadd,
            "Content-Type: application/json\r\n",
            "{\"challenge\": \"sT/f+2z8HvEkP3topBoJL83eWNZZtpAb1v+tcfY4E3s=\"}");
}

static void twinkly_logout_request(struct async_ctx* device) {
    LOG(LL_DEBUG, (__func__));
    struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
    if (!cadd) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    };
    cadd->cb = twinkly_logout_cb;
    cadd->arg = device;
    char* headers = NULL;
    mg_asprintf(
            &headers,
            0,
            "X-Auth-Token: %.*s\r\nContent-Type: application/json\r\n",
            device->auth_token.len,
            device->auth_token.p);
    http_request(&device->ip, METHOD_LOGOUT, cadd, headers, "{}");
    free(headers);
}

static void twinkly_verify_request(struct async_ctx* device, char* data) {
    LOG(LL_DEBUG, (__func__));
    struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
    if (!cadd) {
        LOG(LL_ERROR, ("%s invalid args", __func__));
        return;
    };
    cadd->cb = twinkly_verify_cb;
    cadd->arg = device;
    char* headers = NULL;
    mg_asprintf(
            &headers,
            0,
            "X-Auth-Token: %.*s\r\nContent-Type: application/json\r\n",
            device->auth_token.len,
            device->auth_token.p);
    http_request(&device->ip, METHOD_VERIFY, cadd, headers, data);
    free(headers);
}

static void mqtt_handler(
        struct mg_connection* c,
        const char* topic,
        int topic_len,
        const char* msg,
        int msg_len,
        void* userdata) {
    mgos_twinkly_ev_data_t data;
    data.index = (int) userdata;
    LOG(LL_DEBUG, ("%ld %.*s: %.*s", (long) userdata, topic_len, topic, msg_len, msg));
    char* str;
    str = NULL;
    if (json_scanf(msg, msg_len, "{appstatus: %Q}", &str) == 1) {
        data.value = mode_to_int(str);
        mgos_event_trigger(MGOS_TWINKLY_EV_MODE, &data);
    }
    str = NULL;
    if (json_scanf(msg, msg_len, "{status: %Q}", &str) == 1) {
        data.value = status_to_int(str);
        mgos_event_trigger(MGOS_TWINKLY_EV_STATUS, &data);
    }
    int brightness = 0;
    if (json_scanf(msg, msg_len, "{brightness: %d}", &brightness) == 1) {
        data.value = brightness;
        mgos_event_trigger(MGOS_TWINKLY_EV_BRIGHTNESS, &data);
    }
    // if (strstr(topic, "appstatus")) {
    // } else if (strstr(topic, "status")) {
    // } else if (strstr(topic, "params")) {
    // }
}

static char* get_family(struct mg_str json) {
    char* f = NULL;
    if (json_scanf(json.p, json.len, "{fw_family: %Q}", &f) == 1)
        return f;
    else
        return "A";
}

static bool is_gen1(char* family) {
    // A B C D E H J K
    return !strcmp(family, "A") || !strcmp(family, "B") || !strcmp(family, "C") || !strcmp(family, "D") ||
           !strcmp(family, "E") || !strcmp(family, "H") || !strcmp(family, "J") || !strcmp(family, "K");
}

static bool is_gen2(char* family) {
    // F G L M N P
    return !strcmp(family, "F") || !strcmp(family, "G") || !strcmp(family, "L") || !strcmp(family, "M") ||
           !strcmp(family, "N") || !strcmp(family, "P");
}

static bool twinkly_subscribe_cb(int idx, const struct mg_str* ip, const struct mg_str* json) {
    LOG(LL_DEBUG, ("%s %.*s", __func__, ip->len, ip->p));
    bool result = false;
    if (!is_gen1(get_family(*json)))
        return result;
    char* str;
    if (json_scanf(json->p, json->len, "{mac: %Q}", &str) == 1) {
        unsigned char a[6];
        int last = -1;
        int rc = sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%n", a + 0, a + 1, a + 2, a + 3, a + 4, a + 5, &last);
        if (rc != 6 || 17 != last) {
            LOG(LL_ERROR, ("Invalid mac address: %s", str));
            return false;
        }
        char* topic = NULL;
        // appstatus, status, params
        mg_asprintf(&topic, 0, "xled/+/%02X%02X%02X%02X%02X%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
        mgos_mqtt_sub(topic, mqtt_handler, (void*) idx); /* Subscribe */
        free(topic);
        result = true;
    }
    return result;
}

static bool jstore_iterate_cb(
        struct mgos_jstore* store,
        int idx,
        mgos_jstore_item_hnd_t hnd,
        const struct mg_str* id,
        const struct mg_str* data,
        void* userdata) {
    LOG(LL_DEBUG, ("%s %.*s", __func__, id->len, id->p));
    mgos_twinkly_iterate_cb_t cb = userdata;
    s_devices_number++;
    return cb ? cb(idx, id, data) : true;
    (void) store;
    (void) idx;
    (void) hnd;
    (void) data;
}

static bool list_rpc_cb(
        struct mgos_jstore* store,
        int idx,
        mgos_jstore_item_hnd_t hnd,
        const struct mg_str* id,
        const struct mg_str* data,
        void* userdata) {
    LOG(LL_DEBUG, ("%s %ld %p %p %p", __func__, (long) idx, id, data, userdata));
    if (!id || !data || !userdata)
        return false;
    struct json_out* out = (struct json_out*) userdata;
    if (idx)
        json_printf(out, ",");
    json_printf(out, "{%.*Q: %.*Q}", id->len, id->p, data->len, data->p);
    return true;
    (void) store;
    (void) idx;
    (void) hnd;
    (void) data;
}

static void add_rpc_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %ld %p", __func__, (long) data, arg));
    struct mg_rpc_request_info* ri = arg;
    struct mbuf fb;
    int err = (int) data;
    switch (err) {
        case MGOS_TWINKLY_ERROR_OK: {
            struct json_out out = JSON_OUT_MBUF(&fb);
            mbuf_init(&fb, 50);
            json_printf(&out, "{success: %B}", true);
            mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
            mbuf_free(&fb);
            break;
        }
        case MGOS_TWINKLY_ERROR_MEM: {
            mg_rpc_send_errorf(ri, err, "out of memory");
            break;
        }
        case MGOS_TWINKLY_ERROR_EXISTS: {
            mg_rpc_send_errorf(ri, err, "already exists");
            break;
        }
        case MGOS_TWINKLY_ERROR_TIMEOUT: {
            mg_rpc_send_errorf(ri, err, "device connection timed out");
            break;
        }
        case MGOS_TWINKLY_ERROR_RESPONSE: {
            mg_rpc_send_errorf(ri, err, "invalid response, not a twinkly device");
            break;
        }
        case MGOS_TWINKLY_ERROR_JSTORE: {
            mg_rpc_send_errorf(ri, err, "internal storage error");
            break;
        }
        default: {
            mg_rpc_send_errorf(ri, err, "unknown error %ld", (long) err);
            break;
        }
    }
    ri = NULL;
}

static void set_mqtt_config_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct http_message* hm = data;
    struct async_ctx* device = arg;
    if (hm)
        LOG(LL_DEBUG, ("%s %ld %.*s", __func__, (long) hm->resp_code, hm->body.len, hm->body.p));
    else
        LOG(LL_ERROR, ("%s error", __func__));
    if (device)
        free((void*) device->post_data);
    twinkly_device_free(device);
}

static void twinkly_set_mqtt_config(struct mg_str* ip, const char* server) {
    LOG(LL_DEBUG, ("%s %.*s %s", __func__, ip->len, ip->p, server ? server : "[no server]"));
    unsigned int port = 1883;
    char host[200];
    int len = 0;
    int num = sscanf(server, "%[^ :]:%u%n", (char*) &host, &port, &len);
    if (num > 0) {
        char* data = NULL;
        if (num == 1)
            mg_asprintf(&data, 0, "{\"broker_host\": \"%s\"}", host);
        else
            mg_asprintf(&data, 0, "{\"broker_host\": \"%s\",\"broker_port\": %ld}", host, (unsigned long) port);
        twinkly_device_request(
                twinkly_device_new(*ip), METHOD_MQTT_CONFIG, (const char*) data, set_mqtt_config_cb, NULL);
    }
}

static void twinkly_add_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct http_message* hm = data;
    if (!arg)
        return;
    struct cb_ctx* cc = arg;
    int res;
    struct mg_str* ip = cc->userdata;

    res = hm ? jstore_add_device(ip, hm->body) : MGOS_TWINKLY_ERROR_TIMEOUT;
    if (hm) {
        mgos_sys_config_set_twinkly_config_changed(true);
        mgos_sys_config_save(&mgos_sys_config, false, NULL);
        mgos_event_trigger(MGOS_TWINKLY_EV_ADDED, NULL);
        // For gen1 device only (current gen2 fw = 2.5.6)
        if (is_gen1(get_family(hm->body))){
            twinkly_set_mqtt_config(ip, mgos_sys_config_get_mqtt_server());
            twinkly_subscribe_cb(0, ip, &hm->body);
        }
    }
    if (cc && cc->cb)
        cc->cb((void*) res, cc->arg); // add_rpc_cb
    mg_strfree(ip);
    free(ip);
    free(cc);
}

static void info_rpc_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct mg_rpc_request_info* ri = arg;
    struct http_message* hm = data;
    if (hm) {
        struct mg_str json = hm->body;
        char* mac = NULL;
        if (json_scanf(json.p, json.len, "{mac: %Q}", &mac) == 0) {
            LOG(LL_ERROR, ("Invalid response"));
            mg_rpc_send_responsef(ri, "{code: %d, message: %Q}", 1, "Invalid response");
        } else {
            mg_rpc_send_responsef(ri, "%.*s", json.len, json.p);
        }
    } else {
        mg_rpc_send_responsef(ri, "{code: %d, message: %Q}", 2, "Connection timed out");
    }
    ri = NULL;
}

static void twinkly_info_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    // data is struct http_message*
    if (!arg)
        return;
    struct cb_ctx* cc = arg;
    // int res;
    struct mg_str* ip = cc->userdata;

    LOG(LL_INFO, (__func__));
    if (cc && cc->cb)
        cc->cb(data, cc->arg); // info_rpc_cb
    mg_strfree(ip);
    free(ip);
    free(cc);
}

void mgos_twinkly_add(struct mg_str* ip, tw_cb_t cb, void* arg) {
    LOG(LL_DEBUG, (__func__));
    struct cb_ctx* cc = calloc(1, sizeof(struct cb_ctx));
    if (!cc) {
        if (cb)
            cb((void*) MGOS_TWINKLY_ERROR_MEM, arg);
        return;
    };
    cc->cb = cb;
    cc->arg = arg;
    cc->userdata = ip;
    struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
    if (!cadd) {
        if (cb)
            cb((void*) MGOS_TWINKLY_ERROR_MEM, arg);
        free(cc);
        return;
    };
    cadd->cb = twinkly_add_cb;
    cadd->arg = cc;
    http_request(ip, METHOD_GESTALT, cadd, NULL, NULL);
}

static void remove_rpc_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %ld %p", __func__, (long) data, arg));
    struct mg_rpc_request_info* ri = arg;
    struct mbuf fb;
    int err = (int) data;
    switch (err) {
        case MGOS_TWINKLY_ERROR_OK: {
            struct json_out out = JSON_OUT_MBUF(&fb);
            mbuf_init(&fb, 50);
            json_printf(&out, "{success: %B}", true);
            mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
            mbuf_free(&fb);
            break;
        }
        case MGOS_TWINKLY_ERROR_MEM: {
            mg_rpc_send_errorf(ri, err, "out of memory");
            break;
        }
        case MGOS_TWINKLY_ERROR_EXISTS: {
            mg_rpc_send_errorf(ri, err, "not exists");
            break;
        }
        case MGOS_TWINKLY_ERROR_TIMEOUT: {
            mg_rpc_send_errorf(ri, err, "device connection timed out");
            break;
        }
        case MGOS_TWINKLY_ERROR_RESPONSE: {
            mg_rpc_send_errorf(ri, err, "invalid response, not a twinkly device");
            break;
        }
        case MGOS_TWINKLY_ERROR_JSTORE: {
            mg_rpc_send_errorf(ri, err, "internal storage error");
            break;
        }
        default: {
            mg_rpc_send_errorf(ri, err, "unknown error %ld", (long) err);
            break;
        }
    }
    ri = NULL;
}

void mgos_twinkly_remove(struct mg_str* ip, tw_cb_t cb, void* arg) {
    LOG(LL_DEBUG, (__func__));
    int res = jstore_remove_device(ip);
    mgos_sys_config_set_twinkly_config_changed(true);
    mgos_sys_config_save(&mgos_sys_config, false, NULL);
    // Restoring mqtt config
    twinkly_set_mqtt_config(ip, "mqtt.twinkly.com");
    mgos_event_trigger(MGOS_TWINKLY_EV_REMOVED, NULL);
    if (cb)
        cb((void*) res, arg);
}

void mgos_twinkly_info(struct mg_str* ip, tw_cb_t cb, void* arg) {
    LOG(LL_DEBUG, (__func__));
    struct cb_ctx* cc = calloc(1, sizeof(struct cb_ctx));
    if (!cc) {
        if (cb)
            cb((void*) MGOS_TWINKLY_ERROR_MEM, arg);
        return;
    };
    cc->cb = cb;
    cc->arg = arg;
    cc->userdata = ip;
    struct cb_ctx* cadd = calloc(1, sizeof(struct cb_ctx));
    if (!cadd) {
        if (cb)
            cb((void*) MGOS_TWINKLY_ERROR_MEM, arg);
        free(cc);
        return;
    };
    cadd->cb = twinkly_info_cb;
    cadd->arg = cc;
    http_request(ip, METHOD_GESTALT, cadd, NULL, NULL);
}

static void call_rpc_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct async_ctx* device = arg;
    struct mg_rpc_request_info* ri = device->arg;
    struct http_message* hm = data;
    if (hm) {
        struct mg_str json = hm->body;
        if (hm->resp_code == 200)
            mg_rpc_send_responsef(ri, "%.*s", json.len, json.p);
        else
            mg_rpc_send_responsef(ri, "{code: %d, message: %Q}", hm->resp_code, json.len, json.p);
    } else {
        mg_rpc_send_responsef(ri, "{code: %d, message: %Q}", 2, "Error connecting device");
    }
    // Always dynamic alloc here
    free(device->method);
    free((char*) device->post_data);
    twinkly_device_free(device);
    ri = NULL;
}

// RPC handlers
static void
        list_handler(struct mg_rpc_request_info* ri, void* cb_arg, struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, (__func__));
    struct mbuf fb;

    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        mg_rpc_send_errorf(ri, 400, "failed to open jstore");
        return;
    };

    struct json_out out = JSON_OUT_MBUF(&fb);
    mbuf_init(&fb, 500);
    json_printf(&out, "[");
    mgos_jstore_iterate(store, list_rpc_cb, &out);
    json_printf(&out, "]");

    mg_rpc_send_responsef(ri, "%.*s", fb.len, fb.buf);
    ri = NULL;

    mgos_jstore_free(store);
    mbuf_free(&fb);

    (void) cb_arg;
    (void) fi;
}

static void
        add_handler(struct mg_rpc_request_info* ri, void* cb_arg, struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("%s %.*s", __func__, args.len, args.p));

    char* ip = NULL;

    json_scanf(args.p, args.len, "{ip: %Q}", &ip);
    struct mg_str* aip = calloc(1, sizeof(struct mg_str));
    *aip = mg_strdup(mg_mk_str(ip));

    if (ip) {
        mgos_twinkly_add(aip, add_rpc_cb, ri);
    } else
        mg_rpc_send_errorf(ri, 400, "IP address is required (a.b.c.d)");

    ri = NULL;

    (void) cb_arg;
    (void) fi;
}

static void
        remove_handler(struct mg_rpc_request_info* ri, void* cb_arg, struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("%s %.*s", __func__, args.len, args.p));

    char* ip = NULL;

    json_scanf(args.p, args.len, "{ip: %Q}", &ip);
    struct mg_str* aip = calloc(1, sizeof(struct mg_str));
    *aip = mg_strdup(mg_mk_str(ip));

    if (ip) {
        mgos_twinkly_remove(aip, remove_rpc_cb, ri);
    } else
        mg_rpc_send_errorf(ri, 400, "IP address is required (a.b.c.d)");

    ri = NULL;

    (void) cb_arg;
    (void) fi;
}

static void
        info_handler(struct mg_rpc_request_info* ri, void* cb_arg, struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("%s %.*s", __func__, args.len, args.p));

    char* ip = NULL;

    json_scanf(args.p, args.len, "{ip: %Q}", &ip);
    struct mg_str* aip = calloc(1, sizeof(struct mg_str));
    *aip = mg_strdup(mg_mk_str(ip));

    if (ip) {
        mgos_twinkly_info(aip, info_rpc_cb, ri);
    } else
        mg_rpc_send_errorf(ri, 400, "IP is required (a.b.c.d)");

    ri = NULL;

    (void) cb_arg;
    (void) fi;
}

static void
        call_handler(struct mg_rpc_request_info* ri, void* cb_arg, struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("%s %.*s", __func__, args.len, args.p));

    char* ip = NULL;
    char* method = NULL;
    json_scanf(args.p, args.len, "{ip: %Q}", &ip);
    json_scanf(args.p, args.len, "{method: %Q}", &method);
    if (ip && method) {
        char* data = NULL;
        json_scanf(args.p, args.len, "{data: %Q}", &data);
        LOG(LL_DEBUG,
            ("%s %s %s %s", __func__, ip ? ip : "[no ip]", method ? method : "[no method]", data ? data : "[no data]"));
        twinkly_device_request(
                twinkly_device_new(mg_mk_str(ip)), strdup(method), data ? strdup(data) : NULL, call_rpc_cb, ri);
    } else
        mg_rpc_send_errorf(ri, 400, "IP and method required");

    ri = NULL;

    (void) cb_arg;
    (void) fi;
}

bool mgos_twinkly_iterate(mgos_twinkly_iterate_cb_t cb) {
    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        return false;
    }
    s_devices_number = 0;
    if (!mgos_jstore_iterate(store, jstore_iterate_cb, (void*) cb)) {
        LOG(LL_ERROR, ("Failed to load data from jstore"));
        return false;
    }
    mgos_jstore_free(store);
    LOG(LL_DEBUG, ("%ld devices iterated", (long) s_devices_number));
    return true;
}

static void cloud_cb(int ev, void* evd, void* arg) {
    struct mgos_cloud_arg* ca = (struct mgos_cloud_arg*) evd;
    switch (ev) {
        case MGOS_EVENT_CLOUD_CONNECTED: {
            LOG(LL_INFO, ("Cloud connected (type %d)", ca->type));
            s_cloud_connected = true;
            break;
        }
        case MGOS_EVENT_CLOUD_DISCONNECTED: {
            LOG(LL_INFO, ("Cloud disconnected (type %d)", ca->type));
            s_cloud_connected = false;
            break;
        }
    }
    (void) arg;
}

int mgos_twinkly_count() {
    mgos_twinkly_iterate(NULL);
    return s_devices_number;
}

static void set_mode_cb(void* data, void* arg) {
    LOG(LL_DEBUG, ("%s %p %p", __func__, data, arg));
    struct async_ctx* device = arg;
    struct http_message* hm = data;
    struct mg_str json = hm->body;
    int code = 0;
    if (json_scanf(json.p, json.len, "{code: %d}", &code) == 1) {
        if (code == 1000) {
            free(device);
        } else {
            LOG(LL_ERROR, ("setmode error, code %ld", (long) code));
            char* data = NULL;
            switch (code) {
                case 1105:
                    data = "{\"mode\":\"movie\"}";
                    break;
                case 1104:
                    data = "{\"mode\":\"effect\"}";
                    break;
                default:
                    data = "{\"mode\":\"off\"}";
            }
            twinkly_device_request(device, METHOD_LED_MODE, data, set_mode_cb, NULL);
        }
        hm = NULL;
    }
}


bool mgos_twinkly_set_mode(int idx, bool mode) {
    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        return false;
    }
    bool res = false;
    mgos_jstore_item_hnd_t hnd;
    struct mg_str ip, json;
    if (mgos_jstore_item_get(store, MGOS_JSTORE_REF_BY_INDEX(idx), &ip, &json, &hnd, NULL, NULL)) {
        char* mode_off = "{\"mode\":\"off\"}";
        char* mode_on = "{\"mode\":\"effect\"}"; // default value
        char* family = get_family(json);
        if (is_gen1(family))
            mode_on = "{\"mode\":\"movie\"}";
        if (is_gen2(family))
            mode_on = "{\"mode\":\"playlist\"}";
        twinkly_device_request(twinkly_device_new(ip), METHOD_LED_MODE, mode ? mode_on : mode_off, set_mode_cb, NULL);
        res = true;
    } else {
        LOG(LL_ERROR, ("Failed to get item %ld from jstore", (long) idx));
    }
    mgos_jstore_free(store);
    return res;
}

bool mgos_twinkly_set_brightness(int idx, int value) {
    struct mgos_jstore* store = mgos_jstore_create(JSON_PATH, NULL);
    if (!store) {
        LOG(LL_ERROR, ("Failed to open jstore %s", JSON_PATH));
        return false;
    }
    bool res = false;
    mgos_jstore_item_hnd_t hnd;
    struct mg_str ip;
    if (mgos_jstore_item_get(store, MGOS_JSTORE_REF_BY_INDEX(idx), &ip, NULL, &hnd, NULL, NULL)) {
        char* data = NULL;
        mg_asprintf(&data, 0, "{\"type\":\"A\",\"value\":%ld}", (long) value);
        twinkly_device_request(
                twinkly_device_new(ip),
                METHOD_LED_OUT_BRIGHTNESS,
                data,
                NULL, // contex auto free
                NULL);
        res = true;
    } else {
        LOG(LL_ERROR, ("Failed to get item %ld from jstore", (long) idx));
    }
    mgos_jstore_free(store);
    return res;
}

// Libarary
bool mgos_twinkly_init(void) {
    if (!mgos_sys_config_get_twinkly_enable())
        return true;
    // MQTT subscribe for gen1
    mgos_twinkly_iterate(twinkly_subscribe_cb);
    mgos_event_add_handler(MGOS_EVENT_CLOUD_CONNECTED, cloud_cb, NULL);
    mgos_event_add_handler(MGOS_EVENT_CLOUD_DISCONNECTED, cloud_cb, NULL);
    if (mgos_sys_config_get_twinkly_rpc_enable()) {
        struct mg_rpc* c = mgos_rpc_get_global();
        mg_rpc_add_handler(c, "Twinkly.List", "{}", list_handler, NULL);
        mg_rpc_add_handler(c, "Twinkly.Add", "{ip:%Q}", add_handler, NULL);
        mg_rpc_add_handler(c, "Twinkly.Remove", "{ip:%Q}", remove_handler, NULL);
        mg_rpc_add_handler(c, "Twinkly.Info", "{ip:%Q}", info_handler, NULL);
        mg_rpc_add_handler(c, "Twinkly.Call", "{ip:%Q, method:%Q, data:%Q}", call_handler, NULL);
    }
    return true;
}

void mgos_twinkly_deinit(void) {
    //
}
