#pragma once
/* NOSTROMO over-the-air flashing.
 *
 * Call nostromo_ota_start() once the board is on Wi-Fi. It serves, on port 80:
 *   GET  /nostromo/version  {"version":"<app version>"}; NOSTROMO writes the commit into
 *                           version.txt at build time, so do not set PROJECT_VER yourself.
 *   POST /nostromo/ota      {"url":"http://192.168.77.1:8090/nostromo/ota/...","sha256":"..."}:
 *                           downloads the image into the free OTA slot, checks its SHA-256,
 *                           and restarts on it.
 * Starting also marks the running firmware as good, which cancels the bootloader's rollback:
 * a new image that crashes before reaching this call is rolled back automatically.
 */
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the endpoints on `server`, or starts a server on port 80 when it is NULL. */
esp_err_t nostromo_ota_start(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
