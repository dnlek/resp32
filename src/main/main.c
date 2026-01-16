/*
 * ESP32-S3 Robot Camera Server with ESP-IDF WiFi Provisioning
 * 
 * Features:
 * - WiFi provisioning with captive portal for easy WiFi setup
 * - Credentials stored securely in NVS
 * - Auto-reconnect if WiFi disconnects
 * - Reset button support (hold GPIO 0 for 5 seconds to reset WiFi)
 * - Camera server (when camera is enabled)
 * - Socket server for robot control
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "camera_pins.h"
#include "app_httpd.h"
#include "wifi_provisioning.h"
#include "socket_server.h"
#include "robot_control.h"

// Forward declarations
void wifi_provisioning_init(void);
void socket_server_init(void);
void robot_control_init(void);
void robot_control_task(void *pvParameters);
void reset_button_task(void *pvParameters);
void wifi_monitor_task(void *pvParameters);
void startCameraServer(void);

// Pin definitions
#define RXD2 33
#define TXD2 4
#define RESET_BUTTON_PIN GPIO_NUM_0
#define LED_PIN GPIO_NUM_13

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "RobotCameraServer";
static bool s_wifi_connected = false;
static bool s_in_provisioning = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (!s_in_provisioning) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_in_provisioning) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "WiFi connection failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0};
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(wifi_config.sta.ssid);
        nvs_get_str(nvs_handle, "ssid", (char*)wifi_config.sta.ssid, &required_size);
        required_size = sizeof(wifi_config.sta.password);
        nvs_get_str(nvs_handle, "pass", (char*)wifi_config.sta.password, &required_size);
        nvs_close(nvs_handle);
        
        if (strlen((char*)wifi_config.sta.ssid) > 0) {
            ESP_LOGI(TAG, "Found saved WiFi credentials: %s", wifi_config.sta.ssid);
            esp_netif_create_default_wifi_sta();
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            s_in_provisioning = false;
            
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                pdMS_TO_TICKS(20000));
            
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connected to WiFi successfully");
                return;
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGW(TAG, "Failed to connect to saved WiFi, starting provisioning");
                esp_wifi_stop();
            }
        }
    }
    
    s_in_provisioning = true;
    ESP_LOGI(TAG, "Starting WiFi provisioning...");
    wifi_provisioning_init();
}

// Reset button task
void reset_button_task(void *pvParameters) {
    gpio_set_direction(RESET_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RESET_BUTTON_PIN, GPIO_PULLUP_ONLY);
    
    TickType_t button_press_start = 0;
    bool button_pressed = false;
    const TickType_t hold_time = pdMS_TO_TICKS(5000); // 5 seconds
    
    while (1) {
        if (gpio_get_level(RESET_BUTTON_PIN) == 0) { // Button pressed (LOW)
            if (!button_pressed) {
                button_pressed = true;
                button_press_start = xTaskGetTickCount();
                ESP_LOGI(TAG, "Reset button pressed - hold for 5 seconds to reset WiFi");
            } else {
                TickType_t hold_duration = xTaskGetTickCount() - button_press_start;
                if (hold_duration >= hold_time) {
                    ESP_LOGW(TAG, "RESET BUTTON HELD - Clearing WiFi credentials!");
                    nvs_handle_t nvs_handle;
                    if (nvs_open("wifi", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                        nvs_erase_all(nvs_handle);
                        nvs_commit(nvs_handle);
                        nvs_close(nvs_handle);
                    }
                    ESP_LOGI(TAG, "WiFi credentials cleared. Restarting...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
            }
        } else {
            button_pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void wifi_monitor_task(void *pvParameters) {
    while (1) {
        if (s_wifi_connected && !s_in_provisioning) {
            gpio_set_level(LED_PIN, 0);
        } else {
            static bool led_state = false;
            led_state = !led_state;
            gpio_set_level(LED_PIN, led_state ? 0 : 1);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Main application entry point
void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== ESP32-S3 Robot Camera Server ===");
    ESP_LOGI(TAG, "ESP-IDF WiFi Provisioning Enabled");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize GPIO
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 1); // LED off initially
    
    // Initialize UART for robot communication
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_2, 1024, 1024, 0, NULL, 0);
    
    // Initialize WiFi
    wifi_init_sta();
    
    // Initialize robot control
    robot_control_init();
    
    // Initialize socket server
    socket_server_init();
    
    // Initialize camera server (when enabled)
    startCameraServer();
    
    // Create tasks
    xTaskCreate(reset_button_task, "reset_button", 2048, NULL, 5, NULL);
    xTaskCreate(wifi_monitor_task, "wifi_monitor", 2048, NULL, 5, NULL);
    xTaskCreate(robot_control_task, "robot_control", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Setup complete! Robot ready!");
    
    // Send factory message
    const char *factory_msg = "{Factory}";
    uart_write_bytes(UART_NUM_2, factory_msg, strlen(factory_msg));
}

