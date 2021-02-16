# Source

set(TARGET ${LIBRARY_CORE})
set(INCLUDE_DIRECTORIES
	"raspberry-pi-sense-hat"
	"azure-iot"
	"/usr/local/include/azureiot/"
)
set(SOURCES
	"raspberry-pi-sense-hat/bcm2835.c"
	"raspberry-pi-sense-hat/SHTC3.c"
	"raspberry-pi-sense-hat/LPS22HB.c"
	"azure-iot/pnp_device_client_ll.c"
	"azure-iot/pnp_dps_ll.c"
	"azure-iot/pnp_protocol.c"
)

add_compile_definitions("USE_PROV_MODULE_FULL") # Needed in azure-iot files

add_library(${TARGET} OBJECT ${SOURCES})
target_link_libraries(${TARGET} m)
target_include_directories(${TARGET} PUBLIC ${INCLUDE_DIRECTORIES})