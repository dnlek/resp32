/*
 * Socket Server for Robot Control
 * Uses lwip sockets (ESP-IDF native)
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

static const char *TAG = "SocketServer";

#define SOCKET_PORT 100
static int s_socket_fd = -1;

// Handle client connection
static void socket_client_task(void *pvParameters)
{
    int client_fd = (int)pvParameters;
    char read_buf[512] = {0};
    char send_buf[512] = {0};
    uint8_t heartbeat_count = 0;
    bool heartbeat_status = false;
    bool data_begin = true;
    TickType_t last_heartbeat = 0;
    
    ESP_LOGI(TAG, "Client connected");
    
    while (1) {
        // Check for incoming data
        int len = recv(client_fd, read_buf, sizeof(read_buf) - 1, 0);
        if (len > 0) {
            read_buf[len] = '\0';
            ESP_LOGI(TAG, "Received: %.*s", len, read_buf);
            
            // Process received data
            for (int i = 0; i < len; i++) {
                char c = read_buf[i];
                if (data_begin && c == '{') {
                    data_begin = false;
                    memset(send_buf, 0, sizeof(send_buf));
                }
                if (!data_begin && c != ' ') {
                    strncat(send_buf, &c, 1);
                }
                if (!data_begin && c == '}') {
                    data_begin = true;
                    if (strcmp(send_buf, "{Heartbeat}") == 0) {
                        heartbeat_status = true;
                    } else {
                        // Send to robot via UART
                        uart_write_bytes(UART_NUM_2, send_buf, strlen(send_buf));
                    }
                    memset(send_buf, 0, sizeof(send_buf));
                }
            }
        } else if (len == 0) {
            ESP_LOGI(TAG, "Client disconnected");
            break;
        } else {
            // Error or timeout
            break;
        }
        
        // Check for data from robot (UART)
        uint8_t uart_buf[256];
        int uart_len = uart_read_bytes(UART_NUM_2, uart_buf, sizeof(uart_buf) - 1, 0);
        if (uart_len > 0) {
            uart_buf[uart_len] = '\0';
            // Look for complete message ending with }
            for (int i = 0; i < uart_len; i++) {
                if (uart_buf[i] == '}') {
                    send(client_fd, uart_buf, i + 1, 0);
                    ESP_LOGI(TAG, "Sent to client: %.*s", i + 1, uart_buf);
                    break;
                }
            }
        }
        
        // Send heartbeat every second
        TickType_t now = xTaskGetTickCount();
        if (now - last_heartbeat >= pdMS_TO_TICKS(1000)) {
            const char *heartbeat = "{Heartbeat}";
            send(client_fd, heartbeat, strlen(heartbeat), 0);
            
            if (heartbeat_status) {
                heartbeat_status = false;
                heartbeat_count = 0;
            } else {
                heartbeat_count++;
            }
            
            if (heartbeat_count > 3) {
                ESP_LOGW(TAG, "Heartbeat timeout, disconnecting");
                break;
            }
            
            last_heartbeat = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Send stop command to robot
    const char *stop_cmd = "{\"N\":100}";
    uart_write_bytes(UART_NUM_2, stop_cmd, strlen(stop_cmd));
    
    close(client_fd);
    ESP_LOGI(TAG, "Client connection closed");
    vTaskDelete(NULL);
}

// Socket server task
static void socket_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    int opt = 1;
    
    // Create socket
    s_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    // Set socket options
    setsockopt(s_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SOCKET_PORT);
    
    if (bind(s_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(s_socket_fd);
        vTaskDelete(NULL);
        return;
    }
    
    // Listen for connections
    if (listen(s_socket_fd, 5) < 0) {
        ESP_LOGE(TAG, "Failed to listen on socket");
        close(s_socket_fd);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Socket server started on port %d", SOCKET_PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept connection
        int client_fd = accept(s_socket_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            ESP_LOGE(TAG, "Failed to accept connection");
            continue;
        }
        
        ESP_LOGI(TAG, "New client connected from %s", inet_ntoa(client_addr.sin_addr));
        
        // Create task for this client
        xTaskCreate(socket_client_task, "socket_client", 4096, (void*)client_fd, 5, NULL);
    }
}

// Initialize socket server
void socket_server_init(void)
{
    xTaskCreate(socket_server_task, "socket_server", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Socket server initialized");
}

