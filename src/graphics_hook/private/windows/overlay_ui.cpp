#include "overlay_ui.h"

#include "simple_os_defs.h"
#include "game_hook.h"
#include "input_hook.h"
#include <imgui_impl_win32.h>
#include "windows_capture.h"

ImGuiContext* pImGuiCtx;
bool show_demo_window{true};
bool init_overlay_ui(HWND window) {
    auto pImGuiCtx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

#ifdef WIN32
    if (!ImGui_ImplWin32_Init(window)) {
        return false;
    }
#endif
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // Allow user UI scaling using CTRL+Mouse Wheel scrolling
    io.FontAllowUserScaling = true;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 0.0f;
    style.WindowPadding = ImVec2(0.0f, 0.0f);

    return true;
}

void get_font_tex_data_RGBA32(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->GetTexDataAsRGBA32(out_pixels, out_width, out_height, out_bytes_per_pixel);
}

void set_font_tex(ImTextureID tex_id)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->SetTexID(tex_id);
}

void destroy_overlay_ui()
{
    ImGui_ImplWin32_Shutdown();
}

void overlay_ui_new_frame()
{
    static ImVec2 lastMousePos{ -FLT_MAX, -FLT_MAX };
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow(&show_demo_window);
    
    for (auto& pwinInfo : SharedWindowInfos) {
        auto windowType = pwinInfo->Info->hook_window_type;
        
        std::string windowName("window");
        windowName += std::to_string( pwinInfo->Id);
        std::string buttonName = windowName + "button";
        ImGuiWindowFlags flags{ 0 };
        flags |= ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoCollapse| ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        if (windowType == EHookWindowType::Background) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            flags |= ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |ImGuiWindowFlags_NoBringToFrontOnFocus;
        }
        else if (windowType== EHookWindowType::Window) {
            ImVec2 maxSize{ float(pwinInfo->Info->max_width) ,float(pwinInfo->Info->max_height) };
            ImVec2 minSize{ float(pwinInfo->Info->min_width) ,float(pwinInfo->Info->min_height) };
            ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
            ImVec2 pos{ float(pwinInfo->Info->x) ,float(pwinInfo->Info->y) };
            ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver| ImGuiCond_Once);
            ImVec2 size{ float(pwinInfo->Info->render_width) ,float(pwinInfo->Info->render_height) };
            ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver | ImGuiCond_Once);
        }
        ImGui::Begin(windowName.c_str(),NULL, flags);
        
        ImVec2 winSize = ImGui::GetWindowSize();
        if (pwinInfo->Info->width != winSize.x || pwinInfo->Info->height != winSize.y) {
            pwinInfo->Info->width = winSize.x;
            pwinInfo->Info->height = winSize.y;
            window_event_t winEvent;
            winEvent.event = SDL_WindowEventID::SDL_WINDOWEVENT_RESIZED;
            winEvent.data.win_size.height = pwinInfo->Info->height;
            winEvent.data.win_size.width = pwinInfo->Info->width;
            on_window_event(pwinInfo->Id, winEvent);
        }
        ImVec2 winPos = ImGui::GetWindowPos();
        if (pwinInfo->Info->x != winPos.x || pwinInfo->Info->y != winPos.y) {
            pwinInfo->Info->x = winPos.x;
            pwinInfo->Info->y = winPos.y;
            window_event_t winEvent;
            winEvent.event = SDL_WindowEventID::SDL_WINDOWEVENT_MOVED;
            winEvent.data.win_move.x = pwinInfo->Info->x;
            winEvent.data.win_move.y = pwinInfo->Info->y;
            on_window_event(pwinInfo->Id, winEvent);
        }

        

        ImVec2 WindowScreenPos = ImGui::GetCursorScreenPos();
        int32_t mouseXInWindow = io.MousePos.x - WindowScreenPos.x;
        int32_t mouseYInWindow = io.MousePos.y - WindowScreenPos.y;

        ImGui::InvisibleButton(buttonName.c_str(), ImVec2{(float)pwinInfo->Info->render_width, (float)pwinInfo->Info->render_height});
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();


        ImGui::GetWindowDrawList()->AddImage((ImTextureID)pwinInfo->WindowTextureID,
            p0, p1);


        if (ImGui::IsItemFocused()) {
            if (!pwinInfo->bPreFocused) {
                pwinInfo->bPreFocused = true;
                window_event_t winEvent;
                winEvent.event = SDL_WindowEventID::SDL_WINDOWEVENT_ENTER;
                winEvent.data.mouse_motion.x = mouseXInWindow;
                winEvent.data.mouse_motion.y = mouseYInWindow;
                on_window_event(pwinInfo->Id, winEvent);
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Up,EMouseButtonType::Left,1,
                    mouseXInWindow,mouseYInWindow });
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Up,EMouseButtonType::Right,1,
                    mouseXInWindow,mouseYInWindow });
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Up,EMouseButtonType::Middle,1,
                    mouseXInWindow,mouseYInWindow });
            }
            if (io.MouseWheel!=0|| io.MouseWheelH != 0) {
                on_mouse_wheel_event(pwinInfo->Id, mouse_wheel_event_t{ mouseXInWindow,mouseYInWindow,
                     io.MouseWheelH,io.MouseWheel});
            }
            if (lastMousePos.x != -FLT_MAX && lastMousePos.y != -FLT_MAX || io.MousePos.x != -FLT_MAX && io.MousePos.y != -FLT_MAX) {
                if (memcmp(&io.MousePos, &lastMousePos,sizeof(ImVec2))!=0) {
                    on_mouse_move_event(pwinInfo->Id, mouse_motion_event_t{ mouseXInWindow,mouseYInWindow,
                   (int32_t)(io.MousePos.x- lastMousePos.x), (int32_t)(io.MousePos.y - lastMousePos.y) });
                }
            }



            for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_COUNT; key = (ImGuiKey)(key + 1)) {
                if (ImGui::IsKeyPressed(key, false)) {
                    if (imgui_key_to_sdl_scancode(key) == SDL_Scancode::SDL_SCANCODE_UNKNOWN) { continue; }
                    SDL_Keycode keyCode = SDL_GetKeyFromScancode(imgui_key_to_sdl_scancode(key));
                    on_keyboard_event(pwinInfo->Id, keyboard_event_t{ EPressedState::Down,keyCode });
                }
                else if (ImGui::IsKeyReleased(key)) {
                    if (imgui_key_to_sdl_scancode(key) == SDL_Scancode::SDL_SCANCODE_UNKNOWN) { continue; }
                    SDL_Keycode keyCode = SDL_GetKeyFromScancode(imgui_key_to_sdl_scancode(key));
                    on_keyboard_event(pwinInfo->Id, keyboard_event_t{ EPressedState::Up,keyCode });
                }
            }
        }


        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Down,EMouseButtonType::Left,1,
                mouseXInWindow,mouseYInWindow });
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Down,EMouseButtonType::Right,1,
                mouseXInWindow,mouseYInWindow });
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
            on_mouse_button_event(pwinInfo->Id, mouse_button_event_t{ EPressedState::Down,EMouseButtonType::Middle,1,
                mouseXInWindow,mouseYInWindow });
        }



        if (!ImGui::IsItemFocused()) {
            if (pwinInfo->bPreFocused) {
                pwinInfo->bPreFocused = false;
                window_event_t winEvent;
                winEvent.event = SDL_WindowEventID::SDL_WINDOWEVENT_LEAVE;
                on_window_event(pwinInfo->Id, winEvent);
            }
        }
        ImGui::End();
    }

    ImGui::EndFrame();

    lastMousePos = io.MousePos;
}

ImDrawData* overlay_ui_render()
{
    ImGui::Render();
    return ImGui::GetDrawData();
}

SDL_Scancode imgui_key_to_sdl_scancode(ImGuiKey key)
{
    switch (key)
    {
    case ImGuiKey_Tab:return SDL_Scancode::SDL_SCANCODE_TAB;
    case ImGuiKey_LeftArrow:return SDL_Scancode::SDL_SCANCODE_LEFT;
    case ImGuiKey_RightArrow:return SDL_Scancode::SDL_SCANCODE_RIGHT;
    case ImGuiKey_UpArrow:return SDL_Scancode::SDL_SCANCODE_UP;
    case ImGuiKey_DownArrow:return SDL_Scancode::SDL_SCANCODE_DOWN;
    case ImGuiKey_PageUp:return SDL_Scancode::SDL_SCANCODE_PAGEUP;
    case ImGuiKey_PageDown:return SDL_Scancode::SDL_SCANCODE_PAGEDOWN;
    case ImGuiKey_Home:return SDL_Scancode::SDL_SCANCODE_HOME;
    case ImGuiKey_End:return SDL_Scancode::SDL_SCANCODE_END;
    case ImGuiKey_Insert:return SDL_Scancode::SDL_SCANCODE_INSERT;
    case ImGuiKey_Delete:return SDL_Scancode::SDL_SCANCODE_DELETE;
    case ImGuiKey_Backspace:return SDL_Scancode::SDL_SCANCODE_BACKSPACE;
    case ImGuiKey_Space:return SDL_Scancode::SDL_SCANCODE_SPACE;
    case ImGuiKey_Enter:return SDL_Scancode::SDL_SCANCODE_RETURN;
    case ImGuiKey_Escape:return SDL_Scancode::SDL_SCANCODE_ESCAPE;
    case ImGuiKey_LeftCtrl:return SDL_Scancode::SDL_SCANCODE_LCTRL;
    case ImGuiKey_LeftShift:return SDL_Scancode::SDL_SCANCODE_LSHIFT;
    case ImGuiKey_LeftAlt:return SDL_Scancode::SDL_SCANCODE_LALT;
    case ImGuiKey_LeftSuper:return SDL_Scancode::SDL_SCANCODE_LGUI;
    case ImGuiKey_RightCtrl:return SDL_Scancode::SDL_SCANCODE_RCTRL;
    case ImGuiKey_RightShift:return SDL_Scancode::SDL_SCANCODE_RSHIFT;
    case ImGuiKey_RightAlt:return SDL_Scancode::SDL_SCANCODE_RALT;
    case ImGuiKey_RightSuper:return SDL_Scancode::SDL_SCANCODE_RGUI;
    case ImGuiKey_Menu:return SDL_Scancode::SDL_SCANCODE_MENU;
    case ImGuiKey_0:return SDL_Scancode::SDL_SCANCODE_0;
    case ImGuiKey_1:return SDL_Scancode::SDL_SCANCODE_1;
    case ImGuiKey_2:return SDL_Scancode::SDL_SCANCODE_2;
    case ImGuiKey_3:return SDL_Scancode::SDL_SCANCODE_3;
    case ImGuiKey_4:return SDL_Scancode::SDL_SCANCODE_4;
    case ImGuiKey_5:return SDL_Scancode::SDL_SCANCODE_5;
    case ImGuiKey_6:return SDL_Scancode::SDL_SCANCODE_6;
    case ImGuiKey_7:return SDL_Scancode::SDL_SCANCODE_7;
    case ImGuiKey_8:return SDL_Scancode::SDL_SCANCODE_8;
    case ImGuiKey_9:return SDL_Scancode::SDL_SCANCODE_9;
    case ImGuiKey_A:return SDL_Scancode::SDL_SCANCODE_A;
    case ImGuiKey_B:return SDL_Scancode::SDL_SCANCODE_B;
    case ImGuiKey_C:return SDL_Scancode::SDL_SCANCODE_C;
    case ImGuiKey_D:return SDL_Scancode::SDL_SCANCODE_D;
    case ImGuiKey_E:return SDL_Scancode::SDL_SCANCODE_E;
    case ImGuiKey_F:return SDL_Scancode::SDL_SCANCODE_F;
    case ImGuiKey_G:return SDL_Scancode::SDL_SCANCODE_G;
    case ImGuiKey_H:return SDL_Scancode::SDL_SCANCODE_H;
    case ImGuiKey_I:return SDL_Scancode::SDL_SCANCODE_I;
    case ImGuiKey_J:return SDL_Scancode::SDL_SCANCODE_J;
    case ImGuiKey_K:return SDL_Scancode::SDL_SCANCODE_K;
    case ImGuiKey_L:return SDL_Scancode::SDL_SCANCODE_L;
    case ImGuiKey_M:return SDL_Scancode::SDL_SCANCODE_M;
    case ImGuiKey_N:return SDL_Scancode::SDL_SCANCODE_N;
    case ImGuiKey_O:return SDL_Scancode::SDL_SCANCODE_O;
    case ImGuiKey_P:return SDL_Scancode::SDL_SCANCODE_P;
    case ImGuiKey_Q:return SDL_Scancode::SDL_SCANCODE_Q;
    case ImGuiKey_R:return SDL_Scancode::SDL_SCANCODE_R;
    case ImGuiKey_S:return SDL_Scancode::SDL_SCANCODE_S;
    case ImGuiKey_T:return SDL_Scancode::SDL_SCANCODE_T;
    case ImGuiKey_U:return SDL_Scancode::SDL_SCANCODE_U;
    case ImGuiKey_V:return SDL_Scancode::SDL_SCANCODE_V;
    case ImGuiKey_W:return SDL_Scancode::SDL_SCANCODE_W;
    case ImGuiKey_X:return SDL_Scancode::SDL_SCANCODE_X;
    case ImGuiKey_Y:return SDL_Scancode::SDL_SCANCODE_Y;
    case ImGuiKey_Z:return SDL_Scancode::SDL_SCANCODE_Z;
    case ImGuiKey_F1:return SDL_Scancode::SDL_SCANCODE_F1;
    case ImGuiKey_F2:return SDL_Scancode::SDL_SCANCODE_F2;
    case ImGuiKey_F3:return SDL_Scancode::SDL_SCANCODE_F3;
    case ImGuiKey_F4:return SDL_Scancode::SDL_SCANCODE_F4;
    case ImGuiKey_F5:return SDL_Scancode::SDL_SCANCODE_F5;
    case ImGuiKey_F6:return SDL_Scancode::SDL_SCANCODE_F6;
    case ImGuiKey_F7:return SDL_Scancode::SDL_SCANCODE_F7;
    case ImGuiKey_F8:return SDL_Scancode::SDL_SCANCODE_F8;
    case ImGuiKey_F9:return SDL_Scancode::SDL_SCANCODE_F9;
    case ImGuiKey_F10:return SDL_Scancode::SDL_SCANCODE_F10;
    case ImGuiKey_F11:return SDL_Scancode::SDL_SCANCODE_F11;
    case ImGuiKey_F12:return SDL_Scancode::SDL_SCANCODE_F12;
    case ImGuiKey_F13:return SDL_Scancode::SDL_SCANCODE_F13;
    case ImGuiKey_F14:return SDL_Scancode::SDL_SCANCODE_F14;
    case ImGuiKey_F15:return SDL_Scancode::SDL_SCANCODE_F15;
    case ImGuiKey_F16:return SDL_Scancode::SDL_SCANCODE_F16;
    case ImGuiKey_F17:return SDL_Scancode::SDL_SCANCODE_F17;
    case ImGuiKey_F18:return SDL_Scancode::SDL_SCANCODE_F18;
    case ImGuiKey_F19:return SDL_Scancode::SDL_SCANCODE_F19;
    case ImGuiKey_F20:return SDL_Scancode::SDL_SCANCODE_F20;
    case ImGuiKey_F21:return SDL_Scancode::SDL_SCANCODE_F21;
    case ImGuiKey_F22: return SDL_Scancode::SDL_SCANCODE_F22;
    case ImGuiKey_F23:return SDL_Scancode::SDL_SCANCODE_F23;
    case ImGuiKey_F24:return SDL_Scancode::SDL_SCANCODE_F24;
    case ImGuiKey_Apostrophe:return SDL_Scancode::SDL_SCANCODE_APOSTROPHE;
    case ImGuiKey_Comma:return SDL_Scancode::SDL_SCANCODE_COMMA;
    case ImGuiKey_Minus:return SDL_Scancode::SDL_SCANCODE_MINUS;
    case ImGuiKey_Period:return SDL_Scancode::SDL_SCANCODE_PERIOD;
    case ImGuiKey_Slash:return SDL_Scancode::SDL_SCANCODE_SLASH;
    case ImGuiKey_Semicolon:return SDL_Scancode::SDL_SCANCODE_SEMICOLON;
    case ImGuiKey_Equal:return SDL_Scancode::SDL_SCANCODE_EQUALS;
    case ImGuiKey_LeftBracket:return SDL_Scancode::SDL_SCANCODE_LEFTBRACKET;
    case ImGuiKey_Backslash:return SDL_Scancode::SDL_SCANCODE_BACKSLASH;
    case ImGuiKey_RightBracket:return SDL_Scancode::SDL_SCANCODE_RIGHTBRACKET;
    case ImGuiKey_GraveAccent:return SDL_Scancode::SDL_SCANCODE_GRAVE;
    case ImGuiKey_CapsLock:return SDL_Scancode::SDL_SCANCODE_CAPSLOCK;
    case ImGuiKey_ScrollLock:return SDL_Scancode::SDL_SCANCODE_SCROLLLOCK;
    case ImGuiKey_NumLock:return SDL_Scancode::SDL_SCANCODE_NUMLOCKCLEAR;
    case ImGuiKey_PrintScreen:return SDL_Scancode::SDL_SCANCODE_PRINTSCREEN;
    case ImGuiKey_Pause:return SDL_Scancode::SDL_SCANCODE_PAUSE;
    case ImGuiKey_Keypad0:return SDL_Scancode::SDL_SCANCODE_KP_0;
    case ImGuiKey_Keypad1:return SDL_Scancode::SDL_SCANCODE_KP_1;
    case ImGuiKey_Keypad2:return SDL_Scancode::SDL_SCANCODE_KP_2;
    case ImGuiKey_Keypad3:return SDL_Scancode::SDL_SCANCODE_KP_3;
    case ImGuiKey_Keypad4:return SDL_Scancode::SDL_SCANCODE_KP_4;
    case ImGuiKey_Keypad5:return SDL_Scancode::SDL_SCANCODE_KP_5;
    case ImGuiKey_Keypad6:return SDL_Scancode::SDL_SCANCODE_KP_6;
    case ImGuiKey_Keypad7:return SDL_Scancode::SDL_SCANCODE_KP_7;
    case ImGuiKey_Keypad8:return SDL_Scancode::SDL_SCANCODE_KP_8;
    case ImGuiKey_Keypad9:return SDL_Scancode::SDL_SCANCODE_KP_9;
    case ImGuiKey_KeypadDecimal:return SDL_Scancode::SDL_SCANCODE_KP_DECIMAL;
    case ImGuiKey_KeypadDivide:return SDL_Scancode::SDL_SCANCODE_KP_DIVIDE;
    case ImGuiKey_KeypadMultiply:return SDL_Scancode::SDL_SCANCODE_KP_MULTIPLY;
    case ImGuiKey_KeypadSubtract:return SDL_Scancode::SDL_SCANCODE_KP_MINUS;
    case ImGuiKey_KeypadAdd:return SDL_Scancode::SDL_SCANCODE_KP_PLUS;
    case ImGuiKey_KeypadEnter:return SDL_Scancode::SDL_SCANCODE_KP_ENTER;
    case ImGuiKey_KeypadEqual:return SDL_Scancode::SDL_SCANCODE_KP_EQUALS;
    case ImGuiKey_AppBack:return SDL_Scancode::SDL_SCANCODE_AC_BACK;
    case ImGuiKey_AppForward:return SDL_Scancode::SDL_SCANCODE_AC_FORWARD;
    //case ImGuiKey_GamepadStart:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadBack:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadFaceLeft:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadFaceRight:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadFaceUp:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadFaceDown:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadDpadLeft:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadDpadRight:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadDpadUp:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadDpadDown:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadL1:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadR1:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadL2:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadR2:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadL3:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadR3:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadLStickLeft:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadLStickRight:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadLStickUp:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadLStickDown:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadRStickLeft:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadRStickRight:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadRStickUp:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_GamepadRStickDown:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseLeft:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseRight:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseMiddle:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseX1:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseX2:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseWheelX:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_MouseWheelY:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ReservedForModCtrl:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ReservedForModShift:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ReservedForModAlt:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ReservedForModSuper:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ModCtrl:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ModShift:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ModAlt:return SDL_Scancode::SDL_SCANCODE_;
    //case ImGuiKey_ModSuper:return SDL_Scancode::SDL_SCANCODE_;
    default:
        return SDL_Scancode::SDL_SCANCODE_UNKNOWN;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT OverlayImplWin32WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    //switch (msg) {
    //case WM_MOUSEMOVE:
    //case WM_KEYDOWN:
    //}
    return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}


