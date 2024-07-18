#pragma once
#include <stdint.h>
#include <HOOK/graphics_info.h>

#include "game_hook.h"

#define COMPILE_D3D12_HOOK 1

bool hook_d3d9(void);
bool hook_d3d12(void);
bool hook_dxgi(void);

extern void d3d10_capture(void* swap, void* backbuffer);
extern void d3d10_free(void);
extern void d3d11_capture(void* swap, void* backbuffer);
extern void d3d11_free(void);

#ifdef COMPILE_D3D12_HOOK
extern void d3d12_capture(void* swap, void* backbuffer);
extern void d3d12_free(void);
#endif


static inline bool d3d8_hookable(void)
{
    return !!global_hook_info->offsets.d3d8.present;
}

static inline bool ddraw_hookable(void)
{
    return !!global_hook_info->offsets.ddraw.surface_create &&
        !!global_hook_info->offsets.ddraw.surface_restore &&
        !!global_hook_info->offsets.ddraw.surface_release &&
        !!global_hook_info->offsets.ddraw.surface_unlock &&
        !!global_hook_info->offsets.ddraw.surface_blt &&
        !!global_hook_info->offsets.ddraw.surface_flip &&
        !!global_hook_info->offsets.ddraw.surface_set_palette &&
        !!global_hook_info->offsets.ddraw.palette_set_entries;
}

static inline bool d3d9_hookable(void)
{
    return !!global_hook_info->offsets.d3d9.present &&
        !!global_hook_info->offsets.d3d9.present_ex &&
        !!global_hook_info->offsets.d3d9.present_swap;
}

static inline bool dxgi_hookable(void)
{
    return !!global_hook_info->offsets.dxgi.present &&
        !!global_hook_info->offsets.dxgi.resize;
}


#if COMPILE_VULKAN_HOOK
extern __declspec(thread) int vk_presenting;
#endif

static inline bool should_passthrough()
{
#if COMPILE_VULKAN_HOOK
    return vk_presenting > 0;
#else
    return false;
#endif
}
