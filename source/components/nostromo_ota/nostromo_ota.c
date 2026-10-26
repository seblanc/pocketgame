/* NOSTROMO over-the-air flashing contract: see include/nostromo_ota.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nostromo_ota.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

/* Only the Jetson's access point may hand out firmware. */
#define NOSTROMO_IMAGE_PREFIX "http://192.168.77.1:8090/nostromo/ota/"

static const char *TAG = "nostromo_ota";
static volatile bool s_busy;

typedef struct {
    char url[160];
    char sha256[65];
} ota_job_t;

static esp_err_t version_get(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    char body[160];
    snprintf(body, sizeof body, "{\"version\":\"%s\",\"project\":\"%s\",\"partition\":\"%s\"}",
             app->version, app->project_name, running ? running->label : "");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

/* The value of "key":"..." in a small flat JSON object, copied into out. */
static bool json_string(const char *json, const char *key, char *out, size_t size)
{
    char pattern[24];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *at = strstr(json, pattern);
    if (!at) return false;
    at = strchr(at + strlen(pattern), ':');
    if (!at) return false;
    at = strchr(at, '"');
    if (!at) return false;
    const char *end = strchr(++at, '"');
    if (!end || (size_t)(end - at) >= size) return false;
    memcpy(out, at, end - at);
    out[end - at] = '\0';
    return true;
}

static void ota_task(void *arg)
{
    ota_job_t *job = arg;
    static char buf[4096];
    unsigned char digest[32];
    char text[65];
    int length = 0, total = 0, n = 0;
    bool begun = false;
    esp_err_t err = ESP_FAIL;
    esp_ota_handle_t handle = 0;
    mbedtls_sha256_context sha;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t config = {.url = job->url, .timeout_ms = 15000};
    esp_http_client_handle_t client = esp_http_client_init(&config);

    mbedtls_sha256_init(&sha);
    if (!target || !client || mbedtls_sha256_starts(&sha, 0) != 0) goto done;
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 || length <= 0 || (size_t)length > target->size) {
        ESP_LOGE(TAG, "image unavailable or too large (status %d, %d bytes)", esp_http_client_get_status_code(client), length);
        goto done;
    }
    if (esp_ota_begin(target, length, &handle) != ESP_OK) goto done;
    begun = true;
    while (total < length) {
        n = esp_http_client_read(client, buf, sizeof buf);
        if (n <= 0) {
            ESP_LOGE(TAG, "download stopped after %d of %d bytes", total, length);
            goto done;
        }
        if (mbedtls_sha256_update(&sha, (const unsigned char *)buf, n) != 0) goto done;
        if (esp_ota_write(handle, buf, n) != ESP_OK) goto done;
        total += n;
    }
    if (mbedtls_sha256_finish(&sha, digest) != 0) goto done;
    for (int i = 0; i < 32; i++) snprintf(text + 2 * i, 3, "%02x", digest[i]);
    if (strcmp(text, job->sha256) != 0) {
        ESP_LOGE(TAG, "SHA-256 mismatch: got %s", text);
        goto done;
    }
    begun = false;
    err = esp_ota_end(handle);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(target);

done:
    if (begun) esp_ota_abort(handle);
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    mbedtls_sha256_free(&sha);
    free(job);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "new firmware installed in %s; restarting", target->label);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE(TAG, "update failed; still running the current firmware");
    s_busy = false;
    vTaskDelete(NULL);
}

static esp_err_t ota_post(httpd_req_t *req)
{
    char body[512];
    int got = 0;
    ota_job_t *job;

    if (req->content_len <= 0 || req->content_len >= sizeof body)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected a small JSON body");
    while (got < (int)req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "could not read the body");
        got += n;
    }
    body[got] = '\0';
    if (s_busy) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"error\":\"an update is already running\"}");
    }
    job = calloc(1, sizeof *job);
    if (!job) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    if (!json_string(body, "url", job->url, sizeof job->url) || !json_string(body, "sha256", job->sha256, sizeof job->sha256)
        || strncmp(job->url, NOSTROMO_IMAGE_PREFIX, strlen(NOSTROMO_IMAGE_PREFIX)) != 0 || strlen(job->sha256) != 64) {
        free(job);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected url (from the NOSTROMO access point) and sha256");
    }
    s_busy = true;
    if (xTaskCreate(ota_task, "nostromo_ota", 8192, job, 5, NULL) != pdPASS) {
        free(job);
        s_busy = false;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not start the update");
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}

esp_err_t nostromo_ota_start(httpd_handle_t server)
{
    static const httpd_uri_t version = {.uri = "/nostromo/version", .method = HTTP_GET, .handler = version_get};
    static const httpd_uri_t update = {.uri = "/nostromo/ota", .method = HTTP_POST, .handler = ota_post};
    esp_err_t err = ESP_OK;

    if (!server) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        err = httpd_start(&server, &config);
        if (err != ESP_OK) return err;
    }
    err = httpd_register_uri_handler(server, &version);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &update);
    /* The firmware started and serves the contract: keep it (cancels the bootloader's rollback). */
    esp_ota_mark_app_valid_cancel_rollback();
    return err;
}
