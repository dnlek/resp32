/*
 * Robot Control and Factory Test
 * Handles Serial2 communication with robot
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "RobotControl";

#define LED_PIN GPIO_NUM_13

// Handle factory test commands
static void handle_factory_test(void)
{
    static char read_buf[256] = {0};
    static int buf_idx = 0;
    
    uint8_t uart_buf[64];
    int len = uart_read_bytes(UART_NUM_2, uart_buf, sizeof(uart_buf) - 1, 0);
    
    if (len > 0) {
        uart_buf[len] = '\0';
        
        for (int i = 0; i < len; i++) {
            char c = uart_buf[i];
            read_buf[buf_idx++] = c;
            
            if (c == '}') {
                read_buf[buf_idx] = '\0';
                
                if (strcmp(read_buf, "{BT_detection}") == 0) {
                    const char *response = "{BT_OK}";
                    uart_write_bytes(UART_NUM_2, response, strlen(response));
                    ESP_LOGI(TAG, "Factory test: BT detection");
                } else if (strcmp(read_buf, "{WA_detection}") == 0) {
                    // Send WiFi status
                    wifi_ap_record_t ap_info;
                    char response[128];
                    
                    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                        snprintf(response, sizeof(response), "{%s}", ap_info.ssid);
                    } else {
                        // In AP mode or disconnected
                        uint8_t mac[6];
                        char mac_str[13];
                        esp_wifi_get_mac(WIFI_IF_STA, mac);
                        snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
                                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                        snprintf(response, sizeof(response), "{RobotSetup-%s}", mac_str);
                    }
                    
                    uart_write_bytes(UART_NUM_2, response, strlen(response));
                    ESP_LOGI(TAG, "Factory test: WA detection - %s", response);
                }
                
                buf_idx = 0;
                memset(read_buf, 0, sizeof(read_buf));
            }
            
            if (buf_idx >= sizeof(read_buf) - 1) {
                buf_idx = 0;
                memset(read_buf, 0, sizeof(read_buf));
            }
        }
    }
}

// Robot control task
void robot_control_task(void *pvParameters)
{
    TickType_t last_led_update = 0;
    bool led_state = false;
    
    while (1) {
        // Handle factory test
        handle_factory_test();
        
        // Update LED based on WiFi status
        // TickType_t now = xTaskGetTickCount();
        // if (now - last_led_update >= pdMS_TO_TICKS(100)) {
        //     wifi_ap_record_t ap_info;
        //     if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        //         // WiFi connected - LED on
        //         gpio_set_level(LED_PIN, 0);
        //     } else {
        //         // WiFi disconnected - blink LED
        //         led_state = !led_state;
        //         gpio_set_level(LED_PIN, led_state ? 0 : 1);
        //     }
        //     last_led_update = now;
        // }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Initialize robot control
void robot_control_init(void)
{
    ESP_LOGI(TAG, "Robot control initialized");
}

