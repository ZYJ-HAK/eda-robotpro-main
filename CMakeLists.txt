# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.0.3")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 2026-11-18 IDF6 兼容说明：
# IDF 6.0 起 driver 组件被拆分，driver/gpio.h、driver/i2c_master.h、
# driver/i2s_std.h、driver/spi_common.h 等头文件改由 esp_driver_* 组件提供，
# 且不再随 driver 组件传递。部分托管组件仍按旧路径引用。
# 注意：IDF 6.1.1 的 __COMPONENT_REQUIRES_COMMON 会被 idf_build_init() 覆盖，
# 无法通过公共属性注入，故改为在各托管组件 CMakeLists.txt 末尾追加
# target_link_libraries 补齐（原文件备份于 .idf6_compat_backup/）。

project(xiaozhi)

