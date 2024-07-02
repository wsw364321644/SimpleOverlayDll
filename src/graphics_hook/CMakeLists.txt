set(TARGET_NAME graphics_hook)
enable_language(CXX ASM)

NewTargetSource()
AddSourceFolder(INCLUDE RECURSE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
AddSourceFolder(RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/private")
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${SourceFiles})
add_library(${TARGET_NAME} SHARED ${SourceFiles})

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "overlay")
set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${TARGET_NAME}$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64,32>")

set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS_DEBUG "/NODEFAULTLIB:LIBCMT.LIB")
set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS_RELEASE "/NODEFAULTLIB:LIBCMTD.LIB")

AddTargetInclude(${TARGET_NAME})

target_link_libraries(${TARGET_NAME} PRIVATE libuv::uv_a)
target_link_libraries(${TARGET_NAME} PRIVATE simple_utilpp_a)
target_link_libraries(${TARGET_NAME} PRIVATE Detours::Detours)
target_link_libraries(${TARGET_NAME} PRIVATE windows_util_a)
target_link_libraries(${TARGET_NAME} PRIVATE rpc_parser_a)
target_link_libraries(${TARGET_NAME} PRIVATE ipc_util_a)
target_link_libraries(${TARGET_NAME} PRIVATE simple_hook_helper_a)
target_link_libraries(${TARGET_NAME} PRIVATE simple_logger_a)
target_link_libraries(${TARGET_NAME} PRIVATE imgui_a)


install(TARGETS ${TARGET_NAME}
    EXPORT ${TARGET_NAME}Targets
    LIBRARY DESTINATION sdk/lib
    ARCHIVE DESTINATION sdk/lib
    RUNTIME DESTINATION sdk/bin
    PUBLIC_HEADER DESTINATION sdk/include
)