#pragma once
#include <simple_os_defs.h>
#include <imgui.h>
#include <SDL2/SDL.h>

bool init_overlay_ui(HWND);
void get_font_tex_data_RGBA32(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel);
void set_font_tex(ImTextureID tex_id);
void destroy_overlay_ui();
void overlay_ui_new_frame();
ImDrawData* overlay_ui_render();
SDL_Scancode imgui_key_to_sdl_scancode(ImGuiKey key);