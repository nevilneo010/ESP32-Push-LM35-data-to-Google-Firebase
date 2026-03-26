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

#define WIFI_SSID               "ACTFIBERNET_2A"
#define WIFI_PASS               "SadhaWifi"
#define LM35_CONTROL_PIN        GPIO_NUM_33
#define FIREBASE_URL            "https://esp32-lm35-e7d0d-default-rtdb.asia-southeast1.firebasedatabase.app/sensor_data.json"
#define FIREBASE_CONTROL_URL    "https://esp32-lm35-e7d0d-default-rtdb.asia-southeast1.firebasedatabase.app/control/lm35_power.json"
#define SENSOR_READ_INTERVAL_MS 5000
#define CONTROL_CHECK_INTERVAL_MS 2000
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

// Get LM35 control state from Firebase
// IMPORTANT: Make sure to set a value at /control/lm35_power in Firebase first!
// Set it to "on" to turn LM35 on, "off" to turn it off, or any other string to turn off.
bool get_lm35_control_state(void) {
    // Configure the HTTP client for GET request
    esp_http_client_config_t config = {
        .url = FIREBASE_CONTROL_URL,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
        .timeout_ms = 15000,  // 15 second timeout
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Open connection and fetch headers first
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Fetch headers
    err = esp_http_client_fetch_headers(client);
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to fetch headers: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Firebase GET status: %d", status_code);
    ESP_LOGI(TAG, "Requesting URL: %s", FIREBASE_CONTROL_URL);

    if (status_code == 200) {
        // Get response data
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Firebase control response - Content Length: %d", content_length);

        // Try to read response data
        if (content_length > 0 && content_length < 1024) {
            char *response_data = malloc(content_length + 1);
            if (response_data) {
                ESP_LOGI(TAG, "Starting to read %d bytes of response data", content_length);

                int read_len = esp_http_client_read(client, response_data, content_length);
                ESP_LOGI(TAG, "esp_http_client_read returned: %d", read_len);

                if (read_len == content_length) {
                    response_data[read_len] = '\0';
                    ESP_LOGI(TAG, "Firebase control: %s", response_data);

                    // Check response directly (Firebase returns quoted strings like "on" or "off")
                    bool power_state = false;
                    if (strstr(response_data, "\"on\"") != NULL || strstr(response_data, "\"true\"") != NULL) {
                        power_state = true;
                    }
                    // Any other value (including "off", "false", or null) keeps it off

                    free(response_data);
                    esp_http_client_close(client);
                    esp_http_client_cleanup(client);
                    return power_state;
                } else {
                    ESP_LOGE(TAG, "Failed to read complete response (read %d/%d bytes)", read_len, content_length);
                    free(response_data);
                }
            }
        } else if (content_length == 0) {
            ESP_LOGW(TAG, "Content length is 0 - Firebase path might not exist");
            ESP_LOGW(TAG, "Set /control/lm35_power in Firebase to a string like 'off'");
        }
    } else {
        ESP_LOGW(TAG, "Firebase GET failed with status: %d", status_code);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
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
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));

    // Turn OFF LM35 sensor initially
    lm35_power_control(false);
    bool current_power_state = false;

    TickType_t last_control_check = xTaskGetTickCount();
    TickType_t last_sensor_read = xTaskGetTickCount();

    while (1) {
        TickType_t current_time = xTaskGetTickCount();

        // Check control state every CONTROL_CHECK_INTERVAL_MS
        if ((current_time - last_control_check) * portTICK_PERIOD_MS >= CONTROL_CHECK_INTERVAL_MS) {
            bool new_power_state = get_lm35_control_state();
            ESP_LOGD(TAG, "Control check: current=%s, new=%s", current_power_state ? "ON" : "OFF", new_power_state ? "ON" : "OFF");
            if (new_power_state != current_power_state) {
                lm35_power_control(new_power_state);
                current_power_state = new_power_state;
                ESP_LOGI(TAG, "LM35 power state changed to: %s", new_power_state ? "ON" : "OFF");
            }
            last_control_check = current_time;
        }

        // Read sensor only if powered on and every SENSOR_READ_INTERVAL_MS
        if (current_power_state && (current_time - last_sensor_read) * portTICK_PERIOD_MS >= SENSOR_READ_INTERVAL_MS) {
            int raw_val;
            adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw_val);

            // Convert raw to mV (3.3V max at 12-bit = 4095)
            float milliVolts = (raw_val * 3300.0) / 4095.0;

            // LM35 Formula: Temperature(°C) = mV / 10
            float tempC = (milliVolts / 10.0) + TEMP_CALIBRATION_OFFSET;

            ESP_LOGI("LM35", "Raw: %d, Temp: %.2f °C", raw_val, tempC);
            upload_sensor_data(tempC, raw_val);

            last_sensor_read = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Small delay to prevent busy waiting
    }
}