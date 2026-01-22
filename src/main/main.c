#include "esp_log.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "PinTest";

typedef struct {
    int pin_pwdn;
    int pin_reset;
    int pin_xclk;
    int pin_siod;
    int pin_sioc;
    int pin_y9;
    int pin_y8;
    int pin_y7;
    int pin_y6;
    int pin_y5;
    int pin_y4;
    int pin_y3;
    int pin_y2;
    int pin_vsync;
    int pin_href;
    int pin_pclk;
} cam_pins_t;

static cam_pins_t configs[] = {
    { -1, -1, 15, 4, 5, 16, 17, 18, 12, 10, 8, 9, 11, 6, 7, 13 },   // M5Stack Wide style
    { -1, -1, 14, 40, 39, 48, 47, 21, 38, 11, 10, 9, 8, 6, 5, 7 },   // ESP32-S3-EYE style
    { -1, -1, 1,  2,  42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 21, 45 } // Generic S3 OV3660
};

static int cfg_count = sizeof(configs)/sizeof(configs[0]);

static int load_index() {
    nvs_handle h;
    int32_t idx = 0;
    if (nvs_open("camtest", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_i32(h, "idx", &idx);
        nvs_close(h);
    }
    return idx;
}

static void save_index(int32_t idx) {
    nvs_handle h;
    if (nvs_open("camtest", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "idx", idx);
        nvs_commit(h);
        nvs_close(h);
    }
}

void app_main(void)
{
  ESP_LOGI(TAG, "RobotCameraServer is starting");
  ESP_LOGI(TAG, "Initializing NVS");
  nvs_flash_init();
  ESP_LOGI(TAG, "NVS initialized");
  int idx = load_index();
  if (idx < 0 || idx >= cfg_count) idx = 0;

  cam_pins_t *p = &configs[idx];

  ESP_LOGI(TAG, "Trying config %d", idx);

  camera_config_t c = {
      .pin_pwdn  = p->pin_pwdn,
      .pin_reset = p->pin_reset,
      .pin_xclk  = p->pin_xclk,
      .pin_sccb_sda = p->pin_siod,
      .pin_sccb_scl = p->pin_sioc,
      .pin_d7 = p->pin_y9,
      .pin_d6 = p->pin_y8,
      .pin_d5 = p->pin_y7,
      .pin_d4 = p->pin_y6,
      .pin_d3 = p->pin_y5,
      .pin_d2 = p->pin_y4,
      .pin_d1 = p->pin_y3,
      .pin_d0 = p->pin_y2,
      .pin_vsync = p->pin_vsync,
      .pin_href  = p->pin_href,
      .pin_pclk  = p->pin_pclk,
      .xclk_freq_hz = 20000000,
      .ledc_timer = LEDC_TIMER_0,
      .ledc_channel = LEDC_CHANNEL_0,
      .pixel_format = PIXFORMAT_JPEG,
      .frame_size = FRAMESIZE_QVGA,
      .jpeg_quality = 10,
      .fb_count = 1
  };

  esp_err_t err = esp_camera_init(&c);

  if (err == ESP_OK) {
      ESP_LOGI(TAG, "Camera OK with config %d", idx);

      while (1) {
          vTaskDelay(pdMS_TO_TICKS(1000));
          ESP_LOGI(TAG, "Getting frame");
          camera_fb_t *fb = esp_camera_fb_get(); 
          if (!fb) {
            ESP_LOGE(TAG, "Failed to get frame"); 
          } else { 
            ESP_LOGI(TAG, "Got frame: %dx%d, %d bytes, format=%d", fb->width, fb->height, fb->len, fb->format); 
            esp_camera_fb_return(fb); 
          }
      }
  }

  ESP_LOGE(TAG, "Camera failed with config %d", idx);

  int next = (idx + 1) % cfg_count;
  save_index(next);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}
