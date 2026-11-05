cmake_minimum_required(VERSION 3.13)

project(codecrafters-bittorrent)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCE_FILES src/*.c)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard

find_package(OpenSSL REQUIRED)

add_executable(bittorrent ${SOURCE_FILES})

target_include_directories(bittorrent PRIVATE include)
target_link_libraries(bittorrent PRIVATE OpenSSL::Crypto)
