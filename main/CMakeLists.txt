file(GLOB_RECURSE SOURCES "src/*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "."
        "src"
        "include"
    PRIV_REQUIRES 
        esp_wifi
        esp_netif
        nvs_flash
        esp_http_server
        wifi_provisioning
        esp32-camera
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CAMERA_MODEL_M5STACK_WIDE)

