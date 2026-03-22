#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID               "WIFI_SSID"
#define WIFI_PASS               "WIFI_PASS"
#define LM35_CONTROL_PIN        GPIO_NUM_33
#define FIREBASE_URL            "YOURFIREBASE_URL/sensor_data.json"
#define SENSOR_READ_INTERVAL_MS 5000
#define TEMP_CALIBRATION_OFFSET 9.5         // Adjust if readings are consistently off

static const char *TAG = "IOT_APP";

// Wi-Fi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Wi-Fi Initialization
void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi Init Finished.");
}

// GPIO Initialization for LM35 Control
void gpio_init_lm35_control(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LM35_CONTROL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_LOGI(TAG, "GPIO33 initialized for LM35 control");
}

// Turn ON/OFF LM35 Sensor Power
void lm35_power_control(bool enable) {
    if (enable) {
        gpio_set_level(LM35_CONTROL_PIN, 1);
        ESP_LOGI(TAG, "LM35 Power: ON");
    } else {
        gpio_set_level(LM35_CONTROL_PIN, 0);
        ESP_LOGI(TAG, "LM35 Power: OFF");
    }
}

void upload_sensor_data(float temperature, int raw_adc) {
    // Create a JSON object with sensor data
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature_c", temperature);
    cJSON_AddNumberToObject(root, "raw_adc", (int)raw_adc);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddStringToObject(root, "status", "active");
    char *json_string = cJSON_PrintUnformatted(root);

    // Configure the HTTP client
    esp_http_client_config_t config = {
        .url = FIREBASE_URL,
        .method = HTTP_METHOD_PATCH,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set headers and body
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    // Perform the request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Successfully sent to Firebase. Status: %d", status_code);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    // Cleanup memory
    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(json_string);
}

void app_main(void) {
    // Initialize NVS (Required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Start Wi-Fi
    wifi_init_sta();

    // Initialize GPIO for LM35 control
    gpio_init_lm35_control();

    // Initialize ADC
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Configure Channel (ADC1_CHANNEL_6 -> GPIO34)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,      // Better linearity for 0-3.3V range
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));

    // Turn ON LM35 sensor
    lm35_power_control(true);

    while (1) {
        int raw_val;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw_val);

        // Convert raw to mV (3.3V max at 12-bit = 4095)
        float milliVolts = (raw_val * 3300.0) / 4095.0;

        // LM35 Formula: Temperature(°C) = mV / 10
        float tempC = (milliVolts / 10.0) + TEMP_CALIBRATION_OFFSET;

        ESP_LOGI("LM35", "Raw: %d, Temp: %.2f °C", raw_val, tempC);
        upload_sensor_data(tempC, raw_val);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}