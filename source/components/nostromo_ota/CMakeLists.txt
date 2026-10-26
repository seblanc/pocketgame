# NOSTROMO OTA contract (installed by NOSTROMO; see nostromo_ota.c). Needs in sdkconfig.defaults:
#   CONFIG_PARTITION_TABLE_TWO_OTA=y   CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
idf_component_register(SRCS "nostromo_ota.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES esp_http_client app_update esp_app_format mbedtls)
