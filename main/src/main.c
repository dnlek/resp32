#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "camera_pins.h"
#include "wifi_provisioning.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *TAG = "camera_server";
static httpd_handle_t camera_httpd = NULL;
static bool camera_initialized = false;

static esp_err_t status_handler(httpd_req_t *req)
{
    char json_response[512];
    sensor_t *s = esp_camera_sensor_get();
    
    wifi_ap_record_t ap_info;
    bool wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    
    if (!camera_initialized || !s) {
        snprintf(json_response, sizeof(json_response),
                "{\"camera\":\"not_initialized\",\"wifi\":\"%s\"}",
                wifi_connected ? "connected" : "disconnected");
    } else {
        snprintf(json_response, sizeof(json_response),
                "{\"camera\":\"ok\",\"sensor_pid\":\"0x%02X\",\"framesize\":%u,\"quality\":%u,\"wifi\":\"%s\"}",
                s->id.PID, s->status.framesize, s->status.quality,
                wifi_connected ? "connected" : "disconnected");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t cam_stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char *part_buf[128];
    
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame123");
    if (res != ESP_OK) {
        return res;
    }
    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        
        size_t hlen = snprintf((char *)part_buf, sizeof(part_buf),
                "\r\n--frame123\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %u\r\n\r\n",
                (unsigned int)fb->len);
        
        if (httpd_resp_send_chunk(req, (const char *)part_buf, hlen) != ESP_OK) {
            esp_camera_fb_return(fb);
            res = ESP_FAIL;
            break;
        }
        
        if (httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
            esp_camera_fb_return(fb);
            res = ESP_FAIL;
            break;
        }
        
        esp_camera_fb_return(fb);
        
        if (res != ESP_OK) {
            break;
        }
    }
    
    return res;
}

static void start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(camera_httpd, &status_uri);
        
        httpd_uri_t cam_uri = {
            .uri = "/cam",
            .method = HTTP_GET,
            .handler = cam_stream_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(camera_httpd, &cam_uri);
        
        ESP_LOGI(TAG, "HTTP server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

static esp_err_t init_camera(void)
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0) {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, FRAMESIZE_SVGA);
        s->set_vflip(s, 0);
        s->set_hmirror(s, 1);
    }
    
    camera_initialized = true;
    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_provisioning_init();
    
    if (init_camera() == ESP_OK) {
        ESP_LOGI(TAG, "Camera initialized, starting HTTP server");
        start_http_server();
    } else {
        ESP_LOGW(TAG, "Starting HTTP server without camera");
        start_http_server();
    }
}

#ifdef __cplusplus
}
#endif

