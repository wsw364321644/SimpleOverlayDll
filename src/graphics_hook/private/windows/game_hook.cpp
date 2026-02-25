#include "game_hook.h"
#include "windows_capture.h"
#include "input_hook.h"

#include <RPC/JrpcHookHelperEvent.h>
#include <RPC/JrpcHookHelper.h>
#include <RPC/MessageFactory.h>

#include <sm_util.h>
#include <mutex_util.h>
#include <event_util.h>
#include <std_ext.h>
#include <jrpc_parser.h>
#include <sm_util.h>
#include <LoggerHelper.h>
#include <HOOK/hook_synchronized.h>
#include <windows_helper.h>
#include <psapi.h>
#include <simple_time.h>
#include <string>
#include <cinttypes>
#include <memory>

std::shared_ptr<IMessageClient> PIpcClient;
std::shared_ptr < IMessageProcesser> PMessageProcesser;
std::shared_ptr<IRPCProcesser> PRPCProcesser;


CommonHandlePtr_t hook_info_handle{};
hook_info_t* global_hook_info{ nullptr };
hot_key_list_handle_t hot_key_list_handle;
CommonHandlePtr_t tex_mutexes[2];
CommonHandlePtr_t signal_restart;
CommonHandlePtr_t signal_stop;
CommonHandlePtr_t signal_ready;
CommonHandlePtr_t signal_exit;
CommonHandlePtr_t signal_init;
CommonHandlePtr_t ready_mutex;
HWND dummy_window = NULL;
HINSTANCE dll_inst = NULL;
volatile bool active = false;
volatile bool pipe_active = false;
char process_name[MAX_PATH] = { 0 };

static unsigned int shmem_id_counter = 0;
static CommonHandlePtr_t shmem_handle = NULL;
static void* shmem_info = 0;

static SharedWindowInfos_t HookThreadSharedWindowInfos;
SharedWindowInfos_t SharedWindowInfos;
static SimpleValueHandle_t SharedWindowInfosHandle;
static SimpleValueHandle_t OverlayEnableHandle;
static inline void close_handle(HANDLE* handle)
{
    if (*handle) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

static inline bool set_capture_ready_mutex(bool bready)
{
    bool success{ true };
    static bool inited{ false };
    std::error_code ec;
    if (bready) {
        char nameBuf[64] = { 0 };
        std::snprintf(nameBuf, 64, "%s%lu", WINDOW_HOOK_KEEPALIVE, GetCurrentProcessId());
        ready_mutex = utilpp::CreateProcMutex(nameBuf, ec);
        success = ready_mutex.IsValid();
    }
    else {
        if (ready_mutex) {
            utilpp::CloseProcMutex(ready_mutex,ec);
        }
    }
    return success;
}

static inline bool is_capture_server_alive(void)
{
    static char keepalive_name[64] = { 0 };
    static bool inited{ false };
    if (!inited) {
        std::snprintf(keepalive_name, 64, "%s%lu", WINDOW_HOOK_KEEPALIVE, GetCurrentProcessId());
        inited = true;
    }
    std::error_code ec;
    auto handle = utilpp::OpenProcMutex(keepalive_name, ec);
    const bool success = handle.IsValid();
    if (success)
        utilpp::CloseProcMutex(handle,ec);
    return success;
}

static inline bool capture_stopped(void)
{
    std::error_code ec;
    return utilpp::PeekProcEvent(signal_stop, ec);
}

void init_new_pipe_client(IMessageClient* pClient) {

    PMessageProcesser = std::make_shared<FMessageProcesser>(pClient);
    PRPCProcesser = std::make_shared<FJRPCProcesser>(PMessageProcesser.get());

    auto HookHelperEventInterface = PRPCProcesser->GetInterface<JRPCHookHelperEventAPI>();
    HookHelperEventInterface->RegisterHotkeyListUpdate(
        [](HotKeyList_t& inHotKeyList) {
            SimpleValueStorage::SetValue(HotKeyListHandle, std::move(inHotKeyList));
        }
    );
    HookHelperEventInterface->RegisterInputStateUpdate(
        [](overlay_ime_event_t& imeEvent) {
            SimpleValueStorage::SetValue(ImeEventHandle, std::move(imeEvent));
        });
    auto HookHelperInterface = PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
    HookHelperInterface->ConnectToHost(GetCurrentProcessId(), GetCommandLineA(),
        [](RPCHandle_t) {
            pipe_active = true;
        },
        [pClient](RPCHandle_t, int64_t, std::string_view, std::string_view) {
            pClient->Disconnect();
        }
    );
    HookHelperInterface->RegisterAddWindow([HookHelperInterface](RPCHandle_t handle, uint64_t id, std::string_view shared_mem_name) {
        auto shmemHandle = OpenSharedMemory(shared_mem_name.data());
        if (!shmemHandle) {
            HookHelperInterface->RespondError(handle, -1, "can't open shared mem");
            return;
        }
        auto info = (hook_window_info_t*)MapSharedMemory(shmemHandle);

        HookThreadSharedWindowInfos.emplace_back(std::make_shared<SharedWindowInfo_t>(SharedWindowInfo_t{ .Id = id,.ShmemHandle = shmemHandle,.Info = info ,
            .SharedHandleCache = info->shared_handle }));
        SimpleValueStorage::SetValue(SharedWindowInfosHandle, HookThreadSharedWindowInfos);
        HookHelperInterface->RespondAddWindow(handle);
        });

    HookHelperInterface->RegisterRemoveWindow([HookHelperInterface](RPCHandle_t handle, uint64_t id) {
        auto itr = std::find_if(HookThreadSharedWindowInfos.begin(), HookThreadSharedWindowInfos.end(),
            [id](const std::shared_ptr<SharedWindowInfo_t>& info) {
                return info->Id == id;
            });
        if (itr == HookThreadSharedWindowInfos.end()) {
            HookHelperInterface->RespondError(handle, -1, "id not exist");
            return;
        }
        (*itr)->RemoveHandle = handle;
        HookThreadSharedWindowInfos.erase(itr);
        SimpleValueStorage::SetValue(SharedWindowInfosHandle, HookThreadSharedWindowInfos);
        });

    HookHelperInterface->RegisterUpdateWindowTexture([HookHelperInterface](RPCHandle_t handle, uint64_t id) {
        auto itr = std::find_if(HookThreadSharedWindowInfos.begin(), HookThreadSharedWindowInfos.end(),
            [id](const std::shared_ptr<SharedWindowInfo_t>& info) {
                return info->Id == id;
            });
        if (itr == HookThreadSharedWindowInfos.end()) {
            HookHelperInterface->RespondError(handle, -1, "id not exist");
            return;
        }
        (*itr)->TextureUpdateHandle = handle;
        //(*itr)->SharedHandleCache = (*itr)->Info->shared_handle;
        async_overlay(SHARED_WINDOW_TEXTURE_UPDATE, NULL);
        });
}

bool init_pipe(void)
{
    if (PIpcClient) {
        return false;
    }
    PIpcClient = NewMessageClient({ EMessageFoundation::LIBUV });
    PIpcClient->AddOnConnectDelegate(init_new_pipe_client);
    PIpcClient->AddOnDisconnectDelegate([](IMessageSession* pClient) {
        pipe_active = false;
        PRPCProcesser = nullptr;
        PMessageProcesser = nullptr;
        HookThreadSharedWindowInfos.clear();
        SimpleValueStorage::SetValue(SharedWindowInfosHandle, HookThreadSharedWindowInfos);
        SimpleValueStorage::SetValue(OverlayEnableHandle, false);
        });

    return true;
}

void hook_thread_tick(void) {
    switch (PIpcClient->GetConnectionState()) {
    case EMessageConnectionState::Idle:
        PIpcClient->Connect(EMessageConnectionType::EMCT_IPC, HOOK_IPC_PIPE);
        break;
    default:
        PIpcClient->Tick(0);
    }
    SimpleValueStorage::Tick(0);
}

bool init_hook_info(void)
{
    auto processID = GetCurrentProcessId();

    get_process_file_base_name_from_handle(GetCurrentProcess(), NULL, process_name, MAX_PATH);

    SharedWindowInfosHandle = SimpleValueStorage::RegisterValue<SharedWindowInfos_t>();

    SimpleValueStorage::RegisterValueChange(SharedWindowInfosHandle,
        [](SimpleValueHandle_t, const void*, const void*) {
            async_overlay(SHARED_WINDOW_INFOS_UPDATE, NULL);
        }
    );
    register_overlay_async_processor(SHARED_WINDOW_INFOS_UPDATE,
        [](LPARAM lParam)->bool {
            auto oldSharedWindowInfos = SharedWindowInfos;
            if (!SimpleValueStorage::GetValue(SharedWindowInfosHandle, SharedWindowInfos)) {
                async_overlay(SHARED_WINDOW_INFOS_UPDATE, NULL);
            }
            for (auto oldInfo : oldSharedWindowInfos) {
                auto itr = std::find_if(SharedWindowInfos.begin(), SharedWindowInfos.end(),
                    [oldInfo](std::shared_ptr<SharedWindowInfo_t> newinfo) {
                        return newinfo->Id == oldInfo->Id;
                    });
                if (itr == SharedWindowInfos.end() && oldInfo->RemoveHandle.IsValid()) {
                    if (oldInfo->Info->bNT_shared) {
                        if (oldInfo->Info->shared_handle) {
                            CloseHandle(HANDLE(oldInfo->Info->shared_handle));
                        }
                        if (oldInfo->SharedHandleCache && oldInfo->Info->shared_handle != oldInfo->SharedHandleCache) {
                            CloseHandle(HANDLE(oldInfo->SharedHandleCache));
                        }
                    }
                    if (PRPCProcesser) {
                        auto HookHelperInterface = PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
                        HookHelperInterface->RespondRemoveWindow(oldInfo->RemoveHandle);
                    }
                    continue;
                }
            }

            return true;
        }
    );
    register_overlay_async_processor(SHARED_WINDOW_TEXTURE_UPDATE,
        [](LPARAM lParam)->bool {
            for (auto info : SharedWindowInfos) {
                if (info->SharedHandleCache == info->Info->shared_handle) {
                    continue;
                }
                if (!info->Info->bNT_shared) {
                    continue;
                }
                CloseHandle(HANDLE(info->SharedHandleCache));
                info->SharedHandleCache = info->Info->shared_handle;
                if (PRPCProcesser) {
                    auto HookHelperInterface = PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
                    HookHelperInterface->RespondUpdateWindowTexture(info->TextureUpdateHandle);
                }
                break;
            }

            return true;
        }
    );
    SimpleValueStorage::RegisterValueChange(OverlayEnableHandle,
        [](SimpleValueHandle_t, const void*, const void*) {
            async_overlay(OVERLAY_ENABLE, NULL);
        }
    );
    register_overlay_async_processor(OVERLAY_ENABLE,
        [](LPARAM lParam)->bool {
            if (!SimpleValueStorage::GetValue(OverlayEnableHandle, global_hook_info->bOverlayEnabled)) {
                async_overlay(OVERLAY_ENABLE, NULL);
            }
            return true;
        }
    );
    hook_info_handle = CreateSharedMemory(GetNamePlusID(SHMEM_HOOK_INFO, processID).c_str(), sizeof(hook_info_t));

    if (!hook_info_handle ) {
        return false;
    }
    global_hook_info = (hook_info_t*)MapSharedMemory(hook_info_handle);

    return true;
}

void free_hook_info(void)
{
    if (global_hook_info) {
        UnmapSharedMemory(hook_info_handle,global_hook_info);
    }
    if (hook_info_handle) {
        CloseSharedMemory(hook_info_handle);
    }
}

bool init_mutexes(void)
{
    std::error_code ec;
    DWORD pid = GetCurrentProcessId();
    char nameBuf[64] = { 0 };
    std::snprintf(nameBuf, 64, "%s%lu", MUTEX_TEXTURE1, pid);
    tex_mutexes[0] = utilpp::CreateProcMutex(nameBuf, ec);
    if (!tex_mutexes[0]) {
        return false;
    }
    std::snprintf(nameBuf, 64, "%s%lu", MUTEX_TEXTURE2, pid);
    tex_mutexes[1] = utilpp::CreateProcMutex(nameBuf, ec);
    if (!tex_mutexes[1]) {
        return false;
    }

    return true;
}

void free_mutexes(void)
{
    std::error_code ec;
    utilpp::CloseProcMutex(tex_mutexes[1],ec);
    utilpp::CloseProcMutex(tex_mutexes[0],ec);
}


bool init_signals(void)
{
    std::error_code ec;
    DWORD pid = GetCurrentProcessId();
    char nameBuf[64] = { 0 };

    std::snprintf(nameBuf, 64, "%s%lu", EVENT_CAPTURE_RESTART, pid);
    signal_restart = utilpp::CreateProcEvent(nameBuf,ec);
    if (!signal_restart) {
        return false;
    }

    std::snprintf(nameBuf, 64, "%s%lu", EVENT_CAPTURE_STOP, pid);
    signal_stop = utilpp::CreateProcEvent(nameBuf, ec);
    if (!signal_stop) {
        return false;
    }

    std::snprintf(nameBuf, 64, "%s%lu", EVENT_HOOK_READY, pid);
    signal_ready = utilpp::CreateProcEvent(nameBuf, ec);
    if (!signal_ready) {
        return false;
    }

    std::snprintf(nameBuf, 64, "%s%lu", EVENT_HOOK_EXIT, pid);
    signal_exit = utilpp::CreateProcEvent(nameBuf, ec);
    if (!signal_exit) {
        return false;
    }

    std::snprintf(nameBuf, 64, "%s%lu", EVENT_HOOK_INIT, pid);
    signal_init = utilpp::CreateProcEvent(nameBuf, ec);
    if (!signal_init) {
        return false;
    }

    return true;
}

void free_signals(void)
{
    std::error_code ec;
    utilpp::CloseProcEvent(signal_exit, ec);
    utilpp::CloseProcEvent(signal_ready, ec);
    utilpp::CloseProcEvent(signal_stop, ec);
    utilpp::CloseProcEvent(signal_restart, ec);
}




#define DEF_FLAGS (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
static DWORD WINAPI dummy_window_thread(LPVOID unused)
{
    static const char dummy_window_class[] = "temp_d3d_window_sonkwo";
    WNDCLASSA wc;
    MSG msg;

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.hInstance = dll_inst;
    wc.lpfnWndProc = (WNDPROC)DefWindowProc;
    wc.lpszClassName = dummy_window_class;

    if (!RegisterClassA(&wc)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "Failed to create temp D3D window class: {}", GetLastError());
        return 0;
    }

    dummy_window = CreateWindowExA(0, dummy_window_class, "Temp Window",
        DEF_FLAGS, 0, 0, 1, 1, NULL, NULL,
        dll_inst, NULL);
    if (!dummy_window) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "Failed to create temp D3D window: {}", GetLastError());
        return 0;
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    (void)unused;
    return 0;
}

void init_dummy_window_thread(void)
{
    HANDLE thread =
        CreateThread(NULL, 0, dummy_window_thread, NULL, 0, NULL);
    if (!thread) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "Failed to create temp D3D window thread: %lu", GetLastError());
        return;
    }

    CloseHandle(thread);
}

bool is_pipe_active(void)
{
    return pipe_active;
}

bool is_capture_active(void)
{
    return active;
}

bool is_capture_stopped(void)
{
    std::error_code ec;
    return utilpp::PeekProcEvent(signal_stop, ec);
}

bool is_capture_restarted(void)
{
    std::error_code ec;
    return utilpp::PeekProcEvent(signal_restart, ec);
}


bool capture_should_stop(void)
{
    bool stop_requested = false;

    if (is_capture_active()) {
        static uint64_t last_keepalive_check = 0;
        uint64_t cur_time = os_gettime_ns();
        bool alive = true;

        if (cur_time - last_keepalive_check > 5000000000) {
            alive = is_capture_server_alive();
            last_keepalive_check = cur_time;
        }

        stop_requested = capture_stopped() || !alive;
    }

    return stop_requested;
}


bool capture_should_init(void)
{
    bool should_init = false;

    if (!is_capture_active()) {
        if (is_capture_restarted()) {
            if (is_capture_server_alive()) {
                should_init = true;
            }
            else {
                SIMPLELOG_LOGGER_TRACE(nullptr,
                    "capture_should_init: inactive, restarted, not alive");
            }
        }
        else {
            SIMPLELOG_LOGGER_TRACE(nullptr,
                "capture_should_init: inactive, not restarted");
        }
    }

    return should_init;
}


static inline bool frame_ready(uint64_t interval)
{
    static uint64_t last_time = 0;
    uint64_t elapsed;
    uint64_t t;

    if (!interval) {
        return true;
    }

    t = os_gettime_ns();
    elapsed = t - last_time;

    if (elapsed < interval) {
        return false;
    }

    last_time = (elapsed > interval * 2) ? t : last_time + interval;
    return true;
}

bool is_capture_ready(void)
{
    return is_capture_active() &&
        frame_ready(global_hook_info->frame_interval);
}

bool is_overlay_active()
{
    return global_hook_info->bOverlayEnabled;
}


/////////////////////////////////////////////////copy thread///////////////////////////////////////////////////////////////


typedef  struct thread_data_t {
    CRITICAL_SECTION mutexes[NUM_BUFFERS];
    CRITICAL_SECTION data_mutex;
    void* volatile cur_data;
    uint8_t* shmem_textures[2];
    HANDLE copy_thread;
    HANDLE copy_event;
    HANDLE stop_event;
    volatile int cur_tex;
    unsigned int pitch;
    unsigned int cy;
    volatile bool locked_textures[NUM_BUFFERS];
}thread_data_t;
static thread_data_t thread_data = { 0 };

static inline bool init_shared_info(size_t size, HWND window)
{
    char name[64];
    HWND top = GetAncestor(window, GA_ROOT);

    std::snprintf(name, 64, SHMEM_TEXTURE "_%" PRIu64 "_%u",
        (uint64_t)(uintptr_t)top, ++shmem_id_counter);

    shmem_handle = CreateSharedMemory(name, size);

    if (!shmem_handle) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "init_shared_info: Failed to create shared memory: {}", GetLastError());
        return false;
    }

    shmem_info = MapSharedMemory(shmem_handle);
    if (!shmem_info) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "init_shared_info: Failed to map shared memory: {}",
            GetLastError());
        return false;
    }

    return true;
}

static inline int try_lock_shmem_tex(int id)
{
    std::error_code ec;
    int next = id == 0 ? 1 : 0;
    DWORD wait_result = WAIT_FAILED;

    if (utilpp::TryLockProcMutex(tex_mutexes[id], ec)) {
        return id;
    }

    if (utilpp::TryLockProcMutex(tex_mutexes[next], ec)) {
        return next;
    }

    return -1;
}

static inline void unlock_shmem_tex(int id)
{
    if (id != -1) {
        std::error_code ec;
        utilpp::ReleaseProcMutex(tex_mutexes[id], ec);
    }
}

static DWORD CALLBACK copy_thread(LPVOID unused)
{
    uint32_t pitch = thread_data.pitch;
    uint32_t cy = thread_data.cy;
    HANDLE events[2] = { NULL, NULL };
    int shmem_id = 0;

    if (!duplicate_handle(&events[0], thread_data.copy_event)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "copy_thread: Failed to duplicate copy event: {}",
            GetLastError());
        return 0;
    }

    if (!duplicate_handle(&events[1], thread_data.stop_event)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "copy_thread: Failed to duplicate stop event: {}",
            GetLastError());
        goto finish;
    }

    for (;;) {
        int copy_tex;
        void* cur_data;

        DWORD ret = WaitForMultipleObjects(2, events, false, INFINITE);
        if (ret != WAIT_OBJECT_0) {
            break;
        }

        EnterCriticalSection(&thread_data.data_mutex);
        copy_tex = thread_data.cur_tex;
        cur_data = thread_data.cur_data;
        LeaveCriticalSection(&thread_data.data_mutex);

        if (copy_tex < NUM_BUFFERS && !!cur_data) {
            EnterCriticalSection(&thread_data.mutexes[copy_tex]);

            int lock_id = try_lock_shmem_tex(shmem_id);
            if (lock_id != -1) {
                memcpy(thread_data.shmem_textures[lock_id],
                    cur_data, (size_t)pitch * (size_t)cy);

                unlock_shmem_tex(lock_id);
                ((shmem_data_t*)shmem_info)->last_tex =
                    lock_id;

                shmem_id = lock_id == 0 ? 1 : 0;
            }

            LeaveCriticalSection(&thread_data.mutexes[copy_tex]);
        }
    }

finish:
    for (size_t i = 0; i < 2; i++) {
        if (events[i]) {
            CloseHandle(events[i]);
        }
    }

    (void)unused;
    return 0;
}



static inline bool init_shmem_thread(uint32_t pitch, uint32_t cy)
{
    shmem_data_t* data = (shmem_data_t*)shmem_info;

    thread_data.pitch = pitch;
    thread_data.cy = cy;
    thread_data.shmem_textures[0] = (uint8_t*)data + data->tex1_offset;
    thread_data.shmem_textures[1] = (uint8_t*)data + data->tex2_offset;

    thread_data.copy_event = CreateEvent(NULL, false, false, NULL);
    if (!thread_data.copy_event) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "init_shmem_thread: Failed to create copy event:{}",
            GetLastError());
        return false;
    }

    thread_data.stop_event = CreateEvent(NULL, true, false, NULL);
    if (!thread_data.stop_event) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "init_shmem_thread: Failed to create stop event: {}",
            GetLastError());
        return false;
    }

    for (size_t i = 0; i < NUM_BUFFERS; i++) {
        InitializeCriticalSection(&thread_data.mutexes[i]);
    }

    InitializeCriticalSection(&thread_data.data_mutex);

    thread_data.copy_thread =
        CreateThread(NULL, 0, copy_thread, NULL, 0, NULL);
    if (!thread_data.copy_thread) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "init_shmem_thread: Failed to create thread: {}",
            GetLastError());
        return false;
    }
    return true;
}

bool capture_init_shmem(shmem_data_t** data, HWND window, uint32_t cx,
    uint32_t cy, uint32_t pitch, uint32_t format, bool flip)
{
    uint32_t tex_size = cy * pitch;
    uint32_t aligned_header = ALIGN(sizeof(shmem_data_t), 32);
    uint32_t aligned_tex = ALIGN(tex_size, 32);
    uint32_t total_size = aligned_header + aligned_tex * 2 + 32;
    uintptr_t align_pos;

    if (!init_shared_info(total_size, window)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "capture_init_shmem: Failed to initialize memory");
        return false;
    }

    *data = (shmem_data_t*)shmem_info;

    /* to ensure fast copy rate, align texture data to 256bit addresses */
    align_pos = (uintptr_t)shmem_info;
    align_pos += aligned_header;
    align_pos &= ~(32 - 1);
    align_pos -= (uintptr_t)shmem_info;

    if (align_pos < sizeof(shmem_data_t))
        align_pos += 32;

    (*data)->last_tex = -1;
    (*data)->tex1_offset = (uint32_t)align_pos;
    (*data)->tex2_offset = (*data)->tex1_offset + aligned_tex;

    global_hook_info->hook_ver_major = HOOK_VER_MAJOR;
    global_hook_info->hook_ver_minor = HOOK_VER_MINOR;
    global_hook_info->window = (uint32_t)(uintptr_t)window;
    global_hook_info->type = CAPTURE_TYPE_MEMORY;
    global_hook_info->format = format;
    global_hook_info->flip = flip;
    global_hook_info->map_id = shmem_id_counter;
    global_hook_info->map_size = total_size;
    global_hook_info->pitch = pitch;
    global_hook_info->cx = cx;
    global_hook_info->cy = cy;
    global_hook_info->UNUSED_base_cx = cx;
    global_hook_info->UNUSED_base_cy = cy;

    if (!init_shmem_thread(pitch, cy)) {
        return false;
    }

    std::error_code ec;
    if (!utilpp::SetProcEvent(signal_ready, ec)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "capture_init_shmem: Failed to signal ready: %d",
            GetLastError());
        return false;
    }

    active = true;
    return true;
}

static inline void thread_data_free(void)
{
    if (thread_data.copy_thread) {
        DWORD ret;

        SetEvent(thread_data.stop_event);
        ret = WaitForSingleObject(thread_data.copy_thread, 500);
        if (ret != WAIT_OBJECT_0)
            TerminateThread(thread_data.copy_thread, (DWORD)-1);

        CloseHandle(thread_data.copy_thread);
    }
    if (thread_data.stop_event)
        CloseHandle(thread_data.stop_event);
    if (thread_data.copy_event)
        CloseHandle(thread_data.copy_event);
    for (size_t i = 0; i < NUM_BUFFERS; i++)
        DeleteCriticalSection(&thread_data.mutexes[i]);

    DeleteCriticalSection(&thread_data.data_mutex);

    memset(&thread_data, 0, sizeof(thread_data));
}

//////////////////////////////////////////////////copy thread/////////////////////////////////////////////////////////



bool capture_init_shtex(shtex_data_t** data, HWND window, uint32_t cx,
    uint32_t cy, uint32_t format, bool flip,
    uintptr_t handle)
{
    if (!init_shared_info(sizeof(shtex_data_t), window)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "capture_init_shtex: Failed to initialize memory");
        return false;
    }

    *data = (shtex_data_t*)shmem_info;
    (*data)->tex_handle = (uint32_t)handle;

    global_hook_info->hook_ver_major = HOOK_VER_MAJOR;
    global_hook_info->hook_ver_minor = HOOK_VER_MINOR;
    global_hook_info->window = (uint32_t)(uintptr_t)window;
    global_hook_info->type = CAPTURE_TYPE_TEXTURE;
    global_hook_info->format = format;
    global_hook_info->flip = flip;
    global_hook_info->map_id = shmem_id_counter;
    global_hook_info->map_size = sizeof(struct shtex_data_t);
    global_hook_info->cx = cx;
    global_hook_info->cy = cy;
    global_hook_info->UNUSED_base_cx = cx;
    global_hook_info->UNUSED_base_cy = cy;

    std::error_code ec;
    if (!utilpp::SetProcEvent(signal_ready,ec)) {
        SIMPLELOG_LOGGER_ERROR(nullptr, "capture_init_shtex: Failed to signal ready: %d",
            GetLastError());
        return false;
    }

    active = true;
    set_capture_ready_mutex(true);
    return true;
}


void shmem_copy_data(size_t idx, void* volatile data)
{
    EnterCriticalSection(&thread_data.data_mutex);
    thread_data.cur_tex = (int)idx;
    thread_data.cur_data = data;
    thread_data.locked_textures[idx] = true;
    LeaveCriticalSection(&thread_data.data_mutex);

    SetEvent(thread_data.copy_event);
}

bool shmem_texture_data_lock(int idx)
{
    bool locked;

    EnterCriticalSection(&thread_data.data_mutex);
    locked = thread_data.locked_textures[idx];
    LeaveCriticalSection(&thread_data.data_mutex);

    if (locked) {
        EnterCriticalSection(&thread_data.mutexes[idx]);
        return true;
    }

    return false;
}

void shmem_texture_data_unlock(int idx)
{
    EnterCriticalSection(&thread_data.data_mutex);
    thread_data.locked_textures[idx] = false;
    LeaveCriticalSection(&thread_data.data_mutex);

    LeaveCriticalSection(&thread_data.mutexes[idx]);
}



void capture_free(void)
{
    thread_data_free();

    if (shmem_info) {
        UnmapSharedMemory(shmem_handle,shmem_info);
        shmem_info = NULL;
    }
    CloseSharedMemory(shmem_handle);
    shmem_handle = NULL;

    std::error_code ec;
    utilpp::SetProcEvent(signal_restart,ec);
    active = false;
    set_capture_ready_mutex(false);
}

IRPCProcesser* get_rpc_processer()
{
    return PRPCProcesser.get();
}

void on_window_event(uint64_t id, window_event_t& e)
{
    if (!get_rpc_processer()) {
        return;
    }
    auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
    HookHelperEventInterface->OverlayWindowEvent(id, e);
}


