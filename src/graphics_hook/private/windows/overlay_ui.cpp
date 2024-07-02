#include "overlay_ui.h"

#include "simple_os_defs.h"
#include "game_hook.h"
#include <imgui_impl_win32.h>

ImGuiContext* pImGuiCtx;
bool show_demo_window{true};
bool init_overlay_ui(HWND window) {
    auto pImGuiCtx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::StyleColorsDark();

#ifdef WIN32
    if (!ImGui_ImplWin32_Init(window)) {
        return false;
    }
#endif

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Allow user UI scaling using CTRL+Mouse Wheel scrolling
    io.FontAllowUserScaling = true;

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
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    ImGui::ShowDemoWindow(&show_demo_window);
    for (auto& pwinInfo : SharedWindowInfos) {
        auto windowType = pwinInfo->Info->hook_window_type;
        
        std::string windowName("window");
        windowName += std::to_string( pwinInfo->Id);
        ImGuiWindowFlags flags{ 0 };
        if (windowType == EHookWindowType::Background) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            flags |= ImGuiWindowFlags_NoResize| ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        }
        ImGui::Begin(windowName.c_str(),NULL, flags);
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + pwinInfo->Info->width, canvas_p0.y + pwinInfo->Info->height);

        ImGui::GetWindowDrawList()->AddImage((ImTextureID)pwinInfo->WindowTextureID, canvas_p0, canvas_p1);
        ImGui::End();
    }

    ImGui::EndFrame();
}

ImDrawData* overlay_ui_render()
{
    ImGui::Render();
    return ImGui::GetDrawData();
}
