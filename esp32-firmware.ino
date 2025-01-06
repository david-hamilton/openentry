#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_client.h"

#define RELAY_PIN GPIO_NUM_2 // Pin to control the door lock relay
#define APPROVED_LIST_SIZE 10
#define DEVICE_NAME_MAX_LEN 32
#define API_URL "http://example.com/api/approved_devices"

// List of approved devices
static char approved_devices[APPROVED_LIST_SIZE][DEVICE_NAME_MAX_LEN];
static int approved_device_count = 0;

// Flag to indicate pairing mode
static bool pairing_mode = false;

void open_door() {
    printf("Door unlocked!\n");
    gpio_set_level(RELAY_PIN, 1);
    vTaskDelay(5000 / portTICK_PERIOD_MS); // Keep the door unlocked for 5 seconds
    gpio_set_level(RELAY_PIN, 0);
    printf("Door locked!\n");
}

bool is_device_approved(const char *device_name) {
    for (int i = 0; i < approved_device_count; i++) {
        if (strcmp(approved_devices[i], device_name) == 0) {
            return true;
        }
    }
    return false;
}

void add_approved_device(const char *device_name) {
    if (approved_device_count >= APPROVED_LIST_SIZE) {
        printf("Approved device list is full!\n");
        return;
    }
    strncpy(approved_devices[approved_device_count], device_name, DEVICE_NAME_MAX_LEN - 1);
    approved_devices[approved_device_count][DEVICE_NAME_MAX_LEN - 1] = '\0';
    approved_device_count++;
    printf("Device '%s' added to approved list!\n", device_name);
}

void fetch_approved_devices_from_api() {
    esp_http_client_config_t config = {
        .url = API_URL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        printf("HTTP GET Status = %d\n", status_code);

        if (status_code == 200) {
            char buffer[1024];
            int len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                printf("Received devices: %s\n", buffer);

                // Reset the approved list and parse new devices
                approved_device_count = 0;
                char *token = strtok(buffer, ",");
                while (token != NULL && approved_device_count < APPROVED_LIST_SIZE) {
                    strncpy(approved_devices[approved_device_count], token, DEVICE_NAME_MAX_LEN - 1);
                    approved_devices[approved_device_count][DEVICE_NAME_MAX_LEN - 1] = '\0';
                    approved_device_count++;
                    token = strtok(NULL, ",");
                }
                printf("Updated approved devices list.\n");
            }
        }
    } else {
        printf("HTTP GET request failed: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                char device_name[DEVICE_NAME_MAX_LEN];
                int name_len = esp_ble_resolve_adv_data(
                    param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, (uint8_t *)device_name);
                if (name_len > 0) {
                    device_name[name_len] = '\0';
                    printf("Detected device: %s\n", device_name);

                    if (pairing_mode) {
                        printf("Pairing mode enabled. Adding '%s' to approved list.\n", device_name);
                        add_approved_device(device_name);
                        pairing_mode = false; // Exit pairing mode after adding the device
                    } else if (is_device_approved(device_name)) {
                        printf("Approved device detected: %s. Unlocking door.\n", device_name);
                        open_door();
                    }
                }
            }
            break;

        default:
            break;
    }
}

void app_main() {
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize the GPIO for the relay
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_PIN, 0);

    // Initialize Bluetooth
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Register GAP callback
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    // Fetch the approved devices from the API
    fetch_approved_devices_from_api();

    // Start scanning for devices
    esp_ble_gap_set_scan_params(&(esp_ble_scan_params_t){
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30
    });

    // Simulate enabling pairing mode (for testing, you could map this to a button press)
    pairing_mode = true;
}
