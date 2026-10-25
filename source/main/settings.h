#pragma once
#include <stdbool.h>

/* Settings kept in NVS (namespace "pg"): volume (0 = silent) and the Wi-Fi network. */

#define SSID_MAX 32
#define PASS_MAX 64

void settings_init(void);
int settings_load_volume(void); /* 0..100, default 70 */
void settings_save_volume(int vol);
/* false when no network is saved; ssid holds SSID_MAX + 1, pass PASS_MAX + 1 */
bool settings_get_wifi(char *ssid, char *pass);
void settings_set_wifi(const char *ssid, const char *pass);
void settings_forget_wifi(void);
