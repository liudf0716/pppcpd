cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

SET(CMAKE_CXX_FLAGS_DEBUG "-O0")
SET(CMAKE_CXX_FLAGS_DEBUG "-g")

include(FetchContent)
FetchContent_Declare(
		yaml-cpp
		GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
		GIT_SHALLOW	ON
		GIT_TAG yaml-cpp-0.6.3
	)
FetchContent_GetProperties(yaml-cpp)
if(NOT yaml-cpp_POPULATED)
	message(STATUS "Populating yaml-cpp...")
	FetchContent_Populate(yaml-cpp)
	
	# Add here options for yaml-cpp building
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "disable yaml tests")
	set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "disable yaml tools")
	set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "disable yaml contrib")

	add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
	message(STATUS "Done.")
endif()

include_directories(BEFORE SYSTEM ${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR}/include)

# VPP库和头文件配置
# 支持通过 -DVPP_INSTALL_DIR=path 手动指定VPP路径
# 或者自动检测系统路径和本地编译路径

if(NOT DEFINED VPP_INSTALL_DIR)
    # 首先检查本地编译的VPP
    set(LOCAL_VPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vpp/build-root/install-vpp-native/vpp")
    if(EXISTS "${LOCAL_VPP_DIR}/include/vapi/vapi.hpp")
        set(VPP_INSTALL_DIR "${LOCAL_VPP_DIR}")
        message(STATUS "Found local VPP installation: ${VPP_INSTALL_DIR}")
    # 然后检查系统安装的VPP
    elseif(EXISTS "/usr/include/vapi/vapi.hpp")
        set(VPP_INSTALL_DIR "/usr")
        message(STATUS "Found system VPP installation: ${VPP_INSTALL_DIR}")
    else()
        message(FATAL_ERROR "VPP installation not found. Please install VPP development packages or specify VPP_INSTALL_DIR manually")
    endif()
else()
    message(STATUS "Using manually specified VPP installation: ${VPP_INSTALL_DIR}")
endif()

# 添加VPP头文件路径
include_directories(BEFORE SYSTEM ${VPP_INSTALL_DIR}/include)

# 添加VPP库文件路径
if(EXISTS "${VPP_INSTALL_DIR}/lib/x86_64-linux-gnu")
    link_directories(${VPP_INSTALL_DIR}/lib/x86_64-linux-gnu)
    message(STATUS "Using VPP library path: ${VPP_INSTALL_DIR}/lib/x86_64-linux-gnu")
elseif(EXISTS "${VPP_INSTALL_DIR}/lib")
    link_directories(${VPP_INSTALL_DIR}/lib)
    message(STATUS "Using VPP library path: ${VPP_INSTALL_DIR}/lib")
else()
    message(FATAL_ERROR "VPP library directory not found in ${VPP_INSTALL_DIR}")
endif()

# set the project name
project(pppcpd)

file(GLOB SOURCES src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/pppctl.cpp)

# add the executable
add_executable(pppcpd ${SOURCES})
add_executable(pppctl ${CMAKE_CURRENT_SOURCE_DIR}/src/pppctl.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/string_helpers.cpp)

target_link_libraries(pppctl PUBLIC pthread)
target_link_libraries(pppctl PUBLIC boost_serialization)
target_link_libraries(pppcpd PUBLIC boost_system)
target_link_libraries(pppcpd PUBLIC boost_program_options)
target_link_libraries(pppcpd PUBLIC boost_serialization)
target_link_libraries(pppcpd PUBLIC boost_random)
target_link_libraries(pppcpd PUBLIC yaml-cpp)
target_link_libraries(pppcpd PUBLIC pthread)
target_link_libraries(pppcpd PUBLIC vapiclient)
target_link_libraries(pppcpd PUBLIC vppapiclient)

# 显示构建信息
message(STATUS "=== Build Configuration ===")
message(STATUS "VPP Install Directory: ${VPP_INSTALL_DIR}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "=============================")