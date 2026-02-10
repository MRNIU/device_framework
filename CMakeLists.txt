# Copyright The virtio_driver Contributors

CMAKE_MINIMUM_REQUIRED (VERSION 3.27 FATAL_ERROR)

PROJECT (virtio_driver)

IF(${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    MESSAGE (
        FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
ENDIF()

ADD_LIBRARY (${PROJECT_NAME} INTERFACE)
TARGET_INCLUDE_DIRECTORIES (${PROJECT_NAME} INTERFACE include)

ADD_SUBDIRECTORY (${PROJECT_SOURCE_DIR}/tests)
