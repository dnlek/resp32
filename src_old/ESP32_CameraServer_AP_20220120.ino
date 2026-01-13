/*
 * @Descripttion: 
 * @version: 
 * @Author: Elegoo
 * @Date: 2020-06-04 11:42:27
 * @LastEditors: Changhua
 * @LastEditTime: 2020-09-07 09:40:03
 */
//#include <EEPROM.h>
#include "CameraWebServer_AP.h"
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_task_wdt.h"
WiFiServer server(100);

#define RXD2 33
#define TXD2 4
CameraWebServer_AP CameraWebServerAP;

bool WA_en = false;

static unsigned long last_station_check = 0;

void SocketServer_Test(void) {
  static bool ED_client = true;
  WiFiClient client = server.available();
  if (client) {
    WA_en = true;
    ED_client = true;
    Serial.println("[Client connected]");
    String readBuff;
    String sendBuff;
    uint8_t Heartbeat_count = 0;
    bool Heartbeat_status = false;
    bool data_begin = true;
    static unsigned long loop_count = 0;
    while (client.connected()) {
      loop_count++;
      
      // Feed watchdog inside client loop
      if (loop_count % 100 == 0) {  // Every 100 iterations (~1 second)
        esp_task_wdt_reset();
        Serial.printf("[DEBUG] SocketServer_Test: Loop iteration %lu, feeding watchdog\n", loop_count);
      }

      delay(10);
      yield();
      if (client.available()) {
        Serial.println("Client available");
        Serial.flush();
        char c = client.read();
        Serial.print(c);
        if (true == data_begin && c == '{') {
          data_begin = false;
        }
        if (false == data_begin && c != ' ') {
          readBuff += c;
        }
        if (false == data_begin && c == '}') {
          data_begin = true;
          if (true == readBuff.equals("{Heartbeat}"))
          {
            Heartbeat_status = true;
          }
          else
          {
            Serial2.print(readBuff);
          }
          //Serial2.print(readBuff);
          readBuff = "";
        }
      }
      if (Serial2.available())
      {
        char c = Serial2.read();
        sendBuff += c;
        if (c == '}') {
          client.print(sendBuff);
          Serial.print(sendBuff);
          sendBuff = "";
        }
      }

      static unsigned long Heartbeat_time = 0;
      if (millis() - Heartbeat_time > 1000) //心跳频率
      {
        Serial.println("Sending Heartbeat");
        Serial.flush();
        client.print("{Heartbeat}");
        if (true == Heartbeat_status)
        {
          Heartbeat_status = false;
          Heartbeat_count = 0;
        }
        else if (false == Heartbeat_status)
        {
          Heartbeat_count += 1;
        }
        if (Heartbeat_count > 3)
        {
          Heartbeat_count = 0;
          Heartbeat_status = false;
          break;
        }
        Heartbeat_time = millis();
      }
      // static unsigned long Test_time = 0;
      // if (millis() - Test_time > 1000) {
      //   Serial.println("Checking WiFi AP stations");
      //   Serial.flush();
      //   Test_time = millis();
      //   // WiFi.softAPgetStationNum() can hang on ESP32-S3 - add timeout protection
      //   //Serial2.println(WiFi.softAPgetStationNum());
      //   uint8_t station_num = 0;
      //   unsigned long start = millis();
      //   station_num = WiFi.softAPgetStationNum();
      //   if (millis() - start > 100) {
      //     Serial.println("WARNING: WiFi.softAPgetStationNum() took too long!");
      //   }
      //   if (0 == station_num) //如果连接的设备个数为"0" 则向车模发送停止命令
      //   {
      //     Serial2.print("{\"N\":100}");
      //     break;
      //   }
      // }
    }
    Serial2.print("{\"N\":100}");
    client.stop();
    Serial.println("[Client disconnected]");
  } else {
    if (ED_client == true) {
      ED_client = false;
      Serial2.print("{\"N\":100}");
    }
  }
}
void FactoryTest(void)
{
  static String readBuff;
  String sendBuff;
  if (Serial2.available()) {
    char c = Serial2.read();
    readBuff += c;
    if (c == '}') //接收到结束字符
    {
      if (true == readBuff.equals("{BT_detection}"))
      {
        Serial2.print("{BT_OK}");
        Serial.println("Factory...");
      }
      else if (true == readBuff.equals("{WA_detection}"))
      {
        Serial2.print("{");
        Serial2.print(CameraWebServerAP.wifi_name);
        Serial2.print("}");
        Serial.println("Factory...");
      }
      readBuff = "";
    }
  }
  if (millis() - last_station_check > 500) {
    Serial.println("Checking WiFi AP stations...");
    Serial.flush();
    // WiFi.softAPgetStationNum() can hang on ESP32-S3 - add timeout protection
    uint8_t station_num = 0;
    unsigned long start = millis();
    station_num = WiFi.softAPgetStationNum();
    last_station_check = millis();
    if (millis() - start > 100) {
      Serial.println("WARNING: WiFi.softAPgetStationNum() took too long!");
    }
    Serial.print("Station num: ");
    Serial.println(station_num);
    Serial.flush();
    if (station_num > 0) {
      if (true == WA_en)
      {
        digitalWrite(13, LOW);
        Serial2.print("{WA_OK}");
        WA_en = false;
      }
    } else {
      static unsigned long Test_time;
      static bool en = true;
      if (millis() - Test_time > 100)
      {
        if (false == WA_en)
        {
          Serial2.print("{WA_NO}");
          WA_en = true;
        }
        if (en == true)
        {
          en = false;
          digitalWrite(13, HIGH);
        }
        else
        {
          en = true;
          digitalWrite(13, LOW);
        }
        Test_time = millis();
      }
    }
  }
}
void setup()
{
  Serial.begin(9600);
  delay(2000);  // Wait for Serial to be ready on ESP32-S3
  
  Serial.println("\n\n=== ESP32-S3 Camera Server Starting ===");
  Serial.flush();
  
  // CRITICAL WORKAROUND: Initialize WiFi FIRST before anything else
  // WiFi.mode() hangs if called after other initialization on ESP32-S3
  // This appears to be a framework bug with initialization order
  Serial.println("Initializing WiFi FIRST (workaround for ESP32-S3 hang)...");
  Serial.flush();
  
  // Disable watchdog during WiFi initialization
  esp_task_wdt_deinit();
  
  // Initialize WiFi AP mode early
  if (!WiFi.mode(WIFI_AP)) {
    Serial.println("FATAL: WiFi.mode(WIFI_AP) failed!");
    Serial.flush();
    while(1) delay(1000);  // Halt on failure
  }
  Serial.println("WiFi mode set to AP");
  Serial.flush();
  
  delay(500);  // Give WiFi time to stabilize
  
  // CRITICAL: Also initialize WiFi.softAP() early to avoid hang
  // WiFi.softAP() can also hang on ESP32-S3 if called later
  Serial.println("Initializing WiFi AP (softAP)...");
  Serial.flush();
  
  // Generate WiFi SSID from MAC address
  uint64_t chipid = ESP.getEfuseMac();
  char string[10];
  sprintf(string, "%04X", (uint16_t)(chipid >> 32));
  String mac0_default = String(string);
  sprintf(string, "%08X", (uint32_t)chipid);
  String mac1_default = String(string);
  String ssid_prefix = "cookie-";
  String url = ssid_prefix + mac0_default + mac1_default;
  const char *mac_default = url.c_str();
  const char *password = "";  // Empty password, matching CameraWebServer_AP.h
  
  if (!WiFi.softAP(mac_default, password, 9)) {
    Serial.println("FATAL: WiFi.softAP() failed!");
    Serial.flush();
    while(1) delay(1000);  // Halt on failure
  }
  Serial.println("WiFi AP started");
  Serial.flush();
  
  delay(1000);  // Give AP time to fully start
  
  // Configure WiFi AP IP address (default is 192.168.4.1)
  // Explicitly set it to ensure it's configured correctly
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(local_ip, gateway, subnet)) {
    Serial.println("WARNING: WiFi.softAPConfig() failed!");
    Serial.flush();
  } else {
    Serial.println("WiFi AP IP configured");
    Serial.flush();
  }
  
  delay(500);  // Give IP config time to apply
  
  // Print WiFi AP info for debugging
  Serial.println("=== WiFi AP Information ===");
  Serial.print("SSID: ");
  Serial.println(mac_default);
  Serial.print("Password: ");
  Serial.println(password[0] ? password : "(empty - open network)");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Gateway: ");
  Serial.println(gateway);
  Serial.print("Subnet: ");
  Serial.println(subnet);
  Serial.print("Channel: 9");
  Serial.println();
  Serial.println("===========================");
  Serial.flush();
  
  // Re-enable watchdog
  esp_task_wdt_init(5, true);
  
  Serial.println("WiFi initialized successfully, continuing with rest of setup...");
  Serial.flush();
  
  delay(500);  // Give WiFi time to stabilize
  
  Serial.println("Initializing Serial2...");
  Serial.flush();
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  Serial.println("Calling CameraWebServer_AP_Init()...");
  Serial.flush();
  //http://192.168.4.1/control?var=framesize&val=3
  //http://192.168.4.1/Test?var=
  CameraWebServerAP.CameraWebServer_AP_Init();
  
  Serial.println("Starting WiFiServer...");
  Serial.flush();
  server.begin();
  
  Serial.println("Setup complete!");
  Serial.flush();
  delay(100);
  // while (Serial.read() >= 0)
  // {
  //   /*清空串口缓存...*/
  // }
  // while (Serial2.read() >= 0)
  // {
  //   /*清空串口缓存...*/
  // }
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  Serial.println("Elegoo-2020...");
  Serial2.print("{Factory}");
}
void loop() {
  // Add watchdog feeding in loop to prevent resets
  static unsigned long last_wdt_feed = 0;
  if (millis() - last_wdt_feed > 1000) {
    esp_task_wdt_reset();
    last_wdt_feed = millis();
  }
  
  SocketServer_Test();
  FactoryTest();
  
  delay(10);

  // Periodic WiFi status check (every 10 seconds) - less frequent to avoid hangs
  static unsigned long last_status_check = 0;
  if (millis() - last_status_check > 10000) {
    Serial.print("WiFi AP Status - IP: ");
    Serial.print(WiFi.softAPIP());
    Serial.print(", SSID: ");
    Serial.print(WiFi.softAPSSID());
    // Note: WiFi.softAPgetStationNum() can hang, so we'll skip it in status check
    Serial.println();
    Serial.flush();
    last_status_check = millis();
  }
}

/*
C:\Program Files (x86)\Arduino\hardware\espressif\arduino-esp32/tools/esptool/esptool.exe --chip esp32 --port COM6 --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 
0xe000 C:\Program Files (x86)\Arduino\hardware\espressif\arduino-esp32/tools/partitions/boot_app0.bin 
0x1000 C:\Program Files (x86)\Arduino\hardware\espressif\arduino-esp32/tools/sdk/bin/bootloader_qio_80m.bin 
0x10000 C:\Users\Faynman\Documents\Arduino\Hex/CameraWebServer_AP_20200608xxx.ino.bin 
0x8000 C:\Users\Faynman\Documents\Arduino\Hex/CameraWebServer_AP_20200608xxx.ino.partitions.bin 

flash:path
C:\Program Files (x86)\Arduino\hardware\espressif\arduino-esp32\tools\partitions\boot_app0.bin
C:\Program Files (x86)\Arduino\hardware\espressif\arduino-esp32\tools\sdk\bin\bootloader_dio_40m.bin
C:\Users\Faynman\Documents\Arduino\Hex\CameraWebServer_AP_20200608xxx.ino.partitions.bin
*/
//esptool.py-- port / dev / ttyUSB0-- baub 261216 write_flash-- flash_size = detect 0 GetChipID.ino.esp32.bin
