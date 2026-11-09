cmake_minimum_required(VERSION 3.22.1)
project(vulkan_test LANGUAGES C CXX)

set(ANDROID_NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(android_native_app_glue STATIC ${ANDROID_NATIVE_APP_GLUE}/android_native_app_glue.c)
target_include_directories(android_native_app_glue PUBLIC ${ANDROID_NATIVE_APP_GLUE})

add_library(vulkan_test SHARED main.cpp)
target_include_directories(vulkan_test PRIVATE ${ANDROID_NATIVE_APP_GLUE})
target_link_libraries(vulkan_test PRIVATE android_native_app_glue vulkan android log)
