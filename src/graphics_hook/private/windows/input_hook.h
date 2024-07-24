#pragma once

#include <RPC/JrpcHookHelperEvent.h>
#include <string_view>
#include <SimpleValueStorage.h>
typedef struct HotKeyState_t {
    HotKeyList_t* ExpectList;
}hot_key_state_t;

extern HotKeyList_t HotKeyList;
extern HotKeyState_t HotKeyState;
extern SimpleValueHandle_t HotKeyListHandle;

extern SimpleValueHandle_t ImeEventHandle;
extern overlay_ime_event_t ImeState;



bool init_input_info();
void trigger_hotkey(std::string_view name);

void on_mouse_move_event(uint64_t id, mouse_motion_event_t e);
void on_mouse_button_event(uint64_t id, mouse_button_event_t e);
void on_mouse_wheel_event(uint64_t id, mouse_wheel_event_t e);
void on_keyboard_event(uint64_t id, keyboard_event_t e);
