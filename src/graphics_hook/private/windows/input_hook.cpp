#include "input_hook.h"
#include "game_hook.h"
#include "windows_capture.h"
#include <LoggerHelper.h>
SimpleValueHandle_t HotKeyListHandle;
HotKeyList_t HotKeyList;
HotKeyState_t HotKeyState{ &HotKeyList };
SimpleValueHandle_t ImeEventHandle;
overlay_ime_event_t ImeState;
bool init_input_info()
{
	HotKeyListHandle = SimpleValueStorage::RegisterValue<HotKeyList_t>();
	SimpleValueStorage::RegisterValueChange(HotKeyListHandle,
		[](SimpleValueHandle_t, const void*, const void*) {
			async_overlay(HOTKEYLIST_UPDATE, NULL);
		}
	);
	register_overlay_async_processor(HOTKEYLIST_UPDATE,
		[](LPARAM lParam)->bool {
			if (!SimpleValueStorage::GetValue(HotKeyListHandle, HotKeyList)) {
				async_overlay(HOTKEYLIST_UPDATE, NULL);
			}
			HotKeyState.ExpectList = &HotKeyList;
			return true;
		}
	);

	ImeEventHandle = SimpleValueStorage::RegisterValue<overlay_ime_event_t>();
	SimpleValueStorage::RegisterValueChange(ImeEventHandle,
		[](SimpleValueHandle_t, const void*, const void*) {
			async_overlay(IME_STATE_UPDATE, NULL);
		}
	);
	register_overlay_async_processor(IME_STATE_UPDATE,
		[](LPARAM lParam)->bool {
			if (!SimpleValueStorage::GetValue(ImeEventHandle, ImeState)) {
				async_overlay(IME_STATE_UPDATE, NULL);
			}
			if (!main_window) {
				return true;
			}
			if (HIMC himc = ::ImmGetContext(main_window))
			{
				COMPOSITIONFORM composition_form = {};
				composition_form.ptCurrentPos.x = (LONG)ImeState.input_pos_x;
				composition_form.ptCurrentPos.y = (LONG)ImeState.input_pos_y;
				composition_form.dwStyle = CFS_FORCE_POSITION;
				::ImmSetCompositionWindow(himc, &composition_form);
				CANDIDATEFORM candidate_form = {};
				candidate_form.dwStyle = CFS_CANDIDATEPOS;
				candidate_form.ptCurrentPos.x = (LONG)ImeState.input_pos_x;
				candidate_form.ptCurrentPos.y = (LONG)ImeState.input_pos_y;
				::ImmSetCandidateWindow(himc, &candidate_form);
				::ImmReleaseContext(main_window, himc);
			}

			return true;
		}
	);
	return true;
}

void trigger_hotkey(std::string_view name)
{
	if (name == OVERLAY_HOT_KEY_NAME) {
		global_hook_info->bOverlayEnabled = !global_hook_info->bOverlayEnabled;
	}
}
void on_mouse_move_event(uint64_t id, mouse_motion_event_t e) {
	if (!get_rpc_processer()) {
		return;
	}
	SIMPLELOG_LOGGER_DEBUG(nullptr, "mouse_motion_event_t: x {} y {} dx {} xy {}", e.x, e.y, e.xrel, e.yrel);
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayMouseMotionEvent(id, e);
}

void on_mouse_button_event(uint64_t id, mouse_button_event_t e) {
	if (!get_rpc_processer()) {
		return;
	}
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayMouseButtonEvent(id, e);
}
void on_mouse_wheel_event(uint64_t id, mouse_wheel_event_t e) {
	if (!get_rpc_processer()) {
		return;
	}
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayMouseWheelEvent(id, e);
}
void on_keyboard_event(uint64_t id, keyboard_event_t e) {
	if (!get_rpc_processer()) {
		return;
	}
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayKeyboardEvent(id, e);
}
void on_char_event(uint64_t id, overlay_char_event_t& e)
{
	if (!get_rpc_processer()) {
		return;
	}
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayCharEvent(id, e);
}
void on_window_event(uint64_t id, window_event_t e) {
	if (!get_rpc_processer()) {
		return;
	}
	auto HookHelperEventInterface = get_rpc_processer()->GetInterface<JRPCHookHelperEventAPI>();
	HookHelperEventInterface->OverlayWindowEvent(id, e);
}

LRESULT ImeImplWin32WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
	switch (msg) {
	case WM_IME_SETCONTEXT: {
		break;
	}

	case WM_IME_COMPOSITION: {
		break;
	}
	case  WM_IME_NOTIFY: {
		break;
	}
	}
	return NULL;
}
