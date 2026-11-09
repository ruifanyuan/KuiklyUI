# Tencent is pleased to support the open source community by making KuiklyUI
# available.
# Copyright (C) 2026 Tencent. All rights reserved.
# Licensed under the License of KuiklyUI;
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# https://github.com/Tencent-TDS/KuiklyUI/blob/main/LICENSE
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.18)
project(kuikly_thread_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(NATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../main/cpp" ABSOLUTE)
find_package(Threads REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_path(LIBUV_INCLUDE_DIR uv.h REQUIRED)
    find_library(LIBUV_LIBRARY uv REQUIRED)
else()
    find_path(LIBUV_INCLUDE_DIR uv.h HINTS /opt/homebrew/opt/libuv/include REQUIRED)
    find_library(LIBUV_LIBRARY uv HINTS /opt/homebrew/opt/libuv/lib REQUIRED)
endif()

set(KUIKLY_THREAD_SANITIZER "" CACHE STRING "Optional sanitizer: thread or address,undefined")
set(KUIKLY_THREAD_SOURCE "${NATIVE_ROOT}/libohos_render/foundation/thread/KRThread.cpp"
        CACHE FILEPATH "KRThread implementation to test or benchmark")
add_executable(krthread_test
        krthread_test.cpp
        "${KUIKLY_THREAD_SOURCE}")
add_executable(krthread_benchmark krthread_benchmark.cpp "${KUIKLY_THREAD_SOURCE}")
foreach(target krthread_test krthread_benchmark)
    target_include_directories(${target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${NATIVE_ROOT}" "${LIBUV_INCLUDE_DIR}")
    target_link_libraries(${target} PRIVATE Threads::Threads "${LIBUV_LIBRARY}")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        target_compile_definitions(${target} PRIVATE KUIKLY_THREAD_TEST_OHOS)
        target_compile_options(${target} PRIVATE -Wno-error=unused-command-line-argument)
        target_link_libraries(${target} PRIVATE qos)
    endif()
    if(APPLE)
        target_compile_options(${target} PRIVATE
                -include "${CMAKE_CURRENT_SOURCE_DIR}/stubs/pthread_compat.h")
    endif()
    if(KUIKLY_THREAD_SANITIZER)
        target_compile_options(${target} PRIVATE
                "-fsanitize=${KUIKLY_THREAD_SANITIZER}" -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE "-fsanitize=${KUIKLY_THREAD_SANITIZER}")
    endif()
endforeach()

enable_testing()
add_test(NAME krthread COMMAND krthread_test)
set_tests_properties(krthread PROPERTIES TIMEOUT 30)
