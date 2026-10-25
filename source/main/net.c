#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "settings.h"

#define AP_IP "192.168.4.1"
#define RETRY_MS 3000
#define ATTEMPT_TIMEOUT_MS 20000
#define FAILS_BEFORE_FAILED 5

typedef enum { CMD_CONNECT, CMD_SETUP_START, CMD_SETUP_STOP, CMD_SETUP_SAVED } net_cmd_t;

static QueueHandle_t s_q;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile wifi_state_t s_state = WIFI_OFF;
static volatile bool s_attempting;
static TickType_t s_attempt_at;
static int s_fails;
static char s_ip[16];
static char s_ap_name[24];
static esp_netif_t *s_sta;

static void set_ip(const char *ip)
{
    portENTER_CRITICAL(&s_mux);
    strlcpy(s_ip, ip, sizeof(s_ip));
    portEXIT_CRITICAL(&s_mux);
}

void net_ip(char *buf, size_t n)
{
    portENTER_CRITICAL(&s_mux);
    strlcpy(buf, s_ip, n);
    portEXIT_CRITICAL(&s_mux);
}

wifi_state_t net_state(void) { return s_state; }
const char *net_ap_name(void) { return s_ap_name; }

const char *net_state_name(wifi_state_t s)
{
    static const char *const N[] = {"off", "connecting", "connected", "failed", "setup"};
    return N[s];
}

static void set_state(wifi_state_t s)
{
    s_state = s;
    app_net_changed();
}

static void send_cmd(net_cmd_t c)
{
    if (s_q) {
        xQueueSend(s_q, &c, pdMS_TO_TICKS(100));
    }
}

void net_connect(void) { send_cmd(CMD_CONNECT); }
void net_setup_start(void) { send_cmd(CMD_SETUP_START); }
void net_setup_stop(void) { send_cmd(CMD_SETUP_STOP); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        if (s_state == WIFI_SETUP || s_state == WIFI_OFF) {
            return;
        }
        if (d->reason == WIFI_REASON_ASSOC_LEAVE && s_state != WIFI_CONNECTED) {
            return; /* our own esp_wifi_disconnect() before a new attempt */
        }
        s_attempting = false;
        if (s_state == WIFI_CONNECTED) {
            app_out("WIFI disconnected reason=%d", d->reason);
            set_ip("");
            set_state(WIFI_CONNECTING);
        } else if (++s_fails == FAILS_BEFORE_FAILED) {
            app_out("WIFI failed reason=%d (still retrying)", d->reason);
            set_state(WIFI_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        set_ip(ip);
        s_attempting = false;
        s_fails = 0;
        app_out("WIFI connected ip=%s", ip);
        set_state(WIFI_CONNECTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        app_out("WIFI setup: a phone joined %s", s_ap_name);
    }
}

static void attempt(void)
{
    s_attempting = true;
    s_attempt_at = xTaskGetTickCount();
    esp_wifi_connect();
}

/* runs on the net task */
static void sta_connect_saved(void)
{
    char ssid[SSID_MAX + 1], pass[PASS_MAX + 1];
    bool have = settings_get_wifi(ssid, pass);
    s_state = have ? WIFI_CONNECTING : WIFI_OFF; /* before the disconnect: its event is ours */
    set_ip("");
    esp_wifi_disconnect();
    if (!have) {
        app_out("WIFI off (no network saved)");
        app_net_changed();
        return;
    }
    wifi_config_t c = {0};
    strlcpy((char *)c.sta.ssid, ssid, sizeof(c.sta.ssid));
    strlcpy((char *)c.sta.password, pass, sizeof(c.sta.password));
    c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    s_fails = 0;
    app_out("WIFI connecting");
    app_net_changed();
    attempt();
}

static void setup_start(void)
{
    if (s_state == WIFI_SETUP) {
        return;
    }
    s_state = WIFI_SETUP;
    s_attempting = false;
    set_ip("");
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_name);
    ap.ap.channel = 6;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 2;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    app_out("WIFI setup ap=%s page=http://" AP_IP "/", s_ap_name);
    app_net_changed();
}

static void setup_stop(void)
{
    if (s_state != WIFI_SETUP) {
        return;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_state = WIFI_OFF;
    app_out("WIFI setup closed");
    sta_connect_saved();
}

static void net_task(void *arg)
{
    for (;;) {
        net_cmd_t cmd;
        if (xQueueReceive(s_q, &cmd, pdMS_TO_TICKS(RETRY_MS)) == pdTRUE) {
            switch (cmd) {
            case CMD_CONNECT:
                if (s_state == WIFI_SETUP) {
                    setup_stop();
                } else {
                    sta_connect_saved();
                }
                break;
            case CMD_SETUP_START:
                setup_start();
                break;
            case CMD_SETUP_SAVED:
                vTaskDelay(pdMS_TO_TICKS(1500)); /* let the phone get the answer page */
                /* fall through */
            case CMD_SETUP_STOP:
                setup_stop();
                break;
            }
            continue;
        }
        bool want = s_state == WIFI_CONNECTING || s_state == WIFI_FAILED;
        if (want && (!s_attempting || xTaskGetTickCount() - s_attempt_at > pdMS_TO_TICKS(ATTEMPT_TIMEOUT_MS))) {
            attempt();
        }
    }
}

/* Captive DNS for the setup hotspot: every A query resolves to the board, so the
 * phone opens the setup page by itself. Silent outside setup. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&a, sizeof(a)) != 0) {
        app_out("WIFI dns failed");
        if (sock >= 0) {
            close(sock);
        }
        vTaskDelete(NULL);
        return;
    }
    static const uint8_t ANSWER[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - sizeof(ANSWER), 0, (struct sockaddr *)&from, &fl);
        if (n < 12 || s_state != WIFI_SETUP || (buf[2] & 0x80) || buf[4] != 0 || buf[5] != 1) {
            continue;
        }
        int p = 12;
        while (p < n && buf[p] && !(buf[p] & 0xC0)) {
            p += buf[p] + 1;
        }
        p += 5; /* root label, QTYPE, QCLASS */
        if (p > n) {
            continue;
        }
        bool type_a = buf[p - 4] == 0 && buf[p - 3] == 1;
        buf[2] = 0x84 | (buf[2] & 0x01); /* response, authoritative, keep RD */
        buf[3] = 0x80;                   /* RA, no error */
        buf[6] = 0;
        buf[7] = type_a ? 1 : 0;
        memset(buf + 8, 0, 4);
        if (type_a) {
            memcpy(buf + p, ANSWER, sizeof(ANSWER));
            p += sizeof(ANSWER);
        }
        sendto(sock, buf, p, 0, (struct sockaddr *)&from, fl);
    }
}

static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>PocketGame</title>"
    "<style>body{background:#080a0e;color:#e2e6ec;font-family:system-ui,sans-serif;margin:0;padding:28px;"
    "max-width:420px}h1{font-weight:300;letter-spacing:2px;font-size:22px;border-bottom:1px solid #303642;"
    "padding-bottom:12px}label{display:block;color:#6e7684;font-size:13px;margin:18px 0 6px}"
    "input{width:100%;box-sizing:border-box;background:none;border:1px solid #303642;border-radius:8px;"
    "color:#e2e6ec;padding:12px;font-size:16px}input:focus{outline:none;border-color:#00d6aa}"
    "button{margin-top:24px;width:100%;padding:12px;background:none;border:1px solid #00d6aa;"
    "border-radius:8px;color:#00d6aa;font-size:16px;letter-spacing:1px}p{color:#6e7684}</style></head><body>";

static const char PAGE_SETUP[] =
    "<h1>POCKETGAME WI-FI</h1><form method=post action=/save>"
    "<label>NETWORK NAME</label><input name=ssid maxlength=32 required autocapitalize=none>"
    "<label>PASSWORD</label><input name=pass type=password maxlength=64>"
    "<button>CONNECT</button></form><p>The board leaves this hotspot and joins your network.</p>"
    "</body></html>";

static const char PAGE_SAVED[] =
    "<h1>SAVED</h1><p>PocketGame is joining your network now. You can close this page and "
    "reconnect your phone to your usual Wi-Fi.</p></body></html>";

static const char PAGE_HOME[] =
    "<h1>POCKETGAME</h1><p>Status: <a style='color:#00d6aa' href=/status>/status</a></p></body></html>";

static esp_err_t h_status(httpd_req_t *r)
{
    char buf[400];
    app_status_json(buf, sizeof(buf));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, buf);
}

static esp_err_t send_page(httpd_req_t *r, const char *body)
{
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(r, PAGE_HEAD);
    httpd_resp_sendstr_chunk(r, body);
    return httpd_resp_sendstr_chunk(r, NULL);
}

static esp_err_t h_root(httpd_req_t *r) { return send_page(r, s_state == WIFI_SETUP ? PAGE_SETUP : PAGE_HOME); }

static void url_decode(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 1 < n; in++) {
        if (*in == '+') {
            out[o++] = ' ';
        } else if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = '\0';
}

static esp_err_t h_save(httpd_req_t *r)
{
    if (s_state != WIFI_SETUP) {
        return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Wi-Fi setup is not running");
    }
    char body[320];
    int len = r->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad form");
    }
    for (int got = 0; got < len;) {
        int n = httpd_req_recv(r, body + got, len - got);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        got += n;
    }
    body[len] = '\0';
    char raw[200], ssid[SSID_MAX + 1], pass[PASS_MAX + 1];
    if (httpd_query_key_value(body, "ssid", raw, sizeof(raw)) != ESP_OK) {
        raw[0] = '\0';
    }
    url_decode(raw, ssid, sizeof(ssid));
    if (httpd_query_key_value(body, "pass", raw, sizeof(raw)) != ESP_OK) {
        raw[0] = '\0';
    }
    url_decode(raw, pass, sizeof(pass));
    if (!ssid[0]) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "network name missing");
    }
    settings_set_wifi(ssid, pass);
    app_out("WIFI saved src=phone");
    esp_err_t err = send_page(r, PAGE_SAVED);
    send_cmd(CMD_SETUP_SAVED);
    return err;
}

/* during setup every unknown URL (the phones' connectivity checks) leads to the page */
static esp_err_t h_404(httpd_req_t *r, httpd_err_code_t code)
{
    if (s_state == WIFI_SETUP) {
        httpd_resp_set_status(r, "302 Found");
        httpd_resp_set_hdr(r, "Location", "http://" AP_IP "/");
        return httpd_resp_send(r, NULL, 0);
    }
    return httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "not found");
}

static void http_start(void)
{
    httpd_handle_t srv;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        app_out("HTTP failed");
        return;
    }
    static const httpd_uri_t URIS[] = {
        {.uri = "/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/", .method = HTTP_GET, .handler = h_root},
        {.uri = "/save", .method = HTTP_POST, .handler = h_save},
    };
    for (size_t i = 0; i < sizeof(URIS) / sizeof(URIS[0]); i++) {
        httpd_register_uri_handler(srv, &URIS[i]);
    }
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, h_404);
}

bool net_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_name, sizeof(s_ap_name), "PocketGame-%02X%02X", mac[4], mac[5]);

    if (esp_netif_init() != ESP_OK) {
        return false;
    }
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }
    s_sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, "pocketgame");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* credentials live in our own NVS keys */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }

    http_start();
    s_q = xQueueCreate(4, sizeof(net_cmd_t));
    xTaskCreate(net_task, "net", 4096, NULL, 5, NULL);
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);
    net_connect();
    return true;
}
