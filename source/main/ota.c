#include "ota.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nostromo_ota.h"

#define RELEASE_URL "https://api.github.com/repos/" GITHUB_REPO "/releases/latest"
#define REPO_URL "https://api.github.com/repos/" GITHUB_REPO
#define ASSET_NAME "pocketgame.bin"
#define URL_MAX 256
#define HTTP_TIMEOUT_MS 15000
#define MAX_REDIRECTS 6
#define CHUNK 4096
#define REPLY_MAX (256 * 1024) /* read at most this much of the release JSON */
#define WIN_LEN 2048           /* the release JSON is scanned through this window */
#define WIN_KEEP 512           /* longer than any "key":"value" pair it looks for */
/* the start of an image: header, first segment header, then the app description */
#define HEAD_LEN (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ota_view_t s_v;            /* under s_mux */
static char s_asset_url[URL_MAX]; /* under s_mux: the firmware of the last checked release */

typedef struct {
    char url[URL_MAX];
    bool github;
} job_t;

const char *ota_state_name(ota_state_t s)
{
    static const char *const N[] = {"idle", "checking", "downloading", "rebooting", "failed"};
    return N[s];
}

static const char *result_name(upd_result_t r)
{
    static const char *const N[] = {"none", "newer", "same", "older", "error"};
    return N[r];
}

void ota_get_view(ota_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    *v = s_v;
    portEXIT_CRITICAL(&s_mux);
}

static bool busy(ota_state_t s) { return s == OTA_CHECKING || s == OTA_DOWNLOADING || s == OTA_REBOOTING; }

/* takes the updater for a new job: false if one runs */
static bool claim(ota_state_t s)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = !busy(s_v.state);
    if (ok) {
        s_v.state = s;
        s_v.progress = 0;
        s_v.reason[0] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
    if (ok) {
        app_ota_changed();
    }
    return ok;
}

static void set_progress(int pct)
{
    portENTER_CRITICAL(&s_mux);
    s_v.progress = pct;
    portEXIT_CRITICAL(&s_mux);
    app_ota_changed();
}

static const char *slot_name(const esp_partition_t *p) { return p ? p->label : "-"; }

/* ---- boot ---- */

static const char *img_state_name(esp_ota_img_states_t s)
{
    switch (s) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

void ota_boot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (bad) {
        app_out("OTA rollback: %s did not start, back on %s", bad->label, slot_name(run));
    }
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    /* "untracked": flashed over USB, so no OTA record yet (and nothing to roll back to) */
    bool tracked = esp_ota_get_state_partition(run, &st) == ESP_OK;
    const char *was = tracked ? img_state_name(st) : "untracked";
    /* this build came up (display, buses, settings): keep it */
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    bool valid = err == ESP_OK || !tracked || st == ESP_OTA_IMG_VALID || st == ESP_OTA_IMG_UNDEFINED;
    app_out("OTA boot slot=%s state=%s was=%s next=%s rollback=on", slot_name(run), valid ? "valid" : "unconfirmed",
            was, slot_name(esp_ota_get_next_update_partition(NULL)));
}

void ota_print_status(void)
{
    ota_view_t v;
    ota_get_view(&v);
    app_out("OTA status state=%s running=%s next=%s fw=%s progress=%d reason=%s latest=%s check=%s",
            ota_state_name(v.state), slot_name(esp_ota_get_running_partition()),
            slot_name(esp_ota_get_next_update_partition(NULL)), FW_VERSION, v.progress, v.reason[0] ? v.reason : "-",
            v.latest[0] ? v.latest : "-", result_name(v.check));
}

/* ---- HTTP(S) ---- */

static esp_http_client_handle_t new_client(const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach, /* https: the public CA bundle */
        .buffer_size = 2048,                        /* GitHub's redirect headers are long */
        .buffer_size_tx = 2048,                     /* so are the redirected URLs */
        .user_agent = "pocketgame/" FW_VERSION,     /* GitHub refuses requests without one */
        .keep_alive_enable = false,
    };
    return esp_http_client_init(&cfg);
}

/* Sends the GET and reads the headers, following redirects (release files on GitHub
 * move to another host). Returns the status code, -1 when the server could not be
 * reached; *len is the body length (<= 0: unknown). */
static int http_open(esp_http_client_handle_t c, int *len)
{
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        if (esp_http_client_open(c, 0) != ESP_OK) {
            return -1;
        }
        *len = (int)esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            return status;
        }
        if (esp_http_client_set_redirection(c) != ESP_OK) {
            return status;
        }
        esp_http_client_close(c);
    }
    return -1;
}

/* ---- download and flash ---- */

static void fail(const char *reason)
{
    portENTER_CRITICAL(&s_mux);
    s_v.state = OTA_FAILED;
    strlcpy(s_v.reason, reason, sizeof(s_v.reason));
    portEXIT_CRITICAL(&s_mux);
    app_out("OTA FAIL reason=%s (still running %s)", reason, FW_VERSION);
    app_ota_changed();
}

/* NULL if head (HEAD_LEN bytes) starts a PocketGame app for this chip, else why not */
static const char *check_head(const uint8_t *head)
{
    const esp_image_header_t *h = (const esp_image_header_t *)head;
    const esp_app_desc_t *d =
        (const esp_app_desc_t *)(head + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (h->magic != ESP_IMAGE_HEADER_MAGIC) {
        return "not_firmware";
    }
    if (h->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        return "wrong_chip";
    }
    if (d->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return "not_app";
    }
    if (strncmp(d->project_name, esp_app_get_description()->project_name, sizeof(d->project_name)) != 0) {
        return "wrong_project";
    }
    char ver[sizeof(d->version) + 1];
    strlcpy(ver, d->version, sizeof(ver));
    app_out("OTA image project=%.32s build=%s", d->project_name, ver);
    return NULL;
}

static void ota_task(void *arg)
{
    job_t *job = arg;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    uint8_t *buf = malloc(CHUNK);
    static uint8_t head[HEAD_LEN]; /* one job at a time */
    char why[24];
    const char *reason = NULL;
    esp_ota_handle_t h = 0;
    bool begun = false;
    int len = 0, total = 0, head_n = 0, last_pct = -1;

    esp_http_client_handle_t c = target && buf ? new_client(job->url) : NULL;
    int status = c ? http_open(c, &len) : -1;
    if (!target) {
        reason = "no_slot";
    } else if (!buf || !c) {
        reason = "no_memory";
    } else if (status < 0) {
        reason = "connect";
    } else if (status != 200) {
        snprintf(why, sizeof(why), "http_%d", status);
        reason = why;
    } else if (len > 0 && (size_t)len > target->size) {
        reason = "too_large";
    }
    if (!reason) {
        app_out("OTA download bytes=%d slot=%s", len, target->label);
    }
    while (!reason && (len <= 0 || total < len)) {
        int n = esp_http_client_read(c, (char *)buf, CHUNK);
        if (n < 0 || (n == 0 && len > 0)) {
            reason = "download";
            break;
        }
        if (n == 0) {
            break; /* end of a body of unknown length */
        }
        total += n;
        const uint8_t *rest = buf;
        if (!begun) {
            /* collect the image's head and check it before touching the flash */
            int take = n < (int)HEAD_LEN - head_n ? n : (int)HEAD_LEN - head_n;
            memcpy(head + head_n, buf, take);
            head_n += take;
            if (head[0] != ESP_IMAGE_HEADER_MAGIC) {
                reason = "not_firmware";
                break;
            }
            if (head_n < (int)HEAD_LEN) {
                continue;
            }
            if ((reason = check_head(head)) != NULL) {
                break;
            }
            if (esp_ota_begin(target, len > 0 ? (size_t)len : OTA_SIZE_UNKNOWN, &h) != ESP_OK) {
                reason = "flash";
                break;
            }
            begun = true;
            if (esp_ota_write(h, head, HEAD_LEN) != ESP_OK) {
                reason = "flash";
                break;
            }
            rest = buf + take;
            n -= take;
        }
        if (n > 0 && esp_ota_write(h, rest, n) != ESP_OK) {
            reason = "flash";
            break;
        }
        int pct = len > 0 ? (int)((int64_t)total * 100 / len) : 0;
        if (len > 0 && pct / 10 != last_pct / 10) {
            app_out("OTA progress pct=%d bytes=%d", pct, total);
        }
        if (pct != last_pct) {
            last_pct = pct;
            set_progress(pct);
        }
    }
    if (!reason && !begun) {
        reason = "not_firmware"; /* shorter than an image's head */
    }
    if (!reason) {
        begun = false; /* esp_ota_end frees the handle either way */
        esp_err_t err = esp_ota_end(h);
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            reason = "invalid_image";
        } else if (err != ESP_OK) {
            reason = "flash";
        } else if (esp_ota_set_boot_partition(target) != ESP_OK) {
            reason = "boot_select";
        }
    }
    if (begun) {
        esp_ota_abort(h);
    }
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    free(buf);
    free(job);

    if (reason) {
        fail(reason);
    } else {
        portENTER_CRITICAL(&s_mux);
        s_v.state = OTA_REBOOTING;
        s_v.progress = 100;
        portEXIT_CRITICAL(&s_mux);
        app_ota_changed();
        app_out("OTA done slot=%s bytes=%d: restarting", target->label, total);
        vTaskDelay(pdMS_TO_TICKS(1500)); /* the screen shows it, the console line goes out */
        esp_restart();
    }
    vTaskDelete(NULL);
}

static bool http_url(const char *url)
{
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

/* NULL when the download started, else the error word */
static const char *start_download(const char *url, const char *src, bool github)
{
    if (!http_url(url) || strlen(url) >= URL_MAX || strchr(url, ' ')) {
        return "url";
    }
    job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return "memory";
    }
    strlcpy(job->url, url, sizeof(job->url));
    job->github = github;
    if (!claim(OTA_DOWNLOADING)) {
        free(job);
        return "busy";
    }
    portENTER_CRITICAL(&s_mux);
    s_v.from_github = github;
    portEXIT_CRITICAL(&s_mux);
    app_out("OTA start url=%s src=%s", url, src);
    if (xTaskCreate(ota_task, "ota", 8192, job, 4, NULL) != pdPASS) {
        free(job);
        fail("no_memory");
    }
    return NULL;
}

static void print_start_err(const char *err)
{
    if (strcmp(err, "url") == 0) {
        app_out("ERR url (ota <http://... | https://...>)");
    } else if (strcmp(err, "busy") == 0) {
        app_out("ERR busy (an update is running)");
    } else {
        app_out("ERR %s", err);
    }
}

bool ota_start_url(const char *url, const char *src)
{
    const char *err = start_download(url, src, false);
    if (err) {
        print_start_err(err);
    }
    return !err;
}

bool ota_install_latest(const char *src)
{
    char url[URL_MAX];
    portENTER_CRITICAL(&s_mux);
    upd_result_t r = s_v.check;
    bool asset = s_v.has_asset;
    strlcpy(url, s_asset_url, sizeof(url));
    portEXIT_CRITICAL(&s_mux);
    if (r != UPD_NEWER) {
        app_out("ERR no newer release (update check first)");
        return false;
    }
    if (!asset) {
        app_out("ERR release has no " ASSET_NAME);
        return false;
    }
    const char *err = start_download(url, src, true);
    if (err) {
        print_start_err(err);
    }
    return !err;
}

/* ---- GitHub release check ---- */

/* the string value of "key" at or after from (a flat search); false if none fits */
static const char *json_str(const char *from, const char *key, char *out, size_t n)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(from, pat);
    if (!p) {
        return NULL;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p++ != ':') {
        return NULL;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p++ != '"') {
        return NULL;
    }
    const char *e = strchr(p, '"');
    if (!e || (size_t)(e - p) >= n) {
        return NULL;
    }
    memcpy(out, p, e - p);
    out[e - p] = '\0';
    return e + 1;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* "v1.2-2026-11" -> 1, 2, 2026, 11 (missing parts 0); false if there is no MAJOR.MINOR */
static bool parse_version(const char *s, int v[4])
{
    memset(v, 0, 4 * sizeof(int));
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    int n = 0;
    if (sscanf(s, "%d.%d%n", &v[0], &v[1], &n) != 2) {
        return false;
    }
    sscanf(s + n, "-%d-%d", &v[2], &v[3]);
    return true;
}

static int compare_versions(const int a[4], const int b[4])
{
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static void check_done(upd_result_t r, const char *latest, const char *asset_url, const char *err)
{
    portENTER_CRITICAL(&s_mux);
    s_v.state = OTA_IDLE;
    s_v.check = r;
    strlcpy(s_v.latest, latest ? latest : "", sizeof(s_v.latest));
    strlcpy(s_v.check_err, err ? err : "", sizeof(s_v.check_err));
    s_v.has_asset = asset_url && asset_url[0];
    strlcpy(s_asset_url, asset_url ? asset_url : "", sizeof(s_asset_url));
    portEXIT_CRITICAL(&s_mux);
    if (r == UPD_ERROR && strcmp(err, "repo_private") == 0) {
        app_out("UPDATE ERR %s reason=" OTA_REPO_PRIVATE_TEXT, err);
    } else if (r == UPD_ERROR && strcmp(err, "no_release") == 0) {
        app_out("UPDATE ERR %s reason=" OTA_NO_RELEASE_TEXT, err);
    } else if (r == UPD_ERROR) {
        app_out("UPDATE ERR %s", err);
    } else {
        app_out("UPDATE result=%s latest=%s current=%s asset=%s", result_name(r), latest, FW_VERSION,
                asset_url && asset_url[0] ? "yes" : "no");
    }
    app_ota_changed();
}

/* scans win for the tag and the firmware URL (keeps the first tag; pocketgame.bin wins
 * over the first .bin); true once both are final */
static bool scan_release(const char *win, char *tag, size_t tag_n, char *best, size_t best_n)
{
    char url[URL_MAX];
    if (!tag[0]) {
        json_str(win, "tag_name", tag, tag_n);
    }
    for (const char *p = win; (p = json_str(p, "browser_download_url", url, sizeof(url))) != NULL;) {
        if (ends_with(url, "/" ASSET_NAME)) {
            strlcpy(best, url, best_n);
            break;
        }
        if (!best[0] && ends_with(url, ".bin")) {
            strlcpy(best, url, best_n);
        }
    }
    return tag[0] && ends_with(best, "/" ASSET_NAME);
}

static void heap_report(const char *what)
{
    app_out("UPDATE heap %s free=%u largest=%u", what, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void check_task(void *arg)
{
    /* the release JSON is streamed through a small window: the internal RAM left next
     * to Wi-Fi and the frame buffer has no room for the whole reply plus TLS */
    char *win = malloc(WIN_LEN + 1);
    char why[24], tag[24] = "", best[URL_MAX] = "";
    const char *err = NULL;
    int len = 0;

    esp_http_client_handle_t c = win ? new_client(RELEASE_URL) : NULL;
    if (c) {
        esp_http_client_set_header(c, "Accept", "application/vnd.github+json");
    }
    int status = c ? http_open(c, &len) : -1;
    if (!win || !c) {
        heap_report(!win ? "buffer" : "client");
        err = "no_memory";
    } else if (status < 0) {
        heap_report("connect");
        err = "connect";
    } else if (status == 404) {
        err = "not_found"; /* no release yet, or the repository is private: asked below */
    } else if (status == 403 || status == 429) {
        err = "rate_limited";
    } else if (status != 200) {
        snprintf(why, sizeof(why), "http_%d", status);
        err = why;
    } else {
        int have = 0, total = 0, n = 0;
        while (total < REPLY_MAX && (n = esp_http_client_read(c, win + have, WIN_LEN - have)) > 0) {
            have += n;
            total += n;
            win[have] = '\0';
            if (scan_release(win, tag, sizeof(tag), best, sizeof(best))) {
                break;
            }
            /* keep the tail: a "key":"value" pair cut at the window's end is whole next time */
            if (have > WIN_KEEP) {
                memmove(win, win + have - WIN_KEEP, WIN_KEEP);
                have = WIN_KEEP;
            }
        }
        if (n < 0 && !tag[0]) {
            err = "read";
        }
    }
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    if (err && strcmp(err, "not_found") == 0) {
        /* the releases API says 404 for both; the repository itself only when it is
         * private (or gone) */
        esp_http_client_handle_t r = new_client(REPO_URL);
        int rs = r ? http_open(r, &len) : -1;
        app_out("UPDATE repo http=%d", rs);
        err = rs == 404 ? "repo_private" : rs == 200 ? "no_release" : "not_found";
        if (r) {
            esp_http_client_close(r);
            esp_http_client_cleanup(r);
        }
    }

    int latest[4] = {0}, current[4] = {0};
    if (!err && !tag[0]) {
        err = "bad_reply";
    } else if (!err && !parse_version(tag, latest)) {
        err = "bad_tag";
    }
    if (!err) {
        parse_version(FW_VERSION, current);
        int cmp = compare_versions(latest, current);
        check_done(cmp > 0 ? UPD_NEWER : cmp == 0 ? UPD_SAME : UPD_OLDER, tag, best, NULL);
    } else {
        check_done(UPD_ERROR, NULL, NULL, err);
    }
    free(win);
    vTaskDelete(NULL);
}

bool ota_check(const char *src)
{
    app_out("UPDATE check repo=" GITHUB_REPO " src=%s", src);
    if (!claim(OTA_CHECKING)) {
        app_out("UPDATE ERR busy");
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    s_v.check = UPD_NONE;
    portEXIT_CRITICAL(&s_mux);
    if (net_state() != WIFI_CONNECTED) {
        check_done(UPD_ERROR, NULL, NULL, "no_wifi");
        return false;
    }
    /* TLS needs a deep stack */
    if (xTaskCreate(check_task, "update", 8192, NULL, 4, NULL) != pdPASS) {
        heap_report("task");
        check_done(UPD_ERROR, NULL, NULL, "no_memory");
        return false;
    }
    return true;
}

/* ---- HTTP endpoints ---- */

static esp_err_t h_status(httpd_req_t *r)
{
    ota_view_t v;
    ota_get_view(&v);
    char buf[360];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"progress\":%d,\"reason\":\"%s\",\"running\":\"%s\",\"next\":\"%s\","
             "\"fw\":\"%s\",\"check\":\"%s\",\"latest\":\"%s\",\"repo\":\"" GITHUB_REPO "\"}",
             ota_state_name(v.state), v.progress, v.reason, slot_name(esp_ota_get_running_partition()),
             slot_name(esp_ota_get_next_update_partition(NULL)), FW_VERSION, result_name(v.check), v.latest);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, buf);
}

/* POST /ota with {"url":"http://..."} (or the bare URL as the body) */
static esp_err_t h_post(httpd_req_t *r)
{
    char body[URL_MAX + 32], url[URL_MAX];
    int len = r->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "expected {\"url\":\"http://...\"}");
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
    char *s = body;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '{') {
        if (!json_str(s, "url", url, sizeof(url))) {
            url[0] = '\0';
        }
    } else {
        strlcpy(url, s, sizeof(url));
        size_t n = strlen(url);
        while (n && isspace((unsigned char)url[n - 1])) {
            url[--n] = '\0';
        }
    }
    const char *err = start_download(url, "http", false);
    httpd_resp_set_type(r, "application/json");
    if (err) {
        httpd_resp_set_status(r, strcmp(err, "busy") == 0 ? "409 Conflict" : "400 Bad Request");
        char out[48];
        snprintf(out, sizeof(out), "{\"accepted\":false,\"error\":\"%s\"}", err);
        return httpd_resp_sendstr(r, out);
    }
    httpd_resp_set_status(r, "202 Accepted");
    return httpd_resp_sendstr(r, "{\"accepted\":true}");
}

void ota_http_register(httpd_handle_t srv)
{
    static const httpd_uri_t URIS[] = {
        {.uri = "/ota/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/ota", .method = HTTP_POST, .handler = h_post},
    };
    for (size_t i = 0; i < sizeof(URIS) / sizeof(URIS[0]); i++) {
        httpd_register_uri_handler(srv, &URIS[i]);
    }
    /* NOSTROMO's flashing contract (GET /nostromo/version, POST /nostromo/ota) */
    if (nostromo_ota_start(srv) != ESP_OK) {
        app_out("OTA nostromo endpoints failed");
    }
}
