cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(OCmakeUtil_TAG 799e0077a0a73875af6798aa6d8c79d5abb020ac)
set(UTILPP_TAG 637d10dc604f268494f1ffdff14828b1afa4e296)
set(OVERLAY_UTIL_TAG 1a0f180c89ff57a83cc2c71a12f184cccbf081e0)

project(simple_overlay_dll VERSION ${VERSION})
enable_language(CXX ASM)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
    "${CMAKE_BINARY_DIR}/rundir"
    CACHE STRING "Directory to install after building" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")

FetchContent_Declare(
  OCmakeUtil
  GIT_REPOSITORY https://github.com/wsw364321644/OCmakeUtil.git
  GIT_TAG ${OCmakeUtil_TAG}
)
FetchContent_MakeAvailable(OCmakeUtil)
include(ProjectImport)
include(TargetImport)
include(ProgramImport)

ImportProject(Detours SSH FIND TAG main)
ImportProject(libuv STATIC_CRT SSH TAG v1.51.0)
ImportProject(SDL2 STATIC_CRT SSH FIND TAG release-2.32.10)

# ImportProject(GLEW STATIC_CRT URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip) # 2.2.0
ImportTarget(imgui SSH TAG v1.92.5)

# find_package(GLEW REQUIRED)
# ############################
set(UTILPP_GIT_SSH ON CACHE BOOL "" FORCE)
set(UTILPP_STATIC_CRT ON CACHE BOOL "" FORCE)
set(UTILPP_ENABLE_IPC ON CACHE BOOL "" FORCE)
set(UTILPP_WITH_STATIC_LIBUV ON CACHE BOOL "" FORCE)
set(UTILPP_ENABLE_NET OFF CACHE BOOL "" FORCE)
set(UTILPP_ENABLE_STEAM OFF CACHE BOOL "" FORCE)
set(UTILPP_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(UTILPP_ENABLE_CPUID OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  utilpp
  GIT_REPOSITORY https://github.com/wsw364321644/utilpp.git
  GIT_TAG ${UTILPP_TAG}
)

set(UTILPP_WITH_STATIC_LIBUV ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  overlay-util
  GIT_REPOSITORY https://github.com/wsw364321644/overlay-util.git
  GIT_TAG ${OVERLAY_UTIL_TAG}
  GIT_SUBMODULES_RECURSE true
)
FetchContent_MakeAvailable(overlay-util utilpp)

add_subdirectory(src/graphics_hook)