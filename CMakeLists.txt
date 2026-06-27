cmake_minimum_required(VERSION 3.10)

project(class_arm C)

set(APP_NAME main)

set(APP_SOURCES
    main.c
    lib/src/lcd.c
    lib/src/led.c
    lib/src/pic.c
    lib/src/pwm.c
    lib/src/touch.c
    lib/src/uarts.c
)

set(APP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/pic
    ${CMAKE_CURRENT_SOURCE_DIR}/frontend
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

add_executable(ts_ui_demo
    frontend/ts_ui_demo.c
    lib/src/lcd.c
    lib/src/led.c
    lib/src/pic.c
    lib/src/pwm.c
    lib/src/touch.c
    lib/src/uarts.c
    lib/src/word.c
)

target_include_directories(ts_ui_demo PRIVATE
    ${APP_INCLUDE_DIRS}
)

target_compile_features(ts_ui_demo PRIVATE c_std_99)

target_compile_options(ts_ui_demo PRIVATE
    -Wall
    -O2
)

target_link_libraries(ts_ui_demo PRIVATE
    m
)
