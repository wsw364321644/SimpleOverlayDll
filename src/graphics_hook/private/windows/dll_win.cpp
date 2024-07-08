#include "graphics_hook.h"
#include "windows_capture.h"
#include "game_hook.h"
#include <LoggerHelper.h>
#include <TimeRecorder.h>
#include <sm_util.h>
#include <HOOK/graphics_info.h>
#include <HOOK/hook_synchronized.h>
#include <mutex>
#include <format>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "SDL.h"

static HANDLE capture_thread = NULL;
static HANDLE dup_hook_mutex{ NULL };
static std::atomic_bool stop_loop{ false };

static bool init_dll(void) {
    DWORD pid = GetCurrentProcessId();
    HANDLE h;
    h = open_mutex_plus_id("graphics_hook", pid,false);
    if (h) {
        CloseHandle(h);
        return false;
    }
    dup_hook_mutex = create_mutex_plus_id("graphics_hook", pid,false);
    if (!dup_hook_mutex) {
        return false;
    }
    return true;
}



static inline bool init_hook(HANDLE thread_handle)
{
    if (thread_handle) {
        WaitForSingleObject(thread_handle, 100);
        CloseHandle(thread_handle);
    }
    init_dummy_window_thread();
    SetEvent(signal_restart);
    return true;
}
static void free_hook(void)
{
    free_hook_info();
    free_mutexes();
    free_signals();
    if (dup_hook_mutex) {
        CloseHandle(dup_hook_mutex);
        dup_hook_mutex = NULL;
    }
}

static inline bool attempt_hook(void)
{
    //static bool ddraw_hooked = false;
    static bool windows_hooked = false;
    static bool d3d8_hooked = false;
    static bool d3d9_hooked = false;
    static bool d3d12_hooked = false;
    static bool dxgi_hooked = false;
    static bool gl_hooked = false;
    static bool vulkan_hooked = false;

    if (!windows_hooked) {
        windows_hooked = hook_Windows();
    }


#ifdef COMPILE_VULKAN_HOOK

    if (!vulkan_hooked) {
        vulkan_hooked = hook_vulkan();
        if (vulkan_hooked) {
            return true;
        }
    }
#endif //COMPILE_VULKAN_HOOK

#ifdef COMPILE_D3D12_HOOK
    if (!d3d12_hooked) {
        d3d12_hooked = hook_d3d12();
    }
#endif

    if (!d3d9_hooked) {
        if (!d3d9_hookable()) {
            SIMPLELOG_LOGGER_WARN(nullptr, "no D3D9 hook address found!");
            d3d9_hooked = true;
        }
        else {
            d3d9_hooked = hook_d3d9();
            if (d3d9_hooked) {
                return true;
            }
        }
    }

    if (!dxgi_hooked) {
    	if (!dxgi_hookable()) {
            SIMPLELOG_LOGGER_WARN(nullptr, " no DXGI hook address found!\n");
    		dxgi_hooked = true;
    	}
    	else {
    		dxgi_hooked = hook_dxgi();
    		if (dxgi_hooked) {
    			return true;
    		}
    	}
    }

    //if (!gl_hooked) {
    //	gl_hooked = hook_gl();
    //	if (gl_hooked) {
    //		return true;
    //	}
    //	/*} else {
    //	rehook_gl();*/
    //}

    //if (!d3d8_hooked) {
    //	if (!d3d8_hookable()) {
    //		d3d8_hooked = true;
    //	}
    //	else {
    //		d3d8_hooked = hook_d3d8();
    //		if (d3d8_hooked) {
    //			return true;
    //		}
    //	}
    //}

    /*if (!ddraw_hooked) {
        if (!ddraw_hookable()) {
            ddraw_hooked = true;
        } else {
            ddraw_hooked = hook_ddraw();
            if (ddraw_hooked) {
                return true;
            }
        }
    }*/
    SIMPLELOG_LOGGER_INFO(nullptr, "D3D8={}", d3d8_hooked);
    SIMPLELOG_LOGGER_INFO(nullptr, "D3D9={}", d3d9_hooked);
    SIMPLELOG_LOGGER_INFO(nullptr, "D3D12={}", d3d12_hooked);
    SIMPLELOG_LOGGER_INFO(nullptr, "DXGI={}", dxgi_hooked);
    SIMPLELOG_LOGGER_INFO(nullptr, "GL={}", gl_hooked);
    SIMPLELOG_LOGGER_INFO(nullptr, "VK={}", vulkan_hooked);

    return false;
}

static inline void capture_loop(void)
{
    static FTimeRecorder  TimeRecorder;
    static FTimeRecorder  HookTimeRecorder;
    static uint32_t  TickMilliseconds{40};
    static uint32_t  HookMilliseconds{4000};

    TimeRecorder.Reset();
    HookTimeRecorder.Reset();
    for (size_t n = 0; !stop_loop; n++) {
        hook_thread_tick();
        tick_async_overlay();
        TimeRecorder.Tick();
        auto Delta=TimeRecorder.GetDelta<std::chrono::milliseconds>();
        std::this_thread::sleep_for(std::chrono::milliseconds(TickMilliseconds) - Delta);
        TimeRecorder.Reset();

        if (HookTimeRecorder.GetDeltaToNow<std::chrono::milliseconds>().count() > HookMilliseconds) {
            attempt_hook();
            HookTimeRecorder.Reset();
        }
    }

}
static DWORD WINAPI main_capture_thread(HANDLE thread_handle)
{
    if (!init_hook(thread_handle)) {
        free_hook();
        return 0;
    }
    WaitForSingleObject(signal_init, INFINITE);
    capture_loop();
    return 0;

}
BOOL WINAPI DllMain(_In_ HINSTANCE hinstDLL, _In_ DWORD fdwReason, _In_ LPVOID lpvReserved)
{
    //printf("hModule.%p lpReserved.%p \n", hinstDLL, lpvReserved);

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH: {
        auto logger=CreateAsyncLogger(LoggerSetting_t{ "",{std::make_shared<RotatingLoggerInfo_t>("graphics_hook.log"),std::make_shared<MSVCLoggerInfo_t>(),std::make_shared<StdoutLoggerInfo_t>()} });
#ifdef NDEBUG
        logger->set_level(spdlog::level::warn);
#else
        logger->set_level(spdlog::level::debug);
#endif
        dll_inst = hinstDLL;
        wchar_t name[MAX_PATH];


        HANDLE cur_thread;
        if (!init_dll()) {
            return false;
        }
        bool success = DuplicateHandle(GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(), &cur_thread,
            SYNCHRONIZE, false, 0);

        if (!success)
            return false;

        if (!init_pipe()) {
            return false;
        }

        if (!init_signals()) {
        	return false;
        }

        if (!init_hook_info()) {
            return false;
        }

        if (!init_mutexes()) {
        	return false;
        }
        if ((SDL_Init(SDL_INIT_VIDEO) < 0)) {
            return false;
        }
        /* this prevents the library from being automatically unloaded
         * by the next FreeLibrary call */
        GetModuleFileNameW(dll_inst, name, MAX_PATH);
        LoadLibraryW(name);

        capture_thread = CreateThread(
            NULL, 0, (LPTHREAD_START_ROUTINE)main_capture_thread,
            (LPVOID)cur_thread, 0, 0);
        if (!capture_thread) {
            CloseHandle(cur_thread);
            return false;
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (!dup_hook_mutex) {
            return true;
        }

        if (capture_thread) {
            stop_loop = true;
            WaitForSingleObject(capture_thread, 300);
            CloseHandle(capture_thread);
        }

        free_hook();
        break;

    case DLL_THREAD_ATTACH:
        //printf("Thread attach. \n");
        break;

    case DLL_THREAD_DETACH:
        //printf("Thread detach. \n");
        break;
    }


    return (TRUE);
}
