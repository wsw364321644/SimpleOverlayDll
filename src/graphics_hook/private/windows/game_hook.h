#pragma once
#include <HOOK/hook_info.h>
#include <HOOK/window_info.h>
#include <simple_os_defs.h>
#include <RPC/JrpcHookHelperEvent.h>
#include <shared_mutex>
#include <SimpleValueStorage.h>

#ifndef ALIGN
#define ALIGN(bytes, align) (((bytes) + ((align)-1)) & ~((align)-1))
#endif
typedef struct HotKeyState_t {
    HotKeyList_t* ExpectList;
}hot_key_state_t;

typedef struct SharedWindowInfo_t {
    uint64_t Id{ 0 };
    intptr_t WindowTextureID;
    CommonHandle_t* ShmemHandle{ nullptr };
    bool bPreFocused{false};
    hook_window_info_t* Info{ nullptr };
}SharedWindowInfo_t;
typedef std::vector<std::shared_ptr<SharedWindowInfo_t>> SharedWindowInfos_t;

extern hook_info_t* global_hook_info;
extern HANDLE tex_mutexes[2];
extern HANDLE signal_restart ;
extern HANDLE signal_stop ;
extern HANDLE signal_ready;
extern HANDLE signal_exit;
extern HANDLE signal_init;
extern HWND dummy_window;
extern HINSTANCE dll_inst;


extern HotKeyList_t HotKeyList;
extern HotKeyState_t HotKeyState;
extern SharedWindowInfos_t SharedWindowInfos;

static inline void* get_offset_addr(HMODULE module, uint32_t offset)
{
    return (void*)((uintptr_t)module + (uintptr_t)offset);
}

static inline bool duplicate_handle(HANDLE* dst, HANDLE src)
{
    return !!DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(),
        dst, 0, false, DUPLICATE_SAME_ACCESS);
}

bool init_pipe(void);

bool init_hook_info(void);
void free_hook_info(void);
bool init_mutexes(void);
void free_mutexes(void);
bool init_signals(void);
void free_signals(void);
void init_dummy_window_thread(void);

void hook_thread_tick(void);

bool is_pipe_active(void);
///hooked new graphic device
bool is_capture_active(void);
bool is_capture_stopped(void);
bool is_capture_restarted(void);
///capture_active with frame interval
bool is_capture_ready(void);   
bool is_overlay_active();
bool capture_should_stop(void);
bool capture_should_init(void);


bool capture_init_shmem(shmem_data_t** data, HWND window,
    uint32_t cx, uint32_t cy, uint32_t pitch,
    uint32_t format, bool flip);

bool capture_init_shtex(shtex_data_t** data, HWND window,
    uint32_t cx, uint32_t cy, uint32_t format,
    bool flip, uintptr_t handle);

void shmem_copy_data(size_t idx, void* volatile data);
bool shmem_texture_data_lock(int idx);
void shmem_texture_data_unlock(int idx);

void capture_free(void);



void trigger_hotkey(std::string_view name);

void on_mouse_move_event(uint64_t id, mouse_motion_event_t e);
void on_mouse_button_event(uint64_t id, mouse_button_event_t e);
void on_mouse_wheel_event(uint64_t id, mouse_wheel_event_t e);
void on_keyboard_event(uint64_t id, keyboard_event_t e);
void on_window_event(uint64_t id, window_event_t e);
