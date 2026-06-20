cmake_minimum_required(VERSION 3.10)

project(class_arm C)

set(APP_NAME main)

set(APP_SOURCES
    main.c
    include/src/lcd.c
)

set(APP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include/inc
)

add_executable(${APP_NAME} ${APP_SOURCES})

target_include_directories(${APP_NAME} PRIVATE
    ${APP_INCLUDE_DIRS}
)

target_compile_features(${APP_NAME} PRIVATE c_std_99)

target_compile_options(${APP_NAME} PRIVATE
    -Wall
    -O2
)
