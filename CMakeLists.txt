cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(UTILPP_TAG 16bfb770d1a121788de736fd30a3c546b5b59b05)
set(OCmakeUtil_TAG 8f3b415d53ee0cef2d8fe59dfa2eb92d62156c56)
set(OVERLAY_UTIL_TAG 296d7125722dcabc6167c3a8d576582a61c2220a)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
    "${CMAKE_BINARY_DIR}/rundir"
    CACHE STRING "Directory to install after building" FORCE)
endif()

project(simple_overlay_dll VERSION ${VERSION})
enable_language(CXX ASM)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")

FetchContent_Declare(
  OCmakeUtil
  GIT_REPOSITORY git@ssh.github.com:wsw364321644/OCmakeUtil.git
  GIT_TAG ${OCmakeUtil_TAG}
)

FetchContent_Declare(
  utilpp
  GIT_REPOSITORY git@ssh.github.com:wsw364321644/utilpp.git
  GIT_TAG ${UTILPP_TAG}
)

set(UTILPP_WITH_STATIC_LIBUV ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  overlay-util
  GIT_REPOSITORY git@ssh.github.com:wsw364321644/overlay-util.git
  GIT_TAG ${OVERLAY_UTIL_TAG}
  GIT_SUBMODULES_RECURSE true
)
FetchContent_MakeAvailable(overlay-util OCmakeUtil utilpp)

ImportProject(Detours SSH FIND TAG 4b8c659f549b0ab21cf649377c7a84eb708f5e68) # 4.0.1
ImportTarget(imgui SSH TAG 6ccc561a2ab497ad4ae6ee1dbd3b992ffada35cb) # v1.90.6
find_package(LIBUV REQUIRED)
find_package(Detours REQUIRED)

add_subdirectory(src/graphics_hook)