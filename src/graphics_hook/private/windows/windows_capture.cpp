#include "windows_capture.h"
#include "game_hook.h"
#include "input_hook.h"
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
typedef SHORT(WINAPI* GetKeyState_t)(_In_ int vKey);
typedef BOOL(WINAPI* PeekMessageA_t)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
typedef BOOL(WINAPI* PeekMessageW_t)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
//UINT GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
//UINT GetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);


typedef struct windows_data_t {
    GetAsyncKeyState_t RealGetAsyncKeyState{ NULL };
    GetKeyState_t RealGetKeyState{ NULL };
    GetKeyboardState_t RealGetKeyboardState{ NULL };
    ShowCursor_t RealShowCursor{ NULL };
    GetCursorPos_t RealGetCursorPos{ NULL };
    SetCursorPos_t RealSetCursorPos{ NULL };
    GetCursor_t RealGetCursor{ NULL };
    SetCursor_t RealSetCursor{ NULL };
    PeekMessageA_t RealPeekMessageA{ NULL };
    PeekMessageW_t RealPeekMessageW{ NULL };
    HHOOK hhk{ NULL };
    WNDPROC OriginWndProc{ NULL };
}windows_data_t;

HWND main_window{NULL};
uint32_t overlay_async_msg;
bool bInGame{ true };
static windows_data_t windows_data = {};
static std::unordered_map<WPARAM,MsgProcessorHandle_t> MsgProcessorList;
static LRESULT CALLBACK hook_callback(int code, WPARAM wParam, LPARAM lParam)
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
            if (!WindowsCheckModifier(node.HotKey.mod)) {
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
    if (is_overlay_active()) {
        OverlayImplWin32WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        ImeImplWin32WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) {
            msg.message = WM_NULL;
        }
        else if ((msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) || (msg.message >= WM_SYSKEYDOWN && msg.message <= WM_SYSDEADCHAR)) {
            msg.message = WM_NULL;
        }
    }
end_hook:
    // call the next hook in the hook chain. This is nessecary or your hook chain will break and the hook stops
    return CallNextHookEx(NULL, code, wParam, lParam);
}

static inline LRESULT CallDefWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    auto res= DefWindowProcA(hWnd, Msg, wParam, lParam);
    bInGame = true;
    return res;
}
static LRESULT CALLBACK HookWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    //SIMPLELOG_LOGGER_TRACE(ENGINE_LOG_NAME, "Hook receive hwnd {},msg {},l {},w {}", (intptr_t)msg.hwnd, Msg, lParam, wParam);
    bInGame = false;
    if (Msg == overlay_async_msg) {
        auto res = MsgProcessorList.find(wParam);
        if (res != MsgProcessorList.end()) {
            res->second.Fn(lParam);
        }
        Msg = WM_NULL;
        goto end_hook;
    }

    if (!is_pipe_active()) {
        goto end_hook;
    }

    switch (Msg) {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        SDL_Scancode scanCode = WindowsScanCodeToSDLScanCode(lParam, wParam);
        SDL_Keycode keyCode = SDL_GetKeyFromScancode(scanCode);
        auto oldExpectList = HotKeyState.ExpectList;
        HotKeyList_t& list = *HotKeyState.ExpectList;
        for (auto node : list) {
            if (node.HotKey.key_code != keyCode) {
                continue;
            }
            if (!WindowsCheckModifier(node.HotKey.mod)) {
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
    if (is_overlay_active()) {
        OverlayImplWin32WndProcHandler(hWnd, Msg, wParam, lParam);
        ImeImplWin32WndProcHandler(hWnd, Msg, wParam, lParam);
        if (Msg >= WM_MOUSEFIRST && Msg <= WM_MOUSELAST) {
            return CallDefWindowProc(hWnd, Msg, wParam, lParam);
        }
        else if ((Msg >= WM_KEYFIRST && Msg <= WM_KEYLAST) || (Msg >= WM_SYSKEYDOWN && Msg <= WM_SYSDEADCHAR)) {
            return CallDefWindowProc(hWnd, Msg, wParam, lParam);
        }
    }
end_hook:
    bInGame = true;
    return CallWindowProcA(windows_data.OriginWndProc, hWnd, Msg, wParam, lParam);
    
}

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

    //windows_data.hhk = SetWindowsHookExA(WH_GETMESSAGE, (HOOKPROC)hook_callback, NULL, thread_id);
    windows_data.OriginWndProc = (WNDPROC)GetWindowLongPtrA(main_window, GWLP_WNDPROC);
    SetWindowLongPtrA(main_window, GWLP_WNDPROC, (LONG_PTR)HookWindowProc);
    return true;
}

static SHORT HookGetAsyncKeyState(_In_ int vKey) {
    if (is_overlay_active()&& bInGame) {
        return 0;
    }
    else {
        return windows_data.RealGetAsyncKeyState(vKey);
    }
}
static SHORT HookGetKeyState(_In_ int vKey) {
    if (is_overlay_active() && bInGame) {
        return 0;
    }
    else {
        return windows_data.RealGetKeyState(vKey);
    }
}
static BOOL HookGetKeyboardState(__out_ecount(256) PBYTE lpKeyState) {
    if (is_overlay_active() && bInGame) {
        memset(lpKeyState, 0, 256);
        return TRUE;
    }
    else {
        return windows_data.RealGetKeyboardState(lpKeyState);
    }
}
static INT HookShowCursor(__in BOOL bShow) {
    if (is_overlay_active() && bInGame) {
        return 0;
    }
    else {
        return windows_data.RealShowCursor(bShow);
    }
}
static BOOL HookGetCursorPos(LPPOINT lpPoint) {
    if (is_overlay_active() && bInGame) {
        return FALSE;
    }
    else {
        return windows_data.RealGetCursorPos(lpPoint);
    }
}
static BOOL HookSetCursorPos(int X, int Y) {
    if (is_overlay_active() && bInGame) {
        return FALSE;
    }
    else {
        return windows_data.RealSetCursorPos(X,Y);
    }
}
static HCURSOR HookGetCursor() {
    if (is_overlay_active() && bInGame) {
        return 0;
    }
    else {
        return windows_data.RealGetCursor();
    }
}
static HCURSOR HookSetCursor(HCURSOR cursor) {
    if (is_overlay_active() && bInGame) {
        return 0;
    }
    else {
        return windows_data.RealSetCursor(cursor);
    }
}

static BOOL WINAPI HookPeekMessageA(
    _Out_ LPMSG lpMsg,
    _In_opt_ HWND hWnd,
    _In_ UINT wMsgFilterMin,
    _In_ UINT wMsgFilterMax,
    _In_ UINT wRemoveMsg) {
    bInGame = false;
    auto res = windows_data.RealPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    bInGame = true;
    return res;
}
static BOOL WINAPI HookPeekMessageW(
    _Out_ LPMSG lpMsg,
    _In_opt_ HWND hWnd,
    _In_ UINT wMsgFilterMin,
    _In_ UINT wMsgFilterMax,
    _In_ UINT wRemoveMsg) {
    bInGame = false;
    auto res=windows_data.RealPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    bInGame = true;
    return res;
}
static bool hook_Windows_input() {
    if (windows_data.RealGetAsyncKeyState) {
        return true;
    }
    windows_data.RealGetAsyncKeyState = GetAsyncKeyState;
    windows_data.RealGetKeyState = GetKeyState;
    windows_data.RealGetKeyboardState = GetKeyboardState;
    windows_data.RealShowCursor = ShowCursor;
    windows_data.RealGetCursorPos = GetCursorPos;
    windows_data.RealSetCursorPos = SetCursorPos;
    windows_data.RealGetCursor = GetCursor;
    windows_data.RealSetCursor = SetCursor;
    windows_data.RealPeekMessageA = PeekMessageA;
    windows_data.RealPeekMessageW = PeekMessageW;
    if (DetourTransactionBegin() != NO_ERROR)
        return false;
    if (DetourAttach((PVOID*)&windows_data.RealGetAsyncKeyState, HookGetAsyncKeyState) != NO_ERROR){
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealGetKeyState, HookGetKeyState) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealGetKeyboardState, HookGetKeyboardState) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealShowCursor, HookShowCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealGetCursorPos, HookGetCursorPos) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealSetCursorPos, HookSetCursorPos) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealGetCursor, HookGetCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealSetCursor, HookSetCursor) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealPeekMessageA, HookPeekMessageA) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourAttach((PVOID*)&windows_data.RealPeekMessageW, HookPeekMessageW) != NO_ERROR) {
        goto DetourAbort;
    }
    if (DetourTransactionCommit() != NO_ERROR) {
        goto DetourClean;
    }
    return true;
DetourAbort:
    DetourTransactionAbort();
    
DetourClean:
    windows_data.RealGetAsyncKeyState = nullptr;
    windows_data.RealGetKeyState = nullptr;
    windows_data.RealGetKeyboardState = nullptr;
    windows_data.RealShowCursor = nullptr;
    windows_data.RealGetCursorPos = nullptr;
    windows_data.RealSetCursorPos = nullptr;
    windows_data.RealGetCursor = nullptr;
    windows_data.RealSetCursor = nullptr;
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
    if (!windows_data.OriginWndProc&&!windows_data.hhk) {
        return;
    }
    while (msgQueue.size() > 0) {
        PostMessageA(main_window, overlay_async_msg, msgQueue.front().wParam, msgQueue.front().lParam);
        msgQueue.pop();
    }

}
