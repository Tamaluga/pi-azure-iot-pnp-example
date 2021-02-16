# App

set(TARGET ${APP})

# Add files for the build process
set(SOURCES
        "main.c"
)
set(INCLUDE_DIRECTORIES
        "."
        "/usr/local/include/azureiot/"
)

link_directories(
        "/usr/local/lib/"
)
include_directories(${INCLUDE_DIRECTORIES})

add_compile_definitions("USE_PROV_MODULE_FULL")

# Create and configure target
add_executable(${TARGET}
        ${SOURCES}
)
target_include_directories(${TARGET} 
        PUBLIC 
                .
        )
target_link_libraries(${TARGET}

        ${LIBRARY_CORE}
        
        iothub_client_mqtt_transport
        iothub_client_mqtt_ws_transport
        iothub_client
        prov_device_ll_client
        prov_mqtt_transport
        prov_auth_client
        prov_device_client
        umqtt
        aziotsharedutil
        hsm_security_client
        parson

        ssl
        crypto
        curl
        pthread
        m
)