/*
 * WiFi Provisioning via BLE
 * Uses ESP-IDF wifi_provisioning component with BLE scheme
 */

#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "WiFiProvisioning";

static void wifi_prov_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "BLE provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received WiFi credentials - SSID: %s", wifi_sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed: %s", 
                         *reason == WIFI_PROV_STA_AUTH_ERROR ? "Authentication failed" : "AP not found");
                break;
            }
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                wifi_config_t wifi_config;
                if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
                    nvs_handle_t nvs_handle;
                    if (nvs_open("wifi", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                        nvs_set_str(nvs_handle, "ssid", (char*)wifi_config.sta.ssid);
                        nvs_set_str(nvs_handle, "pass", (char*)wifi_config.sta.password);
                        nvs_commit(nvs_handle);
                        nvs_close(nvs_handle);
                        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
                    }
                }
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    }
}

void wifi_provisioning_init(void)
{
    esp_netif_create_default_wifi_sta();
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_prov_event_handler, NULL));
    
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };
    
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = "abcd1234";
    
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, "PROV_", NULL));
    
    ESP_LOGI(TAG, "BLE provisioning ready. Use ESP BLE Provisioning app to configure WiFi");
}
