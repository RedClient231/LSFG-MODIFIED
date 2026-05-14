cmake_minimum_required(VERSION 3.22.1)
project(lsfgdiag LANGUAGES CXX)

# Single-translation-unit native probe.
#
# vulkan.h ships with the NDK sysroot at <vulkan/vulkan.h>. We link libvulkan.so
# directly: minSdk=33 (Android 13) guarantees the loader is present. If the
# loader has zero ICDs (unlikely but possible on malformed firmwares), our
# vkCreateInstance call returns VK_ERROR_INCOMPATIBLE_DRIVER and we report
# that gracefully — no need for dlopen gymnastics.
add_library(lsfgdiag SHARED vk_probe.cpp)

target_link_libraries(lsfgdiag PRIVATE vulkan log android)
target_compile_options(lsfgdiag PRIVATE -Wall -Wextra -Wno-unused-parameter -Os -fvisibility=hidden)
set_target_properties(lsfgdiag PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
