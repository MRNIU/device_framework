# Copyright The virtio_driver Contributors

CMAKE_MINIMUM_REQUIRED (VERSION 3.27 FATAL_ERROR)

PROJECT (virtio_driver)

if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    message(
            FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif ()

add_subdirectory(${PROJECT_SOURCE_DIR}/src)
