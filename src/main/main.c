#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "RobotCameraServer";

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== ESP32-S3 Robot Camera Server ===");
    ESP_LOGI(TAG, "ESP-IDF WiFi Provisioning Enabled");
    while (1) { 
      ESP_LOGI(TAG, "RobotCameraServer is running");
      vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}