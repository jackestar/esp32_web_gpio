#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

#if __has_include("credentials.h")
#include "credentials.h"
#endif

#ifndef CREDENTIALS_H
#define WIFI_SSID "default_ssid"
#define WIFI_PASS "default_pass"
#endif

static const char *TAG = "GPIO_WEB";

// --- GPIO State Tracking ---
typedef struct {
    int pin;
    int is_output;
} gpio_state_t;

// List of standard usable GPIOs on ESP32
gpio_state_t gpios[] = {
    {2, 0}, {4, 0}, {5, 0}, {12, 0}, {13, 0}, {14, 0}, {15, 0},
    {16, 0}, {17, 0}, {18, 0}, {19, 0}, {21, 0}, {22, 0}, {23, 0},
    {25, 0}, {26, 0}, {27, 0}, {32, 0}, {33, 0}
};
const int num_gpios = sizeof(gpios) / sizeof(gpios[0]);

void init_gpios(void) {
    for (int i = 0; i < num_gpios; i++) {
        gpio_reset_pin(gpios[i].pin);
        gpio_set_direction(gpios[i].pin, GPIO_MODE_INPUT);
    }
}

void init_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

// --- HTTP URI Handlers ---

// Handler to get states of all GPIOs
esp_err_t state_handler(httpd_req_t *req) {
    char buf[1024];
    int offset = 0;
    
    offset += snprintf(buf + offset, sizeof(buf) - offset, "[");
    for (int i = 0; i < num_gpios; i++) {
        int val = gpio_get_level(gpios[i].pin);
        offset += snprintf(buf + offset, sizeof(buf) - offset, 
                           "{\"pin\":%d,\"dir\":\"%s\",\"val\":%d}%s",
                           gpios[i].pin,
                           gpios[i].is_output ? "out" : "in",
                           val,
                           (i == num_gpios - 1) ? "" : ",");
    }
    snprintf(buf + offset, sizeof(buf) - offset, "]");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// Handler to set GPIO Mode IO
esp_err_t mode_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char pin_str[10], mode_str[10];
        if (httpd_query_key_value(query, "pin", pin_str, sizeof(pin_str)) == ESP_OK &&
            httpd_query_key_value(query, "mode", mode_str, sizeof(mode_str)) == ESP_OK) {
            
            int pin = atoi(pin_str);
            for (int i = 0; i < num_gpios; i++) {
                if (gpios[i].pin == pin) {
                    if (strcmp(mode_str, "out") == 0) {
                        gpio_reset_pin(pin);
                        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT); // IN_OUT allows us to read the pin back
                        gpio_set_level(pin, 0); // Default low
                        gpios[i].is_output = 1;
                    } else {
                        gpio_reset_pin(pin);
                        gpio_set_direction(pin, GPIO_MODE_INPUT);
                        gpios[i].is_output = 0;
                    }
                    break;
                }
            }
        }
    }
    httpd_resp_sendstr(req, "Mode Updated");
    return ESP_OK;
}

// Handler to set GPIO Level
esp_err_t level_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char pin_str[10], level_str[10];
        if (httpd_query_key_value(query, "pin", pin_str, sizeof(pin_str)) == ESP_OK &&
            httpd_query_key_value(query, "level", level_str, sizeof(level_str)) == ESP_OK) {
            
            int pin = atoi(pin_str);
            int level = atoi(level_str);
            gpio_set_level(pin, level);
        }
    }
    httpd_resp_sendstr(req, "Level Updated");
    return ESP_OK;
}

// Handler to serve static files (HTML, CSS, JS)
esp_err_t file_server_handler(httpd_req_t *req) {
    char filepath[128] = "/spiffs";
    if (strcmp(req->uri, "/") == 0) {
        strcat(filepath, "/index.html");
    } else {
        strcat(filepath, req->uri);
    }

    if (strstr(filepath, ".css")) httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js")) httpd_resp_set_type(req, "application/javascript");
    else httpd_resp_set_type(req, "text/html");

    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        httpd_resp_sendstr_chunk(req, line);
    }
    fclose(f);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// --- Wi-Fi Setup ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
}

void wifi_init() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    
    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// --- Main Server ---
void start_webserver() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t state_uri = { .uri = "/api/state", .method = HTTP_GET, .handler = state_handler };
        httpd_register_uri_handler(server, &state_uri);

        httpd_uri_t mode_uri = { .uri = "/api/mode", .method = HTTP_GET, .handler = mode_handler };
        httpd_register_uri_handler(server, &mode_uri);

        httpd_uri_t level_uri = { .uri = "/api/level", .method = HTTP_GET, .handler = level_handler };
        httpd_register_uri_handler(server, &level_uri);

        // Wildcard must be the last URI handler mapped
        httpd_uri_t file_uri = { .uri = "/*", .method = HTTP_GET, .handler = file_server_handler };
        httpd_register_uri_handler(server, &file_uri);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_gpios();
    init_spiffs();
    wifi_init();
    start_webserver();
}