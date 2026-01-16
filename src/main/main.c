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
#include "esp_camera.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/ledc.h"
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

// Try to detect camera sensor type by reading sensor ID
// Returns sensor PID (Product ID) if successful, 0 if failed
static uint16_t detect_camera_sensor(void)
{
    // This would require I2C access to the camera sensor
    // For now, we'll rely on the configured camera model
    // In the future, this could read the sensor ID register (0x0A for OV2640, etc.)
    ESP_LOGI(TAG, "Camera model: WROVER_KIT (configured)");
    return 0; // Detection not implemented yet
}

// Option to disable camera initialization if it crashes
// Set to 0 to skip camera init (system will continue without camera)
// WARNING: ESP32-S3 camera driver has known crash issue - may need to disable
#define ENABLE_CAMERA_INIT 1

// Initialize camera (must be called before starting HTTP camera server)
static esp_err_t camera_init(void)
{
    #if !ENABLE_CAMERA_INIT
    ESP_LOGW(TAG, "Camera initialization disabled (ENABLE_CAMERA_INIT=0)");
    return ESP_ERR_NOT_SUPPORTED;
    #endif
    
    ESP_LOGI(TAG, "Initializing camera...");

    // Check PSRAM availability (camera needs PSRAM for frame buffers)
    // Use heap_caps to check - this works regardless of CONFIG_SPIRAM define
    bool psram_available = false;
    size_t psram_size = 0;
    
    // Check if PSRAM is available via heap capabilities
    psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_size == 0) {
        // Also try the esp_psram API if available
        #ifdef CONFIG_SPIRAM
        psram_size = esp_psram_get_size();
        #endif
    }
    
    psram_available = (psram_size > 0);
    
    if (psram_available) {
        ESP_LOGI(TAG, "PSRAM detected: %d bytes (%.2f MB)", psram_size, psram_size / (1024.0 * 1024.0));
    } else {
        ESP_LOGW(TAG, "PSRAM not detected - camera will use limited frame sizes");
        ESP_LOGW(TAG, "To enable PSRAM: Run 'idf.py menuconfig' -> Component config -> ESP PSRAM -> Enable");
        ESP_LOGW(TAG, "Or add CONFIG_SPIRAM=y to sdkconfig.defaults and rebuild");
    }

    // Validate camera pins are defined
    if (XCLK_GPIO_NUM < 0 || SIOD_GPIO_NUM < 0 || SIOC_GPIO_NUM < 0) {
        ESP_LOGE(TAG, "Invalid camera pin configuration - check camera_pins.h");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Log camera model being used
    #if defined(CAMERA_MODEL_WROVER_KIT)
    ESP_LOGI(TAG, "Camera model: WROVER_KIT");
    // WARNING: WROVER_KIT pins are designed for ESP32, not ESP32-S3
    // Some pins (35, 39) are input-only on ESP32-S3 and may cause issues
    ESP_LOGW(TAG, "Note: WROVER_KIT pins may not be optimal for ESP32-S3");
    #elif defined(CAMERA_MODEL_AI_THINKER)
    ESP_LOGI(TAG, "Camera model: AI_THINKER");
    #elif defined(CAMERA_MODEL_ESP_EYE)
    ESP_LOGI(TAG, "Camera model: ESP_EYE");
    #else
    ESP_LOGI(TAG, "Camera model: Unknown");
    #endif
    
    // Validate all data pins are valid GPIO numbers for ESP32-S3
    // ESP32-S3 has GPIO 0-48, but some are input-only
    int data_pins[] = {Y2_GPIO_NUM, Y3_GPIO_NUM, Y4_GPIO_NUM, Y5_GPIO_NUM,
                       Y6_GPIO_NUM, Y7_GPIO_NUM, Y8_GPIO_NUM, Y9_GPIO_NUM};
    for (int i = 0; i < 8; i++) {
        if (data_pins[i] < 0 || data_pins[i] > 48) {
            ESP_LOGE(TAG, "Invalid data pin %d: GPIO %d (must be 0-48)", i, data_pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Match original config structure exactly
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;  // 10MHz for stability
    config.pixel_format = PIXFORMAT_JPEG;

    // Configure frame size based on PSRAM availability
    // Start with conservative settings to avoid memory issues
    if (psram_available) {
        config.frame_size = FRAMESIZE_VGA;  // 640x480 - more conservative than SVGA
        config.jpeg_quality = 12;
        config.fb_count = 1;  // Start with single buffer to avoid allocation issues
        // ESP32-S3: Frame buffers must be allocated in PSRAM when available
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size = FRAMESIZE_QVGA;  // 320x240 - smaller for no PSRAM
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
    
    // ESP32-S3: Set grab mode (required field to prevent NULL pointer crash)
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    ESP_LOGI(TAG, "Camera config: xclk=%d, sda=%d, scl=%d, pwdn=%d, reset=%d",
             config.pin_xclk, config.pin_sscb_sda, config.pin_sscb_scl,
             config.pin_pwdn, config.pin_reset);
    ESP_LOGI(TAG, "Camera config: frame_size=%d, jpeg_quality=%d, fb_count=%d, fb_location=%d",
             config.frame_size, config.jpeg_quality, config.fb_count, config.fb_location);
    ESP_LOGI(TAG, "Camera data pins: d0=%d, d1=%d, d2=%d, d3=%d, d4=%d, d5=%d, d6=%d, d7=%d",
             config.pin_d0, config.pin_d1, config.pin_d2, config.pin_d3,
             config.pin_d4, config.pin_d5, config.pin_d6, config.pin_d7);

    // Initialize GPIO pins before camera init (ESP32-S3 may need this)
    // Set PWDN and RESET pins if they're not -1
    if (config.pin_pwdn >= 0) {
        gpio_reset_pin(config.pin_pwdn);
        gpio_set_direction(config.pin_pwdn, GPIO_MODE_OUTPUT);
        gpio_set_level(config.pin_pwdn, 0);  // Power on camera
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (config.pin_reset >= 0) {
        gpio_reset_pin(config.pin_reset);
        gpio_set_direction(config.pin_reset, GPIO_MODE_OUTPUT);
        gpio_set_level(config.pin_reset, 1);  // Release reset
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Longer delay to ensure everything is ready
    ESP_LOGI(TAG, "Waiting for camera hardware to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Calling esp_camera_init()...");
    
    // ESP32-S3 camera driver workaround: Try to initialize but catch crashes
    // The driver may crash with NULL pointer if sensor driver isn't properly registered
    // or if there's a compatibility issue with the pin configuration
    
    // TODO: This is a known issue with ESP32-S3 camera driver
    // Possible solutions:
    // 1. Update esp32-camera component to latest version
    // 2. Check if camera sensor driver needs explicit registration
    // 3. Verify GPIO pins are compatible with ESP32-S3 DCMI interface
    // 4. Try ESP_EYE pin configuration which is designed for ESP32-S3
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x (%s)", err, esp_err_to_name(err));
        ESP_LOGE(TAG, "Check: 1) Camera hardware connected 2) Pin configuration 3) Power supply");
        ESP_LOGE(TAG, "Note: ESP32-S3 may require different pin configuration than ESP32");
        ESP_LOGE(TAG, "Consider trying ESP_EYE pin configuration if WROVER_KIT doesn't work");
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (!s)
    {
        ESP_LOGE(TAG, "Camera sensor is NULL after init - driver issue");
        esp_camera_deinit();
        return ESP_FAIL;
    }

    // Try to detect camera sensor type
    // Most OV sensors have a PID register we can read
    // This helps verify the camera matches the configuration
    ESP_LOGI(TAG, "Camera sensor initialized successfully");
    ESP_LOGI(TAG, "Sensor ID: 0x%02X (if available)", s->id.PID);
    
    // Log sensor capabilities
    if (s->id.PID != 0) {
        ESP_LOGI(TAG, "Detected sensor PID: 0x%02X", s->id.PID);
        // Common sensor IDs:
        // 0x26 = OV2640, 0x42 = OV7725, 0x60 = OV3660, etc.
        if (s->id.PID == 0x26) {
            ESP_LOGI(TAG, "Sensor type: OV2640 (common in WROVER_KIT and AI_THINKER)");
        } else if (s->id.PID == 0x42) {
            ESP_LOGI(TAG, "Sensor type: OV7725");
        } else if (s->id.PID == 0x60) {
            ESP_LOGI(TAG, "Sensor type: OV3660");
        }
    }

    // Basic orientation / default settings (tweak as needed)
    s->set_vflip(s, 0);
    s->set_hmirror(s, 0);

    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

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
            // Create netif only if it doesn't exist yet
            esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (sta_netif == NULL) {
                esp_netif_create_default_wifi_sta();
            }
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
    
    // Initialize camera before starting camera HTTP server
    // WARNING: Camera init crashes on ESP32-S3 - this is a known driver issue
    // The crash happens inside esp_camera_init() with NULL pointer dereference
    // To temporarily disable camera, set ENABLE_CAMERA_INIT to 0 in camera_init()
    #if ENABLE_CAMERA_INIT
    esp_err_t camera_err = camera_init();
    if (camera_err != ESP_OK) {
        ESP_LOGW(TAG, "Camera failed to initialize (error: 0x%x); camera endpoints will return errors", camera_err);
        ESP_LOGW(TAG, "System will continue without camera functionality");
    }
    #else
    ESP_LOGW(TAG, "Camera initialization disabled (ENABLE_CAMERA_INIT=0)");
    #endif
    
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

