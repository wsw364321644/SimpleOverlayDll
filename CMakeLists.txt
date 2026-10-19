cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(UTILPP_TAG c188e7f429177809563b691222e1c887f64ea7f5)
set(OVERLAY_UTIL_TAG 2524119b2fb3e412aec7f2ac2a807ec2bad1aec1)

project(simple_overlay_dll VERSION ${VERSION})
enable_language(CXX ASM)
include(GNUInstallDirs)

if(PROJECT_IS_TOP_LEVEL)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})

	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
	if(MSVC)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	endif()

	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		set(
			CMAKE_INSTALL_PREFIX
			"${CMAKE_SOURCE_DIR}/out/${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
			CACHE
				STRING
				"Directory to install after building"
			FORCE
		)
	endif()
endif()

FetchContent_Declare(
	OCmakeUtil
	GIT_REPOSITORY
	https://github.com/wsw364321644/OCmakeUtil.git
	GIT_TAG
	main
)
FetchContent_MakeAvailable(OCmakeUtil)

include(ProjectImport)
include(TargetImport)
include(ProgramImport)

ImportProject(Detours SSH TAG main)
ImportProject(libuv STATIC_CRT SSH TAG v1.52.1)
ImportProject(SDL2 STATIC_CRT SSH TAG release-2.32.10)

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
set(UTILPP_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	utilpp
	GIT_REPOSITORY
	https://github.com/wsw364321644/utilpp.git
	GIT_TAG
	${UTILPP_TAG}
)

set(OVERLAY_STATIC_CRT ON CACHE BOOL "" FORCE)
set(OVERLAY_GIT_SSH ON CACHE BOOL "" FORCE)
set(OVERLAY_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	overlay-util
	GIT_REPOSITORY
	https://github.com/wsw364321644/overlay-util.git
	GIT_TAG
	${OVERLAY_UTIL_TAG}
	GIT_SUBMODULES_RECURSE
	true
)
FetchContent_MakeAvailable(overlay-util utilpp)

add_subdirectory(src/graphics_hook)

ExportFromInstall(UTILPP)