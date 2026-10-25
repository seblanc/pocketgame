#include "settings.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define NS "pg"

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void settings_load_audio(int *vol, bool *mute)
{
    *vol = 70;
    *mute = false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) {
        *vol = v;
    }
    if (nvs_get_u8(h, "mute", &v) == ESP_OK) {
        *mute = v != 0;
    }
    nvs_close(h);
}

void settings_save_audio(int vol, bool mute)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "vol", (uint8_t)vol);
    nvs_set_u8(h, "mute", mute ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool settings_get_wifi(char *ssid, char *pass)
{
    ssid[0] = pass[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = SSID_MAX + 1;
    bool ok = nvs_get_str(h, "ssid", ssid, &n) == ESP_OK && ssid[0];
    n = PASS_MAX + 1;
    if (nvs_get_str(h, "pass", pass, &n) != ESP_OK) {
        pass[0] = '\0';
    }
    nvs_close(h);
    return ok;
}

void settings_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

void settings_forget_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    nvs_commit(h);
    nvs_close(h);
}
