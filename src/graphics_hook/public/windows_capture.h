#pragma once
#include "simple_os_defs.h"
#include <functional>
#include <handle.h>
#define  OVERLAY_MSG_TICK 0x908988
#define  HOTKEYLIST_UPDATE 0x909000
#define  SHARED_WINDOW_INFOS_UPDATE 0x909001
#define  OVERLAY_ENABLE 0x909002
#define  IME_STATE_UPDATE 0x909003
#define  SHARED_WINDOW_TEXTURE_UPDATE 0x909004
typedef std::function<bool( LPARAM lParam)> MsgProcessorFn_t;
typedef struct MsgProcessorHandle_t:CommonHandle_t{
    MsgProcessorHandle_t() :CommonHandle_t() {}
    MsgProcessorHandle_t(NullCommonHandle_t handle) :CommonHandle_t(handle) {}
    MsgProcessorFn_t Fn;
}MsgProcessorHandle_t;


extern HWND main_window;
extern uint32_t overlay_async_msg;

void set_render_window(HWND in_window);
bool hook_Windows();

MsgProcessorHandle_t register_overlay_async_processor(WPARAM wParam,MsgProcessorFn_t MsgLoopProcessor);
void async_overlay(WPARAM wParam, LPARAM lParam);
void tick_async_overlay();

LRESULT OverlayImplWin32WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT ImeImplWin32WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);