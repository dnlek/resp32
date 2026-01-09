
// WARNING!!! Make sure that you have selected Board ---> ESP32 Dev Module
//            Partition Scheme ---> Huge APP (3MB No OTA/1MB SPIFFS)
//            PSRAM ---> enabled
//重要配置：
// #define HTTPD_DEFAULT_CONFIG()             \
//   {                                        \
//     .task_priority = tskIDLE_PRIORITY + 5, \
//     .stack_size = 4096,                    \
//     .server_port = 80,                     \
//     .ctrl_port = 32768,                    \
//     .max_open_sockets = 7,                 \
//     .max_uri_handlers = 8,                 \
//     .max_resp_headers = 8,                 \
//     .backlog_conn = 5,                     \
//     .lru_purge_enable = false,             \
//     .recv_wait_timeout = 5,                \
//     .send_wait_timeout = 5,                \
//     .global_user_ctx = NULL,               \
//     .global_user_ctx_free_fn = NULL,       \
//     .global_transport_ctx = NULL,          \
//     .global_transport_ctx_free_fn = NULL,  \
//     .open_fn = NULL,                       \
//     .close_fn = NULL,                      \
//   }

// Select camera model
// #define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM

#define CAMERA_MODEL_M5STACK_WIDE

//#define CAMERA_MODEL_AI_THINKER

#include "CameraWebServer_AP.h"
#include "camera_pins.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "driver/timer.h"

static const char* TAG = "CameraWebServer";

// #include "BLEAdvertisedDevice.h"
// BLEAdvertisedDevice _BLEAdvertisedDevice;

void startCameraServer();
void CameraWebServer_AP::CameraWebServer_AP_Init(void)
{
  Serial.println("\n\nStarting CameraWebServer_AP_Init...");
  Serial.flush();
  
  // Disable watchdog entirely during initialization
  // WiFi initialization can take time and the default watchdog might reset the system
  // We'll re-enable it after WiFi is initialized
  esp_task_wdt_deinit();  // Disable task watchdog
  Serial.println("Watchdog disabled during initialization");
  Serial.flush();
  
  Serial.println("Creating camera config...");
  camera_config_t config = {};  // Zero-initialize the structure
  Serial.println("Setting LEDC config...");
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
  config.xclk_freq_hz = 10000000;//20000000
  config.pixel_format = PIXFORMAT_JPEG;
  Serial.println("Checking PSRAM...");
  bool has_psram = psramFound();
  Serial.printf("PSRAM found: %s\n", has_psram ? "YES" : "NO");
  
  //init with high specs to pre-allocate larger buffers
  if (has_psram)
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // ESP32-S3 specific: ensure all required fields are set
  // Some fields might be needed but not explicitly set
  Serial.printf("Config: frame_size=%d, jpeg_quality=%d, fb_count=%d\n", 
                config.frame_size, config.jpeg_quality, config.fb_count);

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif
  // camera init
  Serial.println("Initializing camera...");
  Serial.printf("Camera config: xclk=%d, sda=%d, scl=%d, pwdn=%d, reset=%d\n", 
                config.pin_xclk, config.pin_sccb_sda, config.pin_sccb_scl, 
                config.pin_pwdn, config.pin_reset);
  Serial.printf("Camera pins: d0=%d, d1=%d, d2=%d, d3=%d, d4=%d, d5=%d, d6=%d, d7=%d\n", 
                config.pin_d0, config.pin_d1, config.pin_d2, config.pin_d3,
                config.pin_d4, config.pin_d5, config.pin_d6, config.pin_d7);
  
  // Add delay before camera init to ensure everything is ready
  delay(500);
  Serial.println("Calling esp_camera_init...");
  
  // ESP32-S3: Try to initialize camera with error handling
  // The crash might be due to missing sensor driver registration
  esp_err_t err = ESP_FAIL;
  
  // Try camera init - if it crashes, we'll see it in the backtrace
  // But first, let's ensure all config fields are valid
  Serial.println("Validating camera config...");
  
  // Check for invalid pin numbers (ESP32-S3 specific)
  if (config.pin_xclk < 0 || config.pin_sccb_sda < 0 || config.pin_sccb_scl < 0) {
    Serial.println("ERROR: Invalid camera pin configuration!");
    return;
  }
  
  Serial.println("Attempting camera initialization...");
  
  // WORKAROUND: Skip camera init due to ESP32-S3 driver crash
  // The crash happens inside esp_camera_init() with NULL pointer dereference (LoadProhibited)
  // This is an ESP32-S3 camera driver compatibility issue with the Arduino framework
  // The driver tries to access a NULL function pointer or structure
  // 
  // To re-enable camera: Set ENABLE_CAMERA_INIT to 1 and fix the driver issue
  // Possible solutions:
  // 1. Update ESP32 Arduino framework to latest version
  // 2. Check if camera sensor driver is properly registered
  // 3. Verify camera hardware connection and power supply
  // 4. Check if ESP32-S3 requires different camera initialization sequence
  
#define ENABLE_CAMERA_INIT 0  // Set to 1 to enable camera (will crash until driver issue is fixed)
  
#if ENABLE_CAMERA_INIT
  err = esp_camera_init(&config);
  
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    Serial.printf("Error details: %s\n", esp_err_to_name(err));
    Serial.println("WARNING: Continuing without camera - WiFi will still work");
  } else {
    Serial.println("Camera initialized successfully!");
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
      Serial.println("ERROR: Camera sensor is NULL!");
      Serial.println("WARNING: Continuing without camera - WiFi will still work");
    } else {
      //drop down frame size for higher initial frame rate
      //s->set_framesize(s, FRAMESIZE_SXGA); //字节长度采样值:60000                 #9 (画质高)  1280x1024
      s->set_framesize(s, FRAMESIZE_SVGA); //字节长度采样值:40000                   #7 (画质中)  800x600
      // s->set_framesize(s, FRAMESIZE_QVGA); //字节长度采样值:10000                #4 (画质低)  320x240

#if defined(CAMERA_MODEL_M5STACK_WIDE)
      s->set_vflip(s, 0);
      s->set_hmirror(s, 1);
#endif
      s->set_vflip(s, 0);   //图片方向设置（上下）
      s->set_hmirror(s, 0); //图片方向设置（左右）

      // s->set_vflip(s, 1);   //图片方向设置（上下）
      // s->set_hmirror(s, 1); //图片方向设置（左右）
    }
  }
#else
  Serial.println("WARNING: Camera initialization DISABLED due to ESP32-S3 driver crash");
  Serial.println("WiFi will continue to initialize...");
  Serial.println("HTTP server will start but camera endpoints will return errors");
  Serial.println("To enable camera: Set ENABLE_CAMERA_INIT to 1 and fix driver issue");
  err = ESP_FAIL;  // Mark as failed so we skip camera-dependent code
#endif
  
  Serial.println("\r\n");

  uint64_t chipid = ESP.getEfuseMac();
  char string[10];
  sprintf(string, "%04X", (uint16_t)(chipid >> 32));
  String mac0_default = String(string);
  sprintf(string, "%08X", (uint32_t)chipid);
  String mac1_default = String(string);
  String url = ssid + mac0_default + mac1_default;
  const char *mac_default = url.c_str();

  Serial.println(":----------------------------:");
  Serial.print("wifi_name:");
  Serial.println(mac_default);
  Serial.println(":----------------------------:");
  wifi_name = mac0_default + mac1_default;

  Serial.println("Configuring WiFi AP...");
  Serial.flush();
  
  // NOTE: WiFi.mode(WIFI_AP) and WiFi.softAP() should have been called in setup()
  // This is a workaround for ESP32-S3 WiFi initialization hang issue
  // WiFi operations hang if called later in initialization
  
  Serial.println("WiFi is already initialized from setup()");
  Serial.flush();
  
  // WiFi.softAP() was already called in setup(), so we just need to verify it's running
  // and configure any additional settings like TxPower
  
  delay(200);  // Give AP time to start
  
  Serial.println("Calling WiFi.setTxPower()...");
  Serial.flush();
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Must be called AFTER WiFi.mode() and WiFi.softAP()
  Serial.println("WiFi TxPower set");
  Serial.flush();
  
  // Watchdog is already re-enabled after WiFi.softAP()
  Serial.println("WiFi configuration complete");
  Serial.flush();
  
  Serial.println("Starting camera HTTP server...");
  Serial.flush();
  startCameraServer();
  Serial.println("Camera HTTP server started");
  Serial.flush();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("' to connect");
  Serial.flush();
}
