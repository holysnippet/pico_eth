cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(pico_eth VERSION 0.1.1)

###
#set(PICO_DEOPTIMIZED_DEBUG 1)
###
pico_sdk_init()

add_library(pico_eth)

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/.
)

pico_generate_pio_header(${PROJECT_NAME}  
        ${CMAKE_CURRENT_LIST_DIR}/ethserdes.pio
)

target_sources(${PROJECT_NAME} PRIVATE
    nosys.c
    picopioeth.c
    ethpio_arch.c
)

# Link to pico_stdlib (gpio, time, etc. functions)
target_link_libraries(${PROJECT_NAME} PRIVATE
    hardware_dma
    hardware_irq
    hardware_pio
    pico_lwip
    pico_stdlib
    pico_time
    pico_sync
    pico_mem_ops
)
