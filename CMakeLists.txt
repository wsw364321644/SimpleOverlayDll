cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)

set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(OVERLAY_UTIL_TAG f5a345c2f171041c3db7d4399751cfb1b3f362c4)

option(OVERLAY_STATIC_CRT "OVERLAY STATIC CRT Build ." OFF)

if(OVERLAY_STATIC_CRT)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  endif()

  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
endif()

project(simple_overlay_dll VERSION ${VERSION})
enable_language(CXX ASM)

FetchContent_Declare(
  overlay-util
  GIT_REPOSITORY git@github.com:wsw364321644/overlay-util.git
  GIT_TAG ${OVERLAY_UTIL_TAG}
  GIT_SUBMODULES_RECURSE true
)
FetchContent_MakeAvailable(overlay-util)

ImportProject(Detours SSH FIND TAG 4b8c659f549b0ab21cf649377c7a84eb708f5e68) # 4.0.1
ImportTarget(imgui SSH TAG 6ccc561a2ab497ad4ae6ee1dbd3b992ffada35cb) # v1.90.6
find_package(LIBUV REQUIRED)
find_package(Detours REQUIRED)

add_subdirectory(src/graphics_hook)