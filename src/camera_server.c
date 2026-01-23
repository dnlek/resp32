#include "camera_server.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

static const char *TAG = "CamServer";

#define UART_BUF_SIZE 512
#define MAX_SENT_CMDS 20
#define MAX_RECV_DATA 100
#define MAX_LOG_ENTRIES 200
#define LOG_ENTRY_SIZE 256

typedef struct {
    char cmd[128];
    bool confirmed;
    uint32_t timestamp;
} sent_cmd_t;

typedef struct {
    char data[256];
    uint32_t timestamp;
} recv_data_t;

typedef struct {
    char log[LOG_ENTRY_SIZE];
    uint32_t timestamp;
} log_entry_t;

static sent_cmd_t sent_cmds[MAX_SENT_CMDS];
static recv_data_t recv_data[MAX_RECV_DATA];
static log_entry_t log_buffer[MAX_LOG_ENTRIES];
static int sent_idx = 0;
static int recv_idx = 0;
static int log_idx = 0;
static SemaphoreHandle_t data_mutex = NULL;
static vprintf_like_t original_vprintf = NULL;

static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int val;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static const char* control_page_html =
"<!DOCTYPE html>"
"<html>"
"<head><title>Robi Control</title>"
"</head>"
"<body style='font-family:sans-serif; padding:20px;'>"

"<h2>Robi Control Panel</h2>"

"<div style='float:left; margin-right:20px;'>"
// "<img src='/stream' style='width:320px;height:auto;border:1px solid #444;' />"
"<br><br>"
"<button onclick=\"fetch('/move?dir=up')\" style='width:100px;height:50px;'>Up</button><br><br>"
"<button onclick=\"fetch('/move?dir=left')\" style='width:100px;height:50px;'>Left</button>"
"<button onclick=\"fetch('/move?dir=right')\" style='width:100px;height:50px;'>Right</button><br><br>"
"<button onclick=\"fetch('/move?dir=down')\" style='width:100px;height:50px;'>Down</button>"
"</div>"

"<div style='float:left; width:400px;'>"
"<h3>Send Command</h3>"
"<input id='cmd' type='text' style='width:250px;height:30px;font-size:16px;' />"
"<button onclick=\"sendCmd()\" style='height:36px;font-size:16px;'>Send</button>"

"<h3>Status</h3>"
"<div id='status' style='border:1px solid #ccc; padding:5px; background:#e8f4f8; font-family:monospace; font-size:11px;'>"
"</div>"

"<h3>Sent Commands <button onclick=\"document.getElementById('sent').innerHTML=''\" style='font-size:10px;'>Clear</button></h3>"
"<div id='sent' style='border:1px solid #ccc; padding:10px; height:150px; overflow-y:auto; background:#f9f9f9;'>"
"</div>"

"<h3>UART Responses <button onclick=\"document.getElementById('recv').innerHTML=''\" style='font-size:10px;'>Clear</button></h3>"
"<div id='recv' style='border:1px solid #ccc; padding:10px; height:150px; overflow-y:auto; background:#f0f0f0; font-family:monospace; font-size:12px;'>"
"</div>"

"<h3>System Logs</h3>"
"<div id='logs' style='border:1px solid #ccc; padding:10px; height:200px; overflow-y:auto; background:#000; color:#0f0; font-family:monospace; font-size:11px;'>"
"</div>"
"</div>"

"<script>"
"let uartDataPending = false;"
"function sendCmd(){"
"  let v = document.getElementById('cmd').value;"
"  if(v){"
"    fetch('/send?msg=' + encodeURIComponent(v)).then(() => {"
"      document.getElementById('cmd').value = '';"
"      updateData();"
"    });"
"  }"
"}"
"function updateData(){"
"  if(uartDataPending) return;"
"  uartDataPending = true;"
"  const controller = new AbortController();"
"  const timeout = setTimeout(() => controller.abort(), 1000);"
"  fetch('/uart_data', {signal: controller.signal}).then(r => r.json()).then(d => {"
"    clearTimeout(timeout);"
"    uartDataPending = false;"
"    if(d.status){"
"      let s = d.status;"
"      document.getElementById('status').innerHTML = 'Uptime: ' + s.uptime + 's | Sent buffer: ' + s.sent_buf + '/' + s.sent_max + ' | Recv buffer: ' + s.recv_buf + '/' + s.recv_max;"
"    }"
"    for(let i = 0; i < d.sent.length; i++){"
"      let s = d.sent[i];"
"      let status = s.confirmed ? 'OK' : 'PENDING';"
"      let div = document.createElement('div');"
"      div.textContent = status + ' [' + s.time + '] ' + s.cmd;"
"      document.getElementById('sent').appendChild(div);"
"    }"
"    for(let i = 0; i < d.received.length; i++){"
"      let div = document.createElement('div');"
"      div.textContent = '[' + d.received[i].time + '] ' + d.received[i].data;"
"      document.getElementById('recv').appendChild(div);"
"    }"
"  }).catch(e => {"
"    clearTimeout(timeout);"
"    uartDataPending = false;"
"    if(e.name === 'AbortError'){"
"      console.error('uart_data timeout');"
"    } else {"
"      console.error('uart_data error:', e);"
"      let div = document.createElement('div');"
"      div.style.color = 'red';"
"      div.textContent = 'Error: ' + e.message;"
"      document.getElementById('recv').appendChild(div);"
"    }"
"  });"
"}"
"setInterval(updateData, 1000);"
"updateData();"
"</script>"

"</body>"
"</html>";


static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static int log_vprintf(const char* fmt, va_list args)
{
    char buffer[LOG_ENTRY_SIZE];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    if (data_mutex && xSemaphoreTake(data_mutex, 0) == pdTRUE) {
        log_buffer[log_idx].timestamp = xTaskGetTickCount();
        strncpy(log_buffer[log_idx].log, buffer, sizeof(log_buffer[log_idx].log) - 1);
        log_buffer[log_idx].log[sizeof(log_buffer[log_idx].log) - 1] = '\0';
        log_idx = (log_idx + 1) % MAX_LOG_ENTRIES;
        xSemaphoreGive(data_mutex);
    }
    
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }
    return len;
}

static void uart_read_task(void *pvParameters)
{
    static char read_buf[256] = {0};
    static int buf_idx = 0;
    ESP_LOGI(TAG, "UART read task started");
    
    while (1) {
        uint8_t uart_buf[64];
        int len = uart_read_bytes(UART_NUM_2, uart_buf, sizeof(uart_buf) - 1, 0);
        
        if (len > 0) {
            uart_buf[len] = '\0';
            ESP_LOGI(TAG, "UART read %d bytes", len);
            
            for (int i = 0; i < len; i++) {
                char c = uart_buf[i];
                if (buf_idx < sizeof(read_buf) - 1) {
                    read_buf[buf_idx++] = c;
                }
                
                if (c == '}') {
                    read_buf[buf_idx] = '\0';
                    ESP_LOGI(TAG, "UART received complete message: %s", read_buf);
                    
                    if (data_mutex && xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        recv_data[recv_idx].timestamp = xTaskGetTickCount();
                        int msg_len = buf_idx < sizeof(recv_data[recv_idx].data) - 1 ? buf_idx : sizeof(recv_data[recv_idx].data) - 1;
                        for (int j = 0; j < msg_len; j++) {
                            char ch = read_buf[j];
                            if (ch >= 32 && ch < 127) {
                                recv_data[recv_idx].data[j] = ch;
                            } else {
                                recv_data[recv_idx].data[j] = '.';
                            }
                        }
                        recv_data[recv_idx].data[msg_len] = '\0';
                        recv_idx = (recv_idx + 1) % MAX_RECV_DATA;
                        xSemaphoreGive(data_mutex);
                    }
                    
                    buf_idx = 0;
                    memset(read_buf, 0, sizeof(read_buf));
                }
                
                if (buf_idx >= sizeof(read_buf) - 1) {
                    ESP_LOGW(TAG, "UART read buffer overflow, clearing");
                    buf_idx = 0;
                    memset(read_buf, 0, sizeof(read_buf));
                }
            }
        } else if (len < 0) {
            ESP_LOGE(TAG, "UART read error: %d", len);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static esp_err_t send_handler(httpd_req_t *req)
{
    int query_len = httpd_req_get_url_query_len(req);
    ESP_LOGI("UART", "Query string length: %d", query_len);
    
    if (query_len <= 0) {
        ESP_LOGW("UART", "No query string");
        httpd_resp_sendstr(req, "ERROR: No query string");
        return ESP_OK;
    }
    
    char *buf = malloc(query_len + 1);
    if (!buf) {
        ESP_LOGE("UART", "Failed to allocate query buffer");
        httpd_resp_sendstr(req, "ERROR: Memory allocation failed");
        return ESP_OK;
    }
    
    esp_err_t ret = httpd_req_get_url_query_str(req, buf, query_len + 1);
    if (ret != ESP_OK) {
        ESP_LOGE("UART", "Failed to get query string: %d", ret);
        free(buf);
        httpd_resp_sendstr(req, "ERROR: Failed to get query string");
        return ESP_OK;
    }
    
    ESP_LOGI("UART", "Query string: %s", buf);
    
    char msg[200];
    ret = httpd_query_key_value(buf, "msg", msg, sizeof(msg));
    free(buf);
    
    if (ret != ESP_OK) {
        ESP_LOGE("UART", "Failed to get msg parameter: %d", ret);
        httpd_resp_sendstr(req, "ERROR: Failed to get msg parameter");
        return ESP_OK;
    }
    
    ESP_LOGI("UART", "Raw msg parameter: %s", msg);
    url_decode(msg);
    ESP_LOGI("UART", "Decoded msg: %s (len=%zu)", msg, strlen(msg));
    
    ESP_LOGI("UART", "Sending %zu bytes:", strlen(msg));
    for (int i = 0; i < strlen(msg); i++) {
        ESP_LOGI("UART", "  [%d] 0x%02x '%c'", i, (unsigned char)msg[i], 
                 (msg[i] >= 32 && msg[i] < 127) ? msg[i] : '.');
    }

    size_t tx_free_before = 0;
    uart_get_tx_buffer_free_size(UART_NUM_2, &tx_free_before);
    ESP_LOGI("UART", "TX buffer free before: %zu bytes", tx_free_before);
    
    uart_flush(UART_NUM_2);
    
    int written = uart_write_bytes(UART_NUM_2, msg, strlen(msg));
    ESP_LOGI("UART", "uart_write_bytes returned: %d (expected: %zu)", written, strlen(msg));
    
    if (written != strlen(msg)) {
        ESP_LOGE("UART", "UART write incomplete! Written %d/%zu bytes", written, strlen(msg));
    }
    
    esp_err_t flush_ret = uart_flush(UART_NUM_2);
    if (flush_ret != ESP_OK) {
        ESP_LOGW("UART", "UART flush returned: %s (0x%x)", esp_err_to_name(flush_ret), flush_ret);
    }
    
    esp_err_t tx_wait = uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(1000));
    if (tx_wait != ESP_OK) {
        ESP_LOGE("UART", "TX wait failed: %s (0x%x)", esp_err_to_name(tx_wait), tx_wait);
    } else {
        ESP_LOGI("UART", "TX completed successfully");
    }
    
    size_t tx_free_after = 0;
    uart_get_tx_buffer_free_size(UART_NUM_2, &tx_free_after);
    ESP_LOGI("UART", "TX buffer free after: %zu bytes (used: %zu)", tx_free_after, tx_free_before - tx_free_after);
    
    bool confirmed = (written == strlen(msg)) && (tx_wait == ESP_OK);

    if (data_mutex && xSemaphoreTake(data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        sent_cmds[sent_idx].timestamp = xTaskGetTickCount();
        strncpy(sent_cmds[sent_idx].cmd, msg, sizeof(sent_cmds[sent_idx].cmd) - 1);
        sent_cmds[sent_idx].cmd[sizeof(sent_cmds[sent_idx].cmd) - 1] = '\0';
        sent_cmds[sent_idx].confirmed = confirmed;
        sent_idx = (sent_idx + 1) % MAX_SENT_CMDS;
        xSemaphoreGive(data_mutex);
    }

    if (confirmed) {
        ESP_LOGI("UART", "Command sent successfully");
        httpd_resp_sendstr(req, "OK");
    } else {
        ESP_LOGW("UART", "Command send incomplete: %d/%zu bytes", written, strlen(msg));
        httpd_resp_sendstr(req, "ERROR: Send incomplete");
    }
    return ESP_OK;
}

static void json_escape(char *out, const char *in, size_t out_size)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < out_size - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"') {
            if (j < out_size - 2) {
                out[j++] = '\\';
                out[j++] = '"';
            }
        } else if (c == '\\') {
            if (j < out_size - 2) {
                out[j++] = '\\';
                out[j++] = '\\';
            }
        } else if (c == '\n') {
            if (j < out_size - 2) {
                out[j++] = '\\';
                out[j++] = 'n';
            }
        } else if (c == '\r') {
            if (j < out_size - 2) {
                out[j++] = '\\';
                out[j++] = 'r';
            }
        } else if (c == '\t') {
            if (j < out_size - 2) {
                out[j++] = '\\';
                out[j++] = 't';
            }
        } else if (c >= 32 && c < 127) {
            out[j++] = c;
        } else {
            if (j < out_size - 1) {
                out[j++] = '.';
            }
        }
    }
    out[j] = '\0';
}

static esp_err_t test_uart_handler(httpd_req_t *req)
{
    const char *test_cmd = "{\"N\":1,\"H\":\"001\",\"D1\":0,\"D2\":0,\"D3\":0,\"D4\":0,\"T\":1000}";
    ESP_LOGI("UART", "TEST: Sending test command: %s", test_cmd);
    
    size_t tx_free_before = 0;
    uart_get_tx_buffer_free_size(UART_NUM_2, &tx_free_before);
    ESP_LOGI("UART", "TEST: TX buffer free before: %zu bytes", tx_free_before);
    
    uart_flush(UART_NUM_2);
    
    int written = uart_write_bytes(UART_NUM_2, test_cmd, strlen(test_cmd));
    ESP_LOGI("UART", "TEST: uart_write_bytes returned: %d (expected: %zu)", written, strlen(test_cmd));
    
    for (int i = 0; i < strlen(test_cmd); i++) {
        ESP_LOGI("UART", "TEST:   byte[%d] = 0x%02x ('%c')", i, (unsigned char)test_cmd[i],
                 (test_cmd[i] >= 32 && test_cmd[i] < 127) ? test_cmd[i] : '.');
    }
    
    esp_err_t flush_ret = uart_flush(UART_NUM_2);
    if (flush_ret != ESP_OK) {
        ESP_LOGW("UART", "TEST: UART flush returned: %s (0x%x)", esp_err_to_name(flush_ret), flush_ret);
    }
    
    esp_err_t tx_wait = uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(1000));
    if (tx_wait != ESP_OK) {
        ESP_LOGE("UART", "TEST: TX wait failed: %s (0x%x)", esp_err_to_name(tx_wait), tx_wait);
        httpd_resp_sendstr(req, "ERROR: TX wait failed");
        return ESP_OK;
    }
    
    size_t tx_free_after = 0;
    uart_get_tx_buffer_free_size(UART_NUM_2, &tx_free_after);
    ESP_LOGI("UART", "TEST: TX buffer free after: %zu bytes (used: %zu)", tx_free_after, tx_free_before - tx_free_after);
    
    char response[256];
    snprintf(response, sizeof(response), "OK: Sent %d bytes, TX completed. Check serial logs for details.", written);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static esp_err_t uart_data_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uart_data_handler: CALLED");
    
    #define MAX_COPY_SENT 10
    #define MAX_COPY_RECV 20
    
    sent_cmd_t local_sent[MAX_COPY_SENT];
    recv_data_t local_recv[MAX_COPY_RECV];
    int local_sent_count = 0;
    int local_recv_count = 0;

    if (!data_mutex) {
        ESP_LOGW(TAG, "uart_data_handler: no mutex");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"sent\":[],\"received\":[],\"logs\":[]}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "uart_data_handler: taking mutex");
    TickType_t start = xTaskGetTickCount();
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        ESP_LOGW(TAG, "uart_data_handler: mutex timeout after %u ticks", (unsigned int)elapsed);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"sent\":[],\"received\":[],\"logs\":[]}");
        return ESP_OK;
    }
    TickType_t mutex_wait = xTaskGetTickCount() - start;
    ESP_LOGI(TAG, "uart_data_handler: mutex acquired after %u ticks", (unsigned int)mutex_wait);

    int total_sent_count = 0;
    int total_recv_count = 0;
    
    for (int i = 0; i < MAX_SENT_CMDS; i++) {
        if (sent_cmds[i].cmd[0] != '\0') {
            total_sent_count++;
        }
    }
    
    for (int i = 0; i < MAX_RECV_DATA; i++) {
        if (recv_data[i].data[0] != '\0') {
            total_recv_count++;
        }
    }
    
    ESP_LOGI(TAG, "uart_data_handler: copying sent commands");
    for (int i = 0; i < MAX_SENT_CMDS && local_sent_count < MAX_COPY_SENT; i++) {
        int idx = (sent_idx - MAX_SENT_CMDS + i + MAX_SENT_CMDS) % MAX_SENT_CMDS;
        if (sent_cmds[idx].cmd[0] != '\0') {
            local_sent[local_sent_count] = sent_cmds[idx];
            local_sent_count++;
        }
    }
    
    ESP_LOGI(TAG, "uart_data_handler: copying received data");
    for (int i = 0; i < MAX_RECV_DATA && local_recv_count < MAX_COPY_RECV; i++) {
        int idx = (recv_idx - MAX_RECV_DATA + i + MAX_RECV_DATA) % MAX_RECV_DATA;
        if (recv_data[idx].data[0] != '\0') {
            local_recv[local_recv_count] = recv_data[idx];
            local_recv_count++;
        }
    }
    
    ESP_LOGI(TAG, "uart_data_handler: clearing buffers");
    memset(sent_cmds, 0, sizeof(sent_cmds));
    memset(recv_data, 0, sizeof(recv_data));
    sent_idx = 0;
    recv_idx = 0;
    
    ESP_LOGI(TAG, "uart_data_handler: releasing mutex");
    xSemaphoreGive(data_mutex);
    ESP_LOGI(TAG, "uart_data_handler: mutex released");

    ESP_LOGI(TAG, "uart_data_handler: building JSON (sent=%d, recv=%d)", 
             local_sent_count, local_recv_count);
    
    uint32_t uptime_sec = xTaskGetTickCount() / configTICK_RATE_HZ;
    
    char json[4096];
    memset(json, 0, sizeof(json));
    int pos = 0;
    int remaining = sizeof(json) - 1;
    
    int n = snprintf(json + pos, remaining, "{\"status\":{\"uptime\":%u,\"sent_buf\":%d,\"recv_buf\":%d,\"sent_max\":%d,\"recv_max\":%d},\"sent\":[",
                     (unsigned int)uptime_sec, total_sent_count, total_recv_count, MAX_SENT_CMDS, MAX_RECV_DATA);
    if (n < 0 || n >= remaining) {
        ESP_LOGE(TAG, "uart_data_handler: JSON buffer overflow");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    pos += n;
    remaining -= n;
    
    bool first = true;
    for (int i = 0; i < local_sent_count && remaining > 100; i++) {
        if (!first) {
            n = snprintf(json + pos, remaining, ",");
            if (n < 0 || n >= remaining) break;
            pos += n;
            remaining -= n;
        }
        first = false;
        char escaped[256];
        json_escape(escaped, local_sent[i].cmd, sizeof(escaped));
        uint32_t sec = local_sent[i].timestamp / configTICK_RATE_HZ;
        n = snprintf(json + pos, remaining,
            "{\"cmd\":\"%s\",\"confirmed\":%s,\"time\":\"%u\"}",
            escaped,
            local_sent[i].confirmed ? "true" : "false",
            (unsigned int)sec);
        if (n < 0 || n >= remaining) break;
        pos += n;
        remaining -= n;
    }
    
    n = snprintf(json + pos, remaining, "],\"received\":[");
    if (n < 0 || n >= remaining) {
        ESP_LOGE(TAG, "uart_data_handler: JSON buffer overflow");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    pos += n;
    remaining -= n;
    
    first = true;
    for (int i = 0; i < local_recv_count && remaining > 100; i++) {
        if (!first) {
            n = snprintf(json + pos, remaining, ",");
            if (n < 0 || n >= remaining) break;
            pos += n;
            remaining -= n;
        }
        first = false;
        char escaped[512];
        json_escape(escaped, local_recv[i].data, sizeof(escaped));
        uint32_t sec = local_recv[i].timestamp / configTICK_RATE_HZ;
        n = snprintf(json + pos, remaining,
            "{\"data\":\"%s\",\"time\":\"%u\"}",
            escaped, (unsigned int)sec);
        if (n < 0 || n >= remaining) break;
        pos += n;
        remaining -= n;
    }
    
    n = snprintf(json + pos, remaining, "]}");
    if (n < 0 || n >= remaining) {
        ESP_LOGE(TAG, "uart_data_handler: JSON buffer overflow");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    pos += n;
    json[pos] = '\0';

    ESP_LOGI(TAG, "uart_data_handler: sending response (%d bytes)", pos);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, pos);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_data_handler: send failed, ret=%d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "uart_data_handler: response sent successfully");
    return ESP_OK;
}


static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    char part_buf[64];

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE("stream", "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
        
        vTaskDelay(pdMS_TO_TICKS(33));
    }

    return res;
}


static esp_err_t control_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, control_page_html, strlen(control_page_html));
    return ESP_OK;
}

static esp_err_t move_handler(httpd_req_t *req)
{
    char buf[32];
    int len = httpd_req_get_url_query_len(req) + 1;

    if (len > 1) {
        httpd_req_get_url_query_str(req, buf, len);

        char dir[16];
        if (httpd_query_key_value(buf, "dir", dir, sizeof(dir)) == ESP_OK) {
            ESP_LOGI("MOVE", "Direction: %s", dir);

            if (strcmp(dir, "up") == 0) {
                // your code here
            } else if (strcmp(dir, "down") == 0) {
                // your code here
            } else if (strcmp(dir, "left") == 0) {
                // your code here
            } else if (strcmp(dir, "right") == 0) {
                // your code here
            }
        }
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Failed to get frame");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    return ESP_OK;
}

void start_camera_server(void)
{
    if (!data_mutex) {
        data_mutex = xSemaphoreCreateMutex();
        if (!data_mutex) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return;
        }
    }
    memset(sent_cmds, 0, sizeof(sent_cmds));
    memset(recv_data, 0, sizeof(recv_data));
    memset(log_buffer, 0, sizeof(log_buffer));

    original_vprintf = esp_log_set_vprintf(log_vprintf);
    xTaskCreate(uart_read_task, "uart_read", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "UART read task created");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    config.stack_size = 16384;
    config.lru_purge_enable = true;
    config.task_priority = 5;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri = {
            .uri = "/jpg",
            .method = HTTP_GET,
            .handler = jpg_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri);

        httpd_uri_t control_page_uri = {
            .uri       = "/control",
            .method    = HTTP_GET,
            .handler   = control_page_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &control_page_uri);
        
        httpd_uri_t move_uri = {
            .uri       = "/move",
            .method    = HTTP_GET,
            .handler   = move_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &move_uri);

        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        
        httpd_uri_t send_uri = {
            .uri       = "/send",
            .method    = HTTP_GET,
            .handler   = send_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &send_uri);

        httpd_uri_t uart_data_uri = {
            .uri       = "/uart_data",
            .method    = HTTP_GET,
            .handler   = uart_data_handler,
            .user_ctx  = NULL
        };
        esp_err_t reg_ret = httpd_register_uri_handler(server, &uart_data_uri);
        ESP_LOGI(TAG, "Registered /uart_data handler, ret=%d", reg_ret);
        
        httpd_uri_t test_uart_uri = {
            .uri       = "/test_uart",
            .method    = HTTP_GET,
            .handler   = test_uart_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &test_uart_uri);
        ESP_LOGI(TAG, "Registered /test_uart handler");
        
        ESP_LOGI(TAG, "Camera server started on port %d", config.server_port);
    }
}


