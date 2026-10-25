#pragma once
#include <stdbool.h>

/* Settings kept in NVS (namespace "pg"): volume, mute and the Wi-Fi network. */

#define SSID_MAX 32
#define PASS_MAX 64

void settings_init(void);
void settings_load_audio(int *vol, bool *mute); /* defaults 70, off */
void settings_save_audio(int vol, bool mute);
/* false when no network is saved; ssid holds SSID_MAX + 1, pass PASS_MAX + 1 */
bool settings_get_wifi(char *ssid, char *pass);
void settings_set_wifi(const char *ssid, const char *pass);
void settings_forget_wifi(void);
