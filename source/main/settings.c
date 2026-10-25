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

int settings_load_volume(void)
{
    int vol = 70;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return vol;
    }
    uint8_t v;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) {
        vol = v;
    }
    nvs_close(h);
    return vol;
}

void settings_save_volume(int vol)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "vol", (uint8_t)vol);
    nvs_erase_key(h, "mute"); /* left over from builds that had a mute switch */
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
