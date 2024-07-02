#pragma once

#include "graphics_hook.h"
#include <LoggerHelper.h>
static inline DXGI_FORMAT strip_dxgi_format_srgb(DXGI_FORMAT format)
{
	switch ((unsigned long)format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	return format;
}

static inline DXGI_FORMAT apply_dxgi_format_typeless(DXGI_FORMAT format,
						     bool allow_srgb_alias)
{
	if (allow_srgb_alias) {
		switch ((unsigned long)format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		}
	}

	return format;
}

static void print_swap_desc(const DXGI_SWAP_CHAIN_DESC *desc)
{
	SIMPLELOG_LOGGER_INFO(nullptr,"DXGI_SWAP_CHAIN_DESC:\n		  \
	         BufferDesc.Width: {}\n								  \
	         BufferDesc.Height: {}\n							  \
	         BufferDesc.RefreshRate.Numerator: {}\n				  \
	         BufferDesc.RefreshRate.Denominator: {}\n			  \
	         BufferDesc.Format: {}\n							  \
	         BufferDesc.ScanlineOrdering: {}\n					  \
	         BufferDesc.Scaling: {}\n							  \
	         SampleDesc.Count: {}\n								  \
	         SampleDesc.Quality: {}\n							  \
	         BufferUsage: {}\n									  \
	         BufferCount: {}\n									  \
	         Windowed: {}\n										  \
	         SwapEffect: {}\n									  \
	         Flags: {}",
	     desc->BufferDesc.Width, desc->BufferDesc.Height,
	     desc->BufferDesc.RefreshRate.Numerator,
	     desc->BufferDesc.RefreshRate.Denominator, (uint32_t)desc->BufferDesc.Format,
		 (uint32_t)desc->BufferDesc.ScanlineOrdering, (uint32_t)desc->BufferDesc.Scaling,
	     desc->SampleDesc.Count, desc->SampleDesc.Quality,
	     desc->BufferUsage, desc->BufferCount, desc->Windowed,
	     (uint32_t)desc->SwapEffect, desc->Flags);
}
