#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Wi-Fi station with credentials kept in NVS, the phone setup hotspot
 * (PocketGame-XXXX, captive page at 192.168.4.1) and the HTTP server on port 80
 * (GET /status, GET / and, during setup, POST /save). */

typedef enum { WIFI_OFF, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED, WIFI_SETUP } wifi_state_t;

/* Starts Wi-Fi and joins the saved network, if any. Needs NVS. */
bool net_init(void);
/* (Re)joins with the saved credentials; ends the setup hotspot if it runs. */
void net_connect(void);
void net_setup_start(void);
void net_setup_stop(void);
wifi_state_t net_state(void);
const char *net_state_name(wifi_state_t s);
/* station IP, "" when not connected */
void net_ip(char *buf, size_t n);
const char *net_ap_name(void);
