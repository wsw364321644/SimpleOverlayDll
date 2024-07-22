#include "dxgi-helpers.hpp"
#include "graphics_hook.h"
#include "overlay_ui.h"

#include <d3d10.h>

#include <dxgi.h>
#include <wrl.h>
using namespace Microsoft::WRL;

struct d3d10_data {
	ID3D10Device *device; /* do not release */
	uint32_t cx;
	uint32_t cy;
	DXGI_FORMAT format;
	bool using_shtex;
	bool multisampled;

	ID3D10Texture2D *scale_tex;
	ID3D10ShaderResourceView *scale_resource;

	ID3D10VertexShader *vertex_shader;
	ID3D10InputLayout *vertex_layout;
	ID3D10PixelShader *pixel_shader;

	ID3D10SamplerState *sampler_state;
	ID3D10BlendState *blend_state;
	ID3D10DepthStencilState *zstencil_state;
	ID3D10RasterizerState *raster_state;

	ID3D10Buffer *vertex_buffer;

	union {
		/* shared texture */
		struct {
			shtex_data_t *shtex_info;
			ID3D10Texture2D *texture;
			HANDLE handle;
		};
		/* shared memory */
		struct {
			shmem_data_t *shmem_info;
			ID3D10Texture2D *copy_surfaces[NUM_BUFFERS];
			bool texture_ready[NUM_BUFFERS];
			bool texture_mapped[NUM_BUFFERS];
			uint32_t pitch;
			int cur_tex;
			int copy_wait;
		};
	};
};

static struct d3d10_data data = {};

void d3d10_free(void)
{
	if (data.scale_tex)
		data.scale_tex->Release();
	if (data.scale_resource)
		data.scale_resource->Release();
	if (data.vertex_shader)
		data.vertex_shader->Release();
	if (data.vertex_layout)
		data.vertex_layout->Release();
	if (data.pixel_shader)
		data.pixel_shader->Release();
	if (data.sampler_state)
		data.sampler_state->Release();
	if (data.blend_state)
		data.blend_state->Release();
	if (data.zstencil_state)
		data.zstencil_state->Release();
	if (data.raster_state)
		data.raster_state->Release();
	if (data.vertex_buffer)
		data.vertex_buffer->Release();

	capture_free();

	if (data.using_shtex) {
		if (data.texture)
			data.texture->Release();
	} else {
		for (size_t i = 0; i < NUM_BUFFERS; i++) {
			if (data.copy_surfaces[i]) {
				if (data.texture_mapped[i])
					data.copy_surfaces[i]->Unmap(0);
				data.copy_surfaces[i]->Release();
			}
		}
	}

	memset(&data, 0, sizeof(data));

	SIMPLELOG_LOGGER_DEBUG(nullptr,"----------------- d3d10 capture freed ----------------");
}

static bool create_d3d10_stage_surface(ID3D10Texture2D **tex)
{
	HRESULT hr;

	D3D10_TEXTURE2D_DESC desc = {};
	desc.Width = data.cx;
	desc.Height = data.cy;
	desc.Format = data.format;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D10_USAGE_STAGING;
	desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;

	hr = data.device->CreateTexture2D(&desc, nullptr, tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "create_d3d10_stage_surface: failed to create texture {}",hr);
		return false;
	}

	return true;
}

static bool create_d3d10_tex(uint32_t cx, uint32_t cy, ID3D10Texture2D **tex,
			     HANDLE *handle)
{
	HRESULT hr;

	D3D10_TEXTURE2D_DESC desc = {};
	desc.Width = cx;
	desc.Height = cy;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = apply_dxgi_format_typeless(
		data.format, global_hook_info->allow_srgb_alias);
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D10_USAGE_DEFAULT;
	desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED;

	hr = data.device->CreateTexture2D(&desc, nullptr, tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "create_d3d10_tex: failed to create texture {}", hr);
		return false;
	}

	if (!!handle) {
		IDXGIResource *dxgi_res;
		hr = (*tex)->QueryInterface(__uuidof(IDXGIResource),
					    (void **)&dxgi_res);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "create_d3d10_tex: failed to query IDXGIResource interface from texture {}",hr);
			return false;
		}

		hr = dxgi_res->GetSharedHandle(handle);
		dxgi_res->Release();
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "create_d3d10_tex: failed to get shared handle {}",hr);
			return false;
		}
	}

	return true;
}

static inline bool d3d10_init_format(IDXGISwapChain *swap, HWND &window)
{
	DXGI_SWAP_CHAIN_DESC desc;
	HRESULT hr;

	hr = swap->GetDesc(&desc);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_init_format: swap->GetDesc failed {}", hr);
		return false;
	}

	print_swap_desc(&desc);

	data.format = strip_dxgi_format_srgb(desc.BufferDesc.Format);
	data.multisampled = desc.SampleDesc.Count > 1;
	window = desc.OutputWindow;
	data.cx = desc.BufferDesc.Width;
	data.cy = desc.BufferDesc.Height;

	return true;
}

static bool d3d10_shmem_init_buffers(size_t idx)
{
	bool success;

	success = create_d3d10_stage_surface(&data.copy_surfaces[idx]);
	if (!success) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_shmem_init_buffers: failed to create copy surface");
		return false;
	}

	if (idx == 0) {
		D3D10_MAPPED_TEXTURE2D map = {};
		HRESULT hr;

		hr = data.copy_surfaces[idx]->Map(0, D3D10_MAP_READ, 0, &map);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_shmem_init_buffers: failed to get pitch {}",hr);
			return false;
		}

		data.pitch = map.RowPitch;
		data.copy_surfaces[idx]->Unmap(0);
	}

	return true;
}

static bool d3d10_shmem_init(HWND window)
{
	data.using_shtex = false;

	for (size_t i = 0; i < NUM_BUFFERS; i++) {
		if (!d3d10_shmem_init_buffers(i)) {
			return false;
		}
	}
	if (!capture_init_shmem(&data.shmem_info, window, data.cx, data.cy,
				data.pitch, data.format, false)) {
		return false;
	}

	SIMPLELOG_LOGGER_DEBUG(nullptr, "d3d10 memory capture successful");
	return true;
}

static bool d3d10_shtex_init(HWND window)
{
	bool success;

	data.using_shtex = true;

	success =
		create_d3d10_tex(data.cx, data.cy, &data.texture, &data.handle);

	if (!success) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_shtex_init: failed to create texture");
		return false;
	}
	if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
				data.format, false, (uintptr_t)data.handle)) {
		return false;
	}

	SIMPLELOG_LOGGER_DEBUG(nullptr, "d3d10 shared texture capture successful");
	return true;
}

static void d3d10_init(IDXGISwapChain *swap)
{
	HWND window;
	HRESULT hr;

	hr = swap->GetDevice(__uuidof(ID3D10Device), (void **)&data.device);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_init: failed to get device from swap {}", hr);
		return;
	}

	/* remove the unneeded extra reference */
	data.device->Release();

	if (!d3d10_init_format(swap, window)) {
		return;
	}

	const bool success = global_hook_info->force_shmem
				     ? d3d10_shmem_init(window)
				     : d3d10_shtex_init(window);
	if (!success)
		d3d10_free();
}

static inline void d3d10_copy_texture(ID3D10Resource *dst, ID3D10Resource *src)
{
	if (data.multisampled) {
		data.device->ResolveSubresource(dst, 0, src, 0, data.format);
	} else {
		data.device->CopyResource(dst, src);
	}
}

static inline void d3d10_shtex_capture(ID3D10Resource *backbuffer)
{
	d3d10_copy_texture(data.texture, backbuffer);
}

static void d3d10_shmem_capture_copy(int i)
{
	D3D10_MAPPED_TEXTURE2D map;
	HRESULT hr;

	if (data.texture_ready[i]) {
		data.texture_ready[i] = false;

		hr = data.copy_surfaces[i]->Map(0, D3D10_MAP_READ, 0, &map);
		if (SUCCEEDED(hr)) {
			data.texture_mapped[i] = true;
			shmem_copy_data(i, map.pData);
		}
	}
}

static inline void d3d10_shmem_capture(ID3D10Resource *backbuffer)
{
	int next_tex;

	next_tex = (data.cur_tex + 1) % NUM_BUFFERS;
	d3d10_shmem_capture_copy(next_tex);

	if (data.copy_wait < NUM_BUFFERS - 1) {
		data.copy_wait++;
	} else {
		if (shmem_texture_data_lock(data.cur_tex)) {
			data.copy_surfaces[data.cur_tex]->Unmap(0);
			data.texture_mapped[data.cur_tex] = false;
			shmem_texture_data_unlock(data.cur_tex);
		}

		d3d10_copy_texture(data.copy_surfaces[data.cur_tex],
				   backbuffer);
		data.texture_ready[data.cur_tex] = true;
	}

	data.cur_tex = next_tex;
}

void d3d10_capture(void* swap_ptr, void* backbuffer_ptr)
{
	IDXGIResource* dxgi_backbuffer = (IDXGIResource*)backbuffer_ptr;
	IDXGISwapChain* swap = (IDXGISwapChain*)swap_ptr;

	HRESULT hr;
	if (capture_should_stop()) {
		d3d10_free();
	}
	if (capture_should_init()) {
		d3d10_init(swap);
	}
	//if (is_capture_ready()) {
	//	ID3D10Resource *backbuffer;
	//	hr = dxgi_backbuffer->QueryInterface(__uuidof(ID3D10Resource),
	//					     (void **)&backbuffer);
	//	if (FAILED(hr)) {
	//		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d10_shtex_capture: failed to get backbuffer {}",hr);
	//		return;
	//	}
	//	if (data.using_shtex)
	//		d3d10_shtex_capture(backbuffer);
	//	else
	//		d3d10_shmem_capture(backbuffer);
	//	backbuffer->Release();
	//}

	if (is_capture_active()) {
		//d3d10_window_update();
		if (is_overlay_active()) {
			overlay_ui_new_frame();
			///dx9_render_draw_data(overlay_ui_render());
		}
	}
}


////// gui code
//
//typedef struct SharedWindowGraphicInfo_t {
//	ComPtr<IDirect3DTexture9> CopyTexDX9;
//	ComPtr<IDirect3DSurface9> CopySurfaceDX9;
//	ComPtr<IDirect3DTexture9> WindowTexDX9;
//	ComPtr<IDirect3DSurface9> WindowSurfaceDX9;
//	ComPtr<IDirect3DSurface9> TempSurfaceDX9;
//	ComPtr<ID3D11Texture2D> WindowTexDX11;
//	ComPtr<ID3D11Texture2D> CopyTexDX11;
//	ComPtr<ID3D11Texture2D> TestTexDX11;
//
//}SharedWindowGraphicInfo_t;
//static std::unordered_map<uint64_t, std::shared_ptr<SharedWindowGraphicInfo_t>>  SharedWindowGraphicInfos;
//
//static void d3d10_window_update() {
//	HRESULT hr;
//	std::set<uint64_t> needDel;
//	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
//		std::inserter(needDel, needDel.begin()),
//		[](const std::pair<uint64_t, std::shared_ptr<SharedWindowGraphicInfo_t>>& key_value)
//		{ return key_value.first; });
//
//	for (auto& windowInfo : SharedWindowInfos) {
//		auto& id = windowInfo->Id;
//		HANDLE sharedHandle = (HANDLE)windowInfo->Info->shared_handle;
//		needDel.erase(id);
//		if (SharedWindowGraphicInfos.contains(id)) {
//			continue;
//		}
//		auto res = SharedWindowGraphicInfos.emplace(id, std::make_shared<SharedWindowGraphicInfo_t>());
//		if (!res.second) {
//			continue;
//		}
//		auto pinfo = res.first->second;
//		if (windowInfo->Info->bNT_shared) {
//			ComPtr<ID3D11Device1> dev;
//			hr = data.d3d11_device->QueryInterface(IID_PPV_ARGS(&dev));
//			if (FAILED(hr)) {
//				d3d9_remove_window(id);
//				continue;
//			}
//			hr = dev->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
//			if (FAILED(hr)) {
//				d3d9_remove_window(id);
//				continue;
//			}
//
//		}
//		else {
//			hr = data.d3d11_device->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
//			if (FAILED(hr)) {
//				d3d9_remove_window(id);
//				continue;
//			}
//		}
//		HANDLE copyHandle{ NULL };
//		d3d9_patch();
//		hr = data.device->CreateTexture(data.cx, data.cy, 1,
//			D3DUSAGE_RENDERTARGET, data.d3d9_format, D3DPOOL_DEFAULT, &pinfo->CopyTexDX9, &copyHandle);
//		d3d9_unpatch();
//		if (FAILED(hr)) {
//			d3d9_remove_window(id);
//			continue;
//		}
//		hr = pinfo->CopyTexDX9->GetSurfaceLevel(0, &pinfo->CopySurfaceDX9);
//		if (FAILED(hr)) {
//			d3d9_remove_window(id);
//			continue;
//		}
//
//		hr = data.d3d11_device->OpenSharedResource(copyHandle, IID_PPV_ARGS(&pinfo->CopyTexDX11));
//		if (FAILED(hr)) {
//			d3d9_remove_window(id);
//			continue;
//		}
//		windowInfo->WindowTextureID = (intptr_t)pinfo->CopyTexDX9.Get();
//	}
//	for (auto& id : needDel) {
//		SharedWindowGraphicInfos.erase(id);
//	}
//	for (auto& pair : SharedWindowGraphicInfos) {
//		auto& pSharedWindowGraphicInfo = pair.second;
//		ComPtr<IDXGIKeyedMutex>  pDXGIKeyedMutex;
//		hr = pSharedWindowGraphicInfo->WindowTexDX11.As(&pDXGIKeyedMutex);
//		if (FAILED(hr)) {
//			needDel.insert(pair.first);
//			continue;
//		}
//		hr = pDXGIKeyedMutex->AcquireSync(1, 0);
//		if (hr != WAIT_OBJECT_0) {
//			continue;
//		}
//		data.d3d11_context->CopyResource(pSharedWindowGraphicInfo->CopyTexDX11.Get(), pSharedWindowGraphicInfo->WindowTexDX11.Get());
//		pDXGIKeyedMutex->ReleaseSync(0);
//	}
//	for (auto& id : needDel) {
//		SharedWindowGraphicInfos.erase(id);
//	}
//}
//
