# Copyright The virtio_driver Contributors

add_library(${PROJECT_NAME} STATIC
        todo.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3;-Werror>
        $<$<CONFIG:Debug>:-O0;-g;-ggdb>
        -Wall
        -Wextra
        -ffreestanding
)

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "include/virtio_driver.h")
