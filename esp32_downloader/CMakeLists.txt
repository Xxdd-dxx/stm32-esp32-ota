# ESP-IDF 工程顶层 CMakeLists.txt
#
# 之前报错 "does not appear to contain CMakeLists.txt" 就是因为缺了这个文件。
# VSCode 里装的是【ESP-IDF 扩展】（不是 PlatformIO），它点 Build 时会用
# ESP-IDF 的构建体系，要求工程根目录必须有 CMakeLists.txt。

cmake_minimum_required(VERSION 3.16)

# 指定芯片型号：ESP32-C3（RISC-V）。必须在 project() 之前设置。
set(IDF_TARGET "esp32c3" CACHE STRING "IDF Build Target")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_downloader)
