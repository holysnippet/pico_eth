cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(pico_eth VERSION 0.1.0)

include(CTest)
enable_testing()

###
#set(PICO_DEOPTIMIZED_DEBUG 1)
###
pico_sdk_init()

add_executable(pico_eth main.c)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

# lwIP : Not sure, allows LwIP to find lwipopts.h
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/includes)

pico_generate_pio_header(${PROJECT_NAME}  
        ${CMAKE_CURRENT_LIST_DIR}/includes/ethserdes.pio
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    main.c
    ntptest.c
    includes/nosys.c
    includes/picopioeth.c
    includes/ethpio_arch.c
)

# Link to pico_stdlib (gpio, time, etc. functions)
target_link_libraries(${PROJECT_NAME} PRIVATE
    hardware_dma
    hardware_irq
    hardware_pio
    pico_lwip
    pico_lwip_iperf
    pico_lwip_http
    pico_stdlib
    pico_time
    pico_sync
    pico_mem_ops
)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)