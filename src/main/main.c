#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "camera_server.h"

static const char *TAG = "PinTest";

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0


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

static bool init_camera(void) {
  
  cam_pins_t *p = &configs[0];

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
  return err == ESP_OK;
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
  int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi connecting");
    ESP_ERROR_CHECK(esp_wifi_connect());
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi disconnected");
    ESP_ERROR_CHECK(esp_wifi_connect());
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI("wifi", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void)
{
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = "",
          .password = "",
      },
  };

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  // ESP_ERROR_CHECK(esp_wifi_connect());
}



void app_main(void)
{
  vTaskDelay(pdMS_TO_TICKS(3000));
  ESP_LOGI(TAG, "RobotCameraServer is starting");
  ESP_LOGI(TAG, "Initializing NVS");
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "NVS initialized");

  ESP_LOGI(TAG, "Initializing network");
  wifi_init_sta();

  ESP_LOGI(TAG, "Network initialized, waiting for IP");
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "IP acquired");

  ESP_LOGI(TAG, "Initializing camera");
  bool camera_ok = init_camera();
  if (!camera_ok) {
    ESP_LOGE(TAG, "Camera failed to initialize");
    vTaskDelay(pdMS_TO_TICKS(1000));
    return;
  }
  ESP_LOGI(TAG, "Camera OK");
  vTaskDelay(pdMS_TO_TICKS(1000));
  start_camera_server();

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
