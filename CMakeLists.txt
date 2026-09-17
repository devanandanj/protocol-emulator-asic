cmake_minimum_required(VERSION 3.16)
project(pemu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Warnings default; individual targets may extend.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Statically link the C++ runtime on MinGW so the resulting exes don't
# need libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll on PATH.
# Fixes STATUS_DLL_NOT_FOUND (0xC0000135) when CLion runs test binaries.
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

enable_testing()

add_subdirectory(model)
add_subdirectory(asm)
