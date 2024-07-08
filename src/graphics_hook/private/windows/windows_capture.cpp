#include "windows_capture.h"
#include "game_hook.h"
#include "overlay_ui.h"
#include <windows_helper.h>
#include <LoggerHelper.h>
#include <Windows.h>
#include <SDL_keycode.h>
#include <SDL2/SDL.h>
#include <windows/sdl_helper_win.h>
#include <detours.h>
#include <queue>

typedef SHORT (WINAPI *GetAsyncKeyState_t)(_In_ int vKey);

typedef BOOL(WINAPI* GetKeyboardState_t)(__out_ecount(256) PBYTE lpKeyState);
typedef INT(WINAPI* ShowCursor_t)(__in BOOL bShow);
typedef BOOL(WINAPI* GetCursorPos_t)(LPPOINT lpPoint);
typedef BOOL(WINAPI* SetCursorPos_t)(int X, int Y);
typedef HCURSOR(WINAPI* GetCursor_t)();
typedef HCURSOR(WINAPI* SetCursor_t)(HCURSOR cursor);
//UINT GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
//UINT GetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);


typedef struct input_data_t {
    GetAsyncKeyState_t RealGetAsyncKeyState{ NULL };
    GetKeyState_t RealGetKeyState{ NULL };
    GetKeyboardState_t RealGetKeyboardState{ NULL };
    ShowCursor_t RealShowCursor{ NULL };
    GetCursorPos_t RealGetCursorPos{ NULL };
    SetCursorPos_t RealSetCursorPos{ NULL };
    GetCursor_t RealGetCursor{ NULL };
    SetCursor_t RealSetCursor{ NULL };
    HHOOK hhk{ NULL };
}input_data_t;

HWND main_window{NULL};
uint32_t overlay_async_msg;

static input_data_t input_data = {};
static std::unordered_map<WPARAM,MsgProcessorHandle_t> MsgProcessorList;
LRESULT CALLBACK hook_callback(int code, WPARAM wParam, LPARAM lParam)
{
    MSG& msg = *(PMSG)lParam;
    if (code < 0){
        goto end_hook;
    }
    
    //SIMPLELOG_LOGGER_TRACE(ENGINE_LOG_NAME, "Hook receive hwnd {},msg {},l {},w {}", (intptr_t)msg.hwnd, msg.message, msg.lParam, msg.wParam);

    if (msg.message == overlay_async_msg) {
        auto res=MsgProcessorList.find(msg.wParam);
        if (res != MsgProcessorList.end()) {
            res->second.Fn(msg.lParam);
        }
        msg.message = WM_NULL;
        goto end_hook;
    }

    if (!is_pipe_active()) {
        goto end_hook;
    }

    switch (msg.message) {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        SDL_Scancode scanCode = WindowsScanCodeToSDLScanCode(msg.lParam, msg.wParam);
        SDL_Keycode keyCode = SDL_GetKeyFromScancode(scanCode);
        auto oldExpectList = HotKeyState.ExpectList;
        HotKeyList_t& list = *HotKeyState.ExpectList;
        for (auto node : list) {
            if (node.HotKey.key_code != keyCode) {
                continue;
            }
            if (!WindowsCheckModifier(node.HotKey.mod,input_data.RealGetKeyState)) {
                break;
            }
            if (std::holds_alternative<std::string>(node.Child)) {
                trigger_hotkey(std::get<std::string>(node.Child));
            }
            else {
                HotKeyState.ExpectList = &std::get<HotKeyList_t>(node.Child);
            }

            break;
        }
        if (oldExpectList == HotKeyState.ExpectList) {
            HotKeyState.ExpectList = &HotKeyList;
        }
        break;
    }
    OverlayImplWin32WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam);

    if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) {
        msg.message = WM_NULL;
    }
    if ((msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) || (msg.message >= WM_SYSKEYDOWN && msg.message <= WM_SYSDEADCHAR)) {
        msg.message = WM_NULL;
    }
    
end_hook:
    // call the next hook in the hook chain. This is nessecary or your hook chain will break and the hook stops
    return CallNextHookEx(NULL, code, wParam, lParam);
}

//LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam)
//{
//    if (code == HC_ACTION && ((DWORD)lParam & 0x80000000) == 0)	// if there is an incoming action and a key was pressed
//    {
//        switch (wParam)
//        {
//        case MOUSEEVENTF_MOVE:
//            SIMPLELOG_LOGGER_TRACE(nullptr, "MOUSEEVENTF_MOVE");
//            break;
//        default:
//            break;
//        }
//    }
//    return CallNextHookEx(mhhk, code, wParam, lParam);
//}
static bool hook_Windows_msg()
{
    if (!main_window) {
        return false;
    }
    DWORD thread_id;
    auto processid = GetCurrentProcessId();

    overlay_async_msg = RegisterWindowMessageA("windows_capture_0x010101");
    if (overlay_async_msg == 0)
    {
        return false;
    }
    thread_id = ::GetWindowThreadProcessId(main_window, NULL);

    input_data.hhk = SetWindowsHookExA(WH_GETMESSAGE, (HOOKPROC)hook_callback, NULL, thread_id);

    return true;
}

static SHORT HookGetAsyncKeyState(_In_ int vKey) {
    if (global_hook_info->bOverlayEnabled) {
        return 0;
    }
    else {
        return input_data.RealGetAsyncKeyState(vKey);
    }
}
static SHORT HookGetKeyState(_In_ int vKey) {
    if (global_hook_info->bOverlayEnabled) {
        return 0;
    }
    else {
        return input_data.RealGetKeyState(vKey);
    }
}
static BOOL HookGetKeyboardState(__out_ecount(256) PBYTE lpKeyState) {
    if (global_hook_info->bOverlayEnabled) {
        memset(lpKeyState, 0, 256);
        return TRUE;
    }
    else {
        return input_data.RealGetKeyboardState(lpKeyState);
    }
}
static INT HookShowCursor(__in BOOL bShow) {
    if (global_hook_info->bOverlayEnabled) {
        return 0;
    }
    else {
        return input_data.RealShowCursor(bShow);
    }
}
static BOOL HookGetCursorPos(LPPOINT lpPoint) {
    if (global_hook_info->bOverlayEnabled) {
        return FALSE;
    }
    else {
        return input_data.RealGetCursorPos(lpPoint);
    }
}
static BOOL HookSetCursorPos(int X, int Y) {
    if (global_hook_info->bOverlayEnabled) {
        return FALSE;
    }
    else {
        return input_data.RealSetCursorPos(X,Y);
    }
}
static HCURSOR HookGetCursor() {
    if (global_hook_info->bOverlayEnabled) {
        return 0;
    }
    else {
        return input_data.RealGetCursor();
    }
}
static HCURSOR HookSetCursor(HCURSOR cursor) {
    if (global_hook_info->bOverlayEnabled) {
        return 0;
    }
    else {
        return input_data.RealSetCursor(cursor);
    }
}

static bool hook_Windows_input() {
    if (input_data.RealGetAsyncKeyState) {
        return true;
    }
    input_data.RealGetAsyncKeyState = GetAsyncKeyState;
    input_data.RealGetKeyState = GetKeyState;
    input_data.RealGetKeyboardState = GetKeyboardState;
    input_data.RealShowCursor = ShowCursor;
    input_data.RealGetCursorPos = GetCursorPos;
    input_data.RealSetCursorPos = SetCursorPos;
    input_data.RealGetCursor = GetCursor;
    input_data.RealSetCursor = SetCursor;

    if (DetourTransactionBegin() != NO_ERROR)
        return false;
    if (DetourAttach((PVOID*)&input_data.RealGetAsyncKeyState, HookGetAsyncKeyState) != NO_ERROR){
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealGetKeyState, HookGetKeyState) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealGetKeyboardState, HookGetKeyboardState) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealShowCursor, HookShowCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealGetCursorPos, HookGetCursorPos) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealSetCursorPos, HookSetCursorPos) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealGetCursor, HookGetCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&input_data.RealSetCursor, HookSetCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourTransactionCommit() != NO_ERROR) {
        goto DetourClean;
    }
    return true;
DetourAbort:
    DetourTransactionAbort();
    
DetourClean:
    input_data.RealGetAsyncKeyState = nullptr;
    input_data.RealGetKeyState = nullptr;
    input_data.RealGetKeyboardState = nullptr;
    input_data.RealShowCursor = nullptr;
    input_data.RealGetCursorPos = nullptr;
    input_data.RealSetCursorPos = nullptr;
    input_data.RealGetCursor = nullptr;
    input_data.RealSetCursor = nullptr;
    return false;
}
void set_render_window(HWND in_window)
{
    main_window = in_window;
}

bool hook_Windows()
{
    if (!hook_Windows_input()) {
        return false;
    }
    return hook_Windows_msg();
}


MsgProcessorHandle_t register_overlay_async_processor(WPARAM wParam, MsgProcessorFn_t MsgLoopProcessor) {
    MsgProcessorHandle_t handle;
    handle.Fn= MsgLoopProcessor;
    auto res = MsgProcessorList.emplace(wParam, handle);
    if (!res.second) {
        return NullHandle;
    }
    return res.first->second;
}

typedef struct async_msg_t {
    WPARAM wParam;
    LPARAM lParam;
}async_msg_t;
static std::queue<async_msg_t> msgQueue;
void async_overlay(WPARAM wParam, LPARAM lParam) {
    msgQueue.emplace(async_msg_t{ wParam, lParam });
}
void tick_async_overlay()
{
    if (!input_data.hhk) {
        return;
    }
    while (msgQueue.size() > 0) {
        PostMessageA(main_window, overlay_async_msg, msgQueue.front().wParam, msgQueue.front().lParam);
        msgQueue.pop();
    }

}