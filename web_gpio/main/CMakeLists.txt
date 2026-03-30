idf_component_register(SRCS "web_gpio.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server fatfs spiffs esp_wifi esp_event nvs_flash esp_netif)

spiffs_create_partition_image(storage ../web_assets FLASH_IN_PROJECT)