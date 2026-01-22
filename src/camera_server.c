#include "camera_server.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/uart.h"

static const char *TAG = "CamServer";

static const char* control_page_html =
"<!DOCTYPE html>"
"<html>"
"<head><title>Robi Control</title></head>"
"<body style='text-align:center; font-family:sans-serif;'>"

"<h2>Robi Direction Control</h2>"

"<img src='/stream' style='width:320px;height:auto;border:1px solid #444;' />"

"<br><br>"

"<button onclick=\"fetch('/move?dir=up')\" style='width:100px;height:50px;'>Up</button><br><br>"
"<button onclick=\"fetch('/move?dir=left')\" style='width:100px;height:50px;'>Left</button>"
"<button onclick=\"fetch('/move?dir=right')\" style='width:100px;height:50px;'>Right</button><br><br>"
"<button onclick=\"fetch('/move?dir=down')\" style='width:100px;height:50px;'>Down</button>"

"<br/><br/>"

"<h3>Send Command</h3>"
"<input id='cmd' type='text' style='width:200px;height:30px;font-size:16px;' />"
"<button onclick=\"sendCmd()\" style='height:36px;font-size:16px;'>Send</button>"

"<script>"
"function sendCmd(){"
"  let v = document.getElementById('cmd').value;"
"  fetch('/send?msg=' + encodeURIComponent(v));"
"}"
"</script>"

"</body>"
"</html>";


static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t send_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_get_url_query_len(req) + 1;

    if (len > 1) {
        httpd_req_get_url_query_str(req, buf, len);

        char msg[100];
        if (httpd_query_key_value(buf, "msg", msg, sizeof(msg)) == ESP_OK) {
            ESP_LOGI("UART", "Sending to UART: %s", msg);

            uart_write_bytes(UART_NUM_2, msg, strlen(msg));
            uart_write_bytes(UART_NUM_2, "\n", 1); // optional newline
        }
    }

    httpd_resp_sendstr(req, "OK");
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
            return ESP_FAIL;
        }

        // Write boundary
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        // Write header
        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        // Write JPEG frame
        res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
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
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

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
        
        ESP_LOGI(TAG, "Camera server started on port %d", config.server_port);
    }
}


