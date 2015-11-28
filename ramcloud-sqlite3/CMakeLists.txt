cmake_minimum_required (VERSION 2.6.2)
message ("Running CMake version ${CMAKE_VERSION}")
project(ramcloud-sqlite3)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-exceptions -fno-strict-aliasing -fasynchronous-unwind-tables -fno-omit-frame-pointer -fvisibility=hidden -Wall -D_REENTRANT -D__EXTENSIONS__ -D_LARGEFILE64_SOURCE -D__LARGE64_FILES -fno-optimize-sibling-calls  -fprofile-arcs -ftest-coverage -fPIC")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -std=c99 -g -fno-strict-aliasing -fasynchronous-unwind-tables -fno-omit-frame-pointer -fvisibility=hidden -Wall -D_REENTRANT -D__EXTENSIONS__ -D_LARGEFILE64_SOURCE -D__LARGE64_FILES -fno-optimize-sibling-calls -fprofile-arcs -ftest-coverage")

set(src_list
    md5.h  md5.c
    sqlite3.h  sqlite3.c
    vfs-ramcloud.h  vfs-ramcloud.c
    vfs-ramcloud-stats.h
    )
#生成可执行文件
#add_executable()
#生成动态库文件
add_library(vfs_ramcloud SHARED ${src_list})
#生成静态库文件
add_library(vfs_ramclouds STATIC ${src_list})


