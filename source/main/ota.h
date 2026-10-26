#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"

/* Over-the-air updates into the two app slots (ota_0 / ota_1, partitions.csv), with the
 * bootloader's rollback: a new image that crashes before ota_boot() marked it valid is
 * replaced by the previous one at the next reset.
 *
 * Two ways in:
 *  - engineering push: "ota <url>" on the console or POST /ota {"url":"..."} on the
 *    board's HTTP server; any http:// or https:// URL (e.g. the Jetson on the NOSTROMO
 *    access point). GET /ota/status gives the state as JSON.
 *  - GitHub: ota_check() reads the latest release of GITHUB_REPO over HTTPS, compares
 *    its tag with FW_VERSION and remembers its firmware asset (a .bin, preferably
 *    pocketgame.bin); ota_install_latest() then downloads and flashes it.
 * The image must be a PocketGame app (ESP image magic and project name). Progress and
 * results go to the console:
 *   OTA start url=.. src=.. | OTA progress pct=n bytes=n | OTA done slot=.. bytes=n
 *   OTA FAIL reason=<word> | UPDATE check repo=.. | UPDATE result=newer|same|older
 *   latest=<tag> current=<fw> asset=yes|no | UPDATE ERR <word>
 * NOSTROMO's own contract (/nostromo/version, /nostromo/ota) is registered next to these. */

#define GITHUB_REPO "seblanc/pocketgame"

typedef enum { OTA_IDLE, OTA_CHECKING, OTA_DOWNLOADING, OTA_REBOOTING, OTA_FAILED } ota_state_t;
typedef enum { UPD_NONE, UPD_NEWER, UPD_SAME, UPD_OLDER, UPD_ERROR } upd_result_t;

/* what the update screen shows */
typedef struct {
    ota_state_t state;
    int progress;        /* 0..100 while downloading */
    char reason[24];     /* why the last download failed */
    upd_result_t check;  /* the last GitHub check */
    char latest[24];     /* its release tag */
    char check_err[24];  /* why it failed (UPD_ERROR) */
    bool has_asset;      /* the release carries a firmware file */
    bool from_github;    /* the download running (or failed) is the GitHub release */
} ota_view_t;

/* At boot: marks the running app valid (cancels a pending rollback) and prints
 * OTA boot slot=ota_N state=valid ... */
void ota_boot(void);
/* adds GET /ota/status, POST /ota and NOSTROMO's contract to the HTTP server */
void ota_http_register(httpd_handle_t srv);

/* starts a download in the background; false (with the ERR line printed) if the URL is
 * not http(s) or an update is already running */
bool ota_start_url(const char *url, const char *src);
/* starts the GitHub check in the background; false (UPDATE ERR ..) if busy or offline */
bool ota_check(const char *src);
/* installs the release found by the last check; false (ERR ..) if there is none newer */
bool ota_install_latest(const char *src);

void ota_get_view(ota_view_t *v);
const char *ota_state_name(ota_state_t s);
/* OTA status state=.. running=.. next=.. fw=.. */
void ota_print_status(void);
