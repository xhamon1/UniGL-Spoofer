cmake_minimum_required(VERSION 3.18)
project(unigl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(unigl SHARED src/main.cpp src/spoof.cpp src/hook.cpp src/profile.cpp)
target_compile_options(unigl PRIVATE -Wall -Wextra -O2 -fno-exceptions -fno-rtti
                       -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections)
target_link_libraries(unigl log)
# hide the statically linked libc++ symbols so they never clash with the host app
target_link_options(unigl PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384)
