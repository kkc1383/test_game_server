cmake_minimum_required(VERSION 3.10)
project(GameServer)

# C++17 표준
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 컴파일 옵션
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Boost
find_package(Boost 1.70 REQUIRED COMPONENTS system)
if(Boost_FOUND)
    message(STATUS "✅ Boost found: ${Boost_VERSION}")
else()
    message(FATAL_ERROR "❌ Boost not found!")
endif()

# nlohmann/json
find_package(nlohmann_json 3.2.0 REQUIRED)
if(nlohmann_json_FOUND)
    message(STATUS "✅ nlohmann_json found")
else()
    message(FATAL_ERROR "❌ nlohmann_json not found!")
endif()

# PhysX 경로 (5.5.0)
set(PHYSX_ROOT "$ENV{HOME}/PhysX-106.4-physx-5.5.0" CACHE PATH "PhysX root directory")

message(STATUS "PhysX Root: ${PHYSX_ROOT}")

# 인클루드 디렉토리
include_directories(
    ${PHYSX_ROOT}/physx/include
    ${PHYSX_ROOT}/pxshared/include
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ⭐ 정적 라이브러리 경로
link_directories(
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release
)

# 소스 파일
set(SOURCES main.cpp)

# 실행 파일 생성
add_executable(GameServer ${SOURCES})

# ⭐ 정적 라이브러리 링크
target_link_libraries(GameServer
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
    
    # PhysX 정적 라이브러리 (.a)
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release/libPhysX_static_64.a
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release/libPhysXCommon_static_64.a
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release/libPhysXFoundation_static_64.a
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release/libPhysXExtensions_static_64.a
    ${PHYSX_ROOT}/physx/bin/linux.x86_64/release/libPhysXPvdSDK_static_64.a
    
    # 시스템 라이브러리
    dl
    rt
    m
)

# C++17 기능
target_compile_features(GameServer PUBLIC cxx_std_17)

# 컴파일 정의
target_compile_definitions(GameServer PRIVATE
    PX_PHYSX_STATIC_LIB
    NDEBUG
)

# 빌드 정보
message(STATUS "=================================")
message(STATUS "Game Server Configuration")
message(STATUS "=================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "PhysX Root: ${PHYSX_ROOT}")
message(STATUS "Boost Version: ${Boost_VERSION}")
message(STATUS "Using Static PhysX Libraries")
message(STATUS "=================================")