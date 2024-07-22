#include "dxgi-helpers.hpp"
#include "graphics_hook.h"
#include "overlay_ui.h"	
#include "gui_shader.h"
#include "windows_capture.h"
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <wrl.h>
using namespace Microsoft::WRL;

///// gui pre declare
static void d3d11_window_update();
static void dx11_render_draw_data(ImDrawData* draw_data);
static bool d3d11_init_gui(HWND window);

struct d3d11_data {
	ID3D11Device* device;         /* do not release */
	ID3D11DeviceContext* context; /* do not release */
	uint32_t cx;
	uint32_t cy;
	DXGI_FORMAT format;
	bool using_shtex;
	bool multisampled;

	//ID3D11Texture2D* scale_tex;
	//ID3D11ShaderResourceView* scale_resource;
	//ID3D11Buffer* vertex_buffer;
	//ID3D11VertexShader* vertex_shader;
	//ID3D11InputLayout* vertex_layout;
	//ID3D11PixelShader* pixel_shader;

	//ID3D11SamplerState* sampler_state;
	//ID3D11BlendState* blend_state;
	//ID3D11DepthStencilState* zstencil_state;
	//ID3D11RasterizerState* raster_state;

	ID3D11Buffer* pVertexConstantBuffer;
	ID3D11Buffer* pVB;
	ID3D11Buffer* pIB;
	int VertexBufferSize;
	int IndexBufferSize;

	ID3D11InputLayout* pInputLayout;
	ID3D11PixelShader* pPixelShader;
	ID3D11VertexShader* pVertexShader;
	ID3D11SamplerState* pFontSampler;
	ID3D11ShaderResourceView* pFontTextureView;
	ID3D11RasterizerState* pRasterizerState;
	ID3D11BlendState* pBlendState;
	ID3D11DepthStencilState* pDepthStencilState;
	union {
		/* shared texture */
		struct {
			shtex_data_t* shtex_info;
			ID3D11Texture2D* texture;
			HANDLE handle;
		};
		/* shared memory */
		struct {
			ID3D11Texture2D* copy_surfaces[NUM_BUFFERS];
			bool texture_ready[NUM_BUFFERS];
			bool texture_mapped[NUM_BUFFERS];
			uint32_t pitch;
			shmem_data_t* shmem_info;
			int cur_tex;
			int copy_wait;
		};
	};
};

static struct d3d11_data data = {};

void d3d11_free(void)
{
	//if (data.scale_tex)
	//	data.scale_tex->Release();
	//if (data.scale_resource)
	//	data.scale_resource->Release();
	//if (data.vertex_shader)
	//	data.vertex_shader->Release();
	//if (data.vertex_layout)
	//	data.vertex_layout->Release();
	//if (data.pixel_shader)
	//	data.pixel_shader->Release();
	//if (data.vertex_buffer)
	//	data.vertex_buffer->Release();

	//if (data.sampler_state)
	//	data.sampler_state->Release();
	//if (data.blend_state)
	//	data.blend_state->Release();
	//if (data.zstencil_state)
	//	data.zstencil_state->Release();
	//if (data.raster_state)
	//	data.raster_state->Release();
	
	if (data.pVertexConstantBuffer)
		data.pVertexConstantBuffer->Release();
	if (data.pVB)
		data.pVB->Release();
	if (data.pIB)
		data.pIB->Release();
	if (data.pInputLayout)
		data.pInputLayout->Release();
	if (data.pPixelShader)
		data.pPixelShader->Release();
	if (data.pVertexShader)
		data.pVertexShader->Release();
	if (data.pFontSampler)
		data.pFontSampler->Release();
	if (data.pFontTextureView)
		data.pFontTextureView->Release();
	if (data.pRasterizerState)
		data.pRasterizerState->Release();
	if (data.pBlendState)
		data.pBlendState->Release();
	if (data.pDepthStencilState)
		data.pDepthStencilState->Release();

	if (data.using_shtex) {
		if (data.texture)
			data.texture->Release();
	}
	else {
		for (size_t i = 0; i < NUM_BUFFERS; i++) {
			if (data.copy_surfaces[i]) {
				if (data.texture_mapped[i])
					data.context->Unmap(
						data.copy_surfaces[i], 0);
				data.copy_surfaces[i]->Release();
			}
		}
	}

	memset(&data, 0, sizeof(data));

	capture_free();

	SIMPLELOG_LOGGER_DEBUG(nullptr,"----------------- d3d11 capture freed ----------------");
}

static bool create_d3d11_stage_surface(ID3D11Texture2D** tex)
{
	HRESULT hr;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = data.cx;
	desc.Height = data.cy;
	desc.Format = data.format;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	hr = data.device->CreateTexture2D(&desc, nullptr, tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d11_stage_surface: failed to create texture {}",hr);
		return false;
	}

	return true;
}

static bool create_d3d11_tex(uint32_t cx, uint32_t cy, ID3D11Texture2D** tex,
	HANDLE* handle)
{
	HRESULT hr;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = cx;
	desc.Height = cy;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = apply_dxgi_format_typeless(
		data.format, global_hook_info->allow_srgb_alias);
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	hr = data.device->CreateTexture2D(&desc, nullptr, tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d11_tex: failed to create texture {}", hr);
		return false;
	}

	if (!!handle) {
		IDXGIResource* dxgi_res;
		hr = (*tex)->QueryInterface(__uuidof(IDXGIResource),
			(void**)&dxgi_res);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d11_tex: failed to query IDXGIResource interface from texture {}",hr);
			return false;
		}

		hr = dxgi_res->GetSharedHandle(handle);
		dxgi_res->Release();
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d11_tex: failed to get shared handle {}",hr);
			return false;
		}
	}

	return true;
}

static inline bool d3d11_init_format(IDXGISwapChain* swap, HWND& window)
{
	DXGI_SWAP_CHAIN_DESC desc;
	HRESULT hr;

	hr = swap->GetDesc(&desc);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d11_init_format: swap->GetDesc failed {}", hr);
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

static bool d3d11_shmem_init_buffers(size_t idx)
{
	bool success;

	success = create_d3d11_stage_surface(&data.copy_surfaces[idx]);
	if (!success) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d11_shmem_init_buffers: failed to create copy surface");
		return false;
	}

	if (idx == 0) {
		D3D11_MAPPED_SUBRESOURCE map = {};
		HRESULT hr;

		hr = data.context->Map(data.copy_surfaces[idx], 0,
			D3D11_MAP_READ, 0, &map);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"d3d11_shmem_init_buffers: failed to get pitch {}",hr);
			return false;
		}

		data.pitch = map.RowPitch;
		data.context->Unmap(data.copy_surfaces[idx], 0);
	}

	return true;
}

static bool d3d11_shmem_init(HWND window)
{
	data.using_shtex = false;

	for (size_t i = 0; i < NUM_BUFFERS; i++) {
		if (!d3d11_shmem_init_buffers(i)) {
			return false;
		}
	}
	if (!capture_init_shmem(&data.shmem_info, window, data.cx, data.cy,
		data.pitch, data.format, false)) {
		return false;
	}

	SIMPLELOG_LOGGER_DEBUG(nullptr, "d3d11 memory capture successful");
	return true;
}

static bool d3d11_shtex_init(HWND window)
{
	bool success;

	data.using_shtex = true;

	success =
		create_d3d11_tex(data.cx, data.cy, &data.texture, &data.handle);

	if (!success) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d11_shtex_init: failed to create texture");
		return false;
	}
	if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
		data.format, false, (uintptr_t)data.handle)) {
		return false;
	}

	SIMPLELOG_LOGGER_DEBUG(nullptr, "d3d11 shared texture capture successful");
	return true;
}

static void d3d11_init(IDXGISwapChain* swap)
{
	HWND window;
	HRESULT hr;

	hr = swap->GetDevice(__uuidof(ID3D11Device), (void**)&data.device);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d11_init: failed to get device from swap {}", hr);
		return;
	}

	data.device->Release();

	data.device->GetImmediateContext(&data.context);
	data.context->Release();

	if (!d3d11_init_format(swap, window)) {
		return;
	}
	
	d3d11_init_gui(window);
	const bool success = global_hook_info->force_shmem
		? d3d11_shmem_init(window)
		: d3d11_shtex_init(window);
	if (!success)
		d3d11_free();
}

static inline void d3d11_copy_texture(ID3D11Resource* dst, ID3D11Resource* src)
{
	if (data.multisampled) {
		data.context->ResolveSubresource(dst, 0, src, 0, data.format);
	}
	else {
		data.context->CopyResource(dst, src);
	}
}

static inline void d3d11_shtex_capture(ID3D11Resource* backbuffer)
{
	d3d11_copy_texture(data.texture, backbuffer);
}

static void d3d11_shmem_capture_copy(int i)
{
	D3D11_MAPPED_SUBRESOURCE map;
	HRESULT hr;

	if (data.texture_ready[i]) {
		data.texture_ready[i] = false;

		hr = data.context->Map(data.copy_surfaces[i], 0, D3D11_MAP_READ,
			0, &map);
		if (SUCCEEDED(hr)) {
			data.texture_mapped[i] = true;
			shmem_copy_data(i, map.pData);
		}
	}
}

static inline void d3d11_shmem_capture(ID3D11Resource* backbuffer)
{
	int next_tex;

	next_tex = (data.cur_tex + 1) % NUM_BUFFERS;
	d3d11_shmem_capture_copy(next_tex);

	if (data.copy_wait < NUM_BUFFERS - 1) {
		data.copy_wait++;
	}
	else {
		if (shmem_texture_data_lock(data.cur_tex)) {
			data.context->Unmap(data.copy_surfaces[data.cur_tex],
				0);
			data.texture_mapped[data.cur_tex] = false;
			shmem_texture_data_unlock(data.cur_tex);
		}

		d3d11_copy_texture(data.copy_surfaces[data.cur_tex],
			backbuffer);
		data.texture_ready[data.cur_tex] = true;
	}

	data.cur_tex = next_tex;
}


void d3d11_capture(void* swap_ptr, void* backbuffer_ptr)
{
	IDXGIResource* dxgi_backbuffer = (IDXGIResource*)backbuffer_ptr;
	IDXGISwapChain* swap = (IDXGISwapChain*)swap_ptr;

	HRESULT hr;
	if (capture_should_stop()) {
		d3d11_free();
	}
	if (capture_should_init()) {
		d3d11_init(swap);
	}
	//if (is_capture_ready()) {
	//	ID3D11Resource* backbuffer;
	//	hr = dxgi_backbuffer->QueryInterface(__uuidof(ID3D11Resource),
	//		(void**)&backbuffer);
	//	if (FAILED(hr)) {
	//		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d11_shtex_capture: failed to get backbuffer {}",hr);
	//		return;
	//	}
	//	if (data.using_shtex)
	//		d3d11_shtex_capture(backbuffer);
	//	else
	//		d3d11_shmem_capture(backbuffer);
	//	backbuffer->Release();
	//}
	if (is_capture_active()) {
		d3d11_window_update();
		if (is_overlay_active()) {
			overlay_ui_new_frame();
			dx11_render_draw_data(overlay_ui_render());
		}
	}
}


/// gui code

struct VERTEX_CONSTANT_BUFFER_DX11
{
	float   mvp[4][4];
};

static bool d3d11_create_font_texture() {
	// Build texture atlas
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	{
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;

		ID3D11Texture2D* pTexture = nullptr;
		D3D11_SUBRESOURCE_DATA subResource;
		subResource.pSysMem = pixels;
		subResource.SysMemPitch = desc.Width * 4;
		subResource.SysMemSlicePitch = 0;
		data.device->CreateTexture2D(&desc, &subResource, &pTexture);
		IM_ASSERT(pTexture != nullptr);

		// Create texture view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		data.device->CreateShaderResourceView(pTexture, &srvDesc, &data.pFontTextureView);
		pTexture->Release();
	}

	// Store our identifier
	io.Fonts->SetTexID((ImTextureID)data.pFontTextureView);

	// Create texture sampler
	// (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
	{
		D3D11_SAMPLER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.MipLODBias = 0.f;
		desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		desc.MinLOD = 0.f;
		desc.MaxLOD = 0.f;
		data.device->CreateSamplerState(&desc, &data.pFontSampler);
	}
	return true;
}
static bool d3d11_init_gui(HWND window) {
	if (!data.device) {
		return false;
	}
	if (data.pFontSampler) {
		return true;
	}
	if (main_window != window) {
		if (main_window) {
			destroy_overlay_ui();
		}
		init_overlay_ui(window);
	}
	set_render_window(window);

	// Create the vertex shader
	{
		//ID3DBlob* vertexShaderBlob;
		//if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr)))
		//	return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
		//if (data.device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &data.pVertexShader) != S_OK)
		//{
		//	vertexShaderBlob->Release();
		//	return false;
		//}

		if (data.device->CreateVertexShader(g_preCompiled_pVertexShaderBlob, sizeof(g_preCompiled_pVertexShaderBlob), nullptr, &data.pVertexShader) != S_OK)
		{
			return false;
		}
		// Create the input layout
		D3D11_INPUT_ELEMENT_DESC local_layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		//if (data.device->CreateInputLayout(local_layout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &data.pInputLayout) != S_OK)
		//{
		//	vertexShaderBlob->Release();
		//	return false;
		//}
		//vertexShaderBlob->Release();

		if (data.device->CreateInputLayout(local_layout, 3, g_preCompiled_pVertexShaderBlob, sizeof(g_preCompiled_pVertexShaderBlob), &data.pInputLayout) != S_OK)
		{
			return false;
		}
		 
		// Create the constant buffer
		{
			D3D11_BUFFER_DESC desc;
			desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX11);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			data.device->CreateBuffer(&desc, nullptr, &data.pVertexConstantBuffer);
		}
	}

	// Create the pixel shader
	{
		//ID3DBlob* pixelShaderBlob;
		//if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
		//	return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
		//if (data.device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &data.pPixelShader) != S_OK)
		//{
		//	pixelShaderBlob->Release();
		//	return false;
		//}
		//pixelShaderBlob->Release();
		if (data.device->CreatePixelShader(g_preCompiled_pPixelShaderBlob, sizeof(g_preCompiled_pPixelShaderBlob), nullptr, &data.pPixelShader) != S_OK)
		{
			return false;
		}
	}

	// Create the blending setup
	{
		D3D11_BLEND_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.AlphaToCoverageEnable = false;
		desc.RenderTarget[0].BlendEnable = true;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		data.device->CreateBlendState(&desc, &data.pBlendState);
	}

	// Create the rasterizer state
	{
		D3D11_RASTERIZER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.ScissorEnable = true;
		desc.DepthClipEnable = true;
		data.device->CreateRasterizerState(&desc, &data.pRasterizerState);
	}

	// Create depth-stencil State
	{
		D3D11_DEPTH_STENCIL_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.DepthEnable = false;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		desc.StencilEnable = false;
		desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		desc.BackFace = desc.FrontFace;
		data.device->CreateDepthStencilState(&desc, &data.pDepthStencilState);
	}

	d3d11_create_font_texture();

	return true;
}

typedef struct DX11SharedWindowGraphicInfo_t {
	ComPtr<ID3D11Texture2D> WindowTexDX11{ nullptr };
	ComPtr<ID3D11Texture2D> CopyTexDX11{ nullptr };
	ComPtr<ID3D11ShaderResourceView> CopySRVDX11{ nullptr };
	ComPtr<ID3D11Texture2D> TestTexDX11{ nullptr };

}DX11SharedWindowGraphicInfo_t;
static std::unordered_map<uint64_t, std::shared_ptr<DX11SharedWindowGraphicInfo_t>>  SharedWindowGraphicInfos;

static void d3d11_remove_window(uint64_t id)
{
	auto itr = SharedWindowGraphicInfos.find(id);
	if (itr == SharedWindowGraphicInfos.end()) {
		return;
	}
	SharedWindowGraphicInfos.erase(id);
}
static void d3d11_window_update() {
	HRESULT hr;
	std::set<uint64_t> needDel;
	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
		std::inserter(needDel, needDel.begin()),
		[](const std::pair<uint64_t, std::shared_ptr<DX11SharedWindowGraphicInfo_t>>& key_value)
		{ return key_value.first; });

	for (auto& windowInfo : SharedWindowInfos) {
		auto& id = windowInfo->Id;
		HANDLE sharedHandle = (HANDLE)windowInfo->Info->shared_handle;
		needDel.erase(id);
		if (SharedWindowGraphicInfos.contains(id)) {
			continue;
		}
		auto res = SharedWindowGraphicInfos.emplace(id, std::make_shared<DX11SharedWindowGraphicInfo_t>());
		if (!res.second) {
			continue;
		}
		auto& pinfo = res.first->second;
		if (windowInfo->Info->bNT_shared) {
			ComPtr<ID3D11Device1> dev;
			hr = data.device->QueryInterface(IID_PPV_ARGS(&dev));
			if (FAILED(hr)) {
				d3d11_remove_window(id);
				continue;
			}
			hr = dev->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d11_remove_window(id);
				continue;
			}

		}
		else {
			hr = data.device->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d11_remove_window(id);
				continue;
			}
		}
		
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = windowInfo->Info->width;
		desc.Height = windowInfo->Info->height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = apply_dxgi_format_typeless(
			data.format, global_hook_info->allow_srgb_alias);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;

		hr = data.device->CreateTexture2D(&desc, NULL, &pinfo->CopyTexDX11);
		if (FAILED(hr)) {
			d3d11_remove_window(id);
			continue;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = data.format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		hr = data.device->CreateShaderResourceView(pinfo->CopyTexDX11.Get(), &srvDesc, &pinfo->CopySRVDX11);
		if (FAILED(hr)) {
			d3d11_remove_window(id);
			continue;
		}

		windowInfo->WindowTextureID = (intptr_t)pinfo->CopySRVDX11.Get();
	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
	for (auto& pair : SharedWindowGraphicInfos) {
		auto& pSharedWindowGraphicInfo = pair.second;
		ComPtr<IDXGIKeyedMutex>  pDXGIKeyedMutex;
		hr = pSharedWindowGraphicInfo->WindowTexDX11.As(&pDXGIKeyedMutex);
		if (FAILED(hr)) {
			needDel.insert(pair.first);
			continue;
		}
		hr = pDXGIKeyedMutex->AcquireSync(1, 0);
		if (hr != WAIT_OBJECT_0) {
			continue;
		}
		data.context->CopyResource(pSharedWindowGraphicInfo->CopyTexDX11.Get(), pSharedWindowGraphicInfo->WindowTexDX11.Get());
		pDXGIKeyedMutex->ReleaseSync(0);
	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
}



static void dx11_setup_render_state(ImDrawData* draw_data, ID3D11DeviceContext* ctx) {
	// Setup viewport
	D3D11_VIEWPORT vp;
	memset(&vp, 0, sizeof(D3D11_VIEWPORT));
	vp.Width = draw_data->DisplaySize.x;
	vp.Height = draw_data->DisplaySize.y;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = vp.TopLeftY = 0;
	ctx->RSSetViewports(1, &vp);

	// Setup shader and vertex buffers
	unsigned int stride = sizeof(ImDrawVert);
	unsigned int offset = 0;
	ctx->IASetInputLayout(data.pInputLayout);
	ctx->IASetVertexBuffers(0, 1, &data.pVB, &stride, &offset);
	ctx->IASetIndexBuffer(data.pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(data.pVertexShader, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &data.pVertexConstantBuffer);
	ctx->PSSetShader(data.pPixelShader, nullptr, 0);
	ctx->PSSetSamplers(0, 1, &data.pFontSampler);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
	ctx->DSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
	ctx->CSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..

	// Setup blend state
	const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
	ctx->OMSetBlendState(data.pBlendState, blend_factor, 0xffffffff);
	ctx->OMSetDepthStencilState(data.pDepthStencilState, 0);
	ctx->RSSetState(data.pRasterizerState);
}

static void dx11_render_draw_data(ImDrawData* draw_data) {
	// Avoid rendering when minimized
	if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
		return;

	ID3D11DeviceContext* ctx = data.context;

	// Create and grow vertex/index buffers if needed
	if (!data.pVB || data.VertexBufferSize < draw_data->TotalVtxCount)
	{
		if (data.pVB) { data.pVB->Release(); data.pVB = nullptr; }
		data.VertexBufferSize = draw_data->TotalVtxCount + 5000;
		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = data.VertexBufferSize * sizeof(ImDrawVert);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		if (data.device->CreateBuffer(&desc, nullptr, &data.pVB) < 0)
			return;
	}
	if (!data.pIB || data.IndexBufferSize < draw_data->TotalIdxCount)
	{
		if (data.pIB) { data.pIB->Release(); data.pIB = nullptr; }
		data.IndexBufferSize = draw_data->TotalIdxCount + 10000;
		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = data.IndexBufferSize * sizeof(ImDrawIdx);
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (data.device->CreateBuffer(&desc, nullptr, &data.pIB) < 0)
			return;
	}

	// Upload vertex/index data into a single contiguous GPU buffer
	D3D11_MAPPED_SUBRESOURCE vtx_resource, idx_resource;
	if (ctx->Map(data.pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource) != S_OK)
		return;
	if (ctx->Map(data.pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource) != S_OK)
		return;
	ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource.pData;
	ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource.pData;
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}
	ctx->Unmap(data.pVB, 0);
	ctx->Unmap(data.pIB, 0);

	// Setup orthographic projection matrix into our constant buffer
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	{
		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		if (ctx->Map(data.pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource) != S_OK)
			return;
		VERTEX_CONSTANT_BUFFER_DX11* constant_buffer = (VERTEX_CONSTANT_BUFFER_DX11*)mapped_resource.pData;
		float L = draw_data->DisplayPos.x;
		float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
		float T = draw_data->DisplayPos.y;
		float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
		float mvp[4][4] =
		{
			{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
			{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
			{ 0.0f,         0.0f,           0.5f,       0.0f },
			{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
		};
		memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
		ctx->Unmap(data.pVertexConstantBuffer, 0);
	}

	// Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
	struct BACKUP_DX11_STATE
	{
		UINT                        ScissorRectsCount, ViewportsCount;
		D3D11_RECT                  ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		D3D11_VIEWPORT              Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		ID3D11RasterizerState* RS;
		ID3D11BlendState* BlendState;
		FLOAT                       BlendFactor[4];
		UINT                        SampleMask;
		UINT                        StencilRef;
		ID3D11DepthStencilState* DepthStencilState;
		ID3D11ShaderResourceView* PSShaderResource;
		ID3D11SamplerState* PSSampler;
		ID3D11PixelShader* PS;
		ID3D11VertexShader* VS;
		ID3D11GeometryShader* GS;
		UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
		ID3D11ClassInstance* PSInstances[256], * VSInstances[256], * GSInstances[256];   // 256 is max according to PSSetShader documentation
		D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
		ID3D11Buffer* IndexBuffer, * VertexBuffer, * VSConstantBuffer;
		UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
		DXGI_FORMAT                 IndexBufferFormat;
		ID3D11InputLayout* InputLayout;
	};
	BACKUP_DX11_STATE old = {};
	old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	ctx->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
	ctx->RSGetViewports(&old.ViewportsCount, old.Viewports);
	ctx->RSGetState(&old.RS);
	ctx->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
	ctx->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
	ctx->PSGetShaderResources(0, 1, &old.PSShaderResource);
	ctx->PSGetSamplers(0, 1, &old.PSSampler);
	old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
	ctx->PSGetShader(&old.PS, old.PSInstances, &old.PSInstancesCount);
	ctx->VSGetShader(&old.VS, old.VSInstances, &old.VSInstancesCount);
	ctx->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
	ctx->GSGetShader(&old.GS, old.GSInstances, &old.GSInstancesCount);

	ctx->IAGetPrimitiveTopology(&old.PrimitiveTopology);
	ctx->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
	ctx->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
	ctx->IAGetInputLayout(&old.InputLayout);

	// Setup desired DX state
	dx11_setup_render_state(draw_data, ctx);

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	int global_idx_offset = 0;
	int global_vtx_offset = 0;
	ImVec2 clip_off = draw_data->DisplayPos;
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != nullptr)
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					dx11_setup_render_state(draw_data, ctx);
				else
					pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
				ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				// Apply scissor/clipping rectangle
				const D3D11_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
				ctx->RSSetScissorRects(1, &r);

				// Bind texture, Draw
				ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)pcmd->GetTexID();
				ctx->PSSetShaderResources(0, 1, &texture_srv);
				ctx->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
			}
		}
		global_idx_offset += cmd_list->IdxBuffer.Size;
		global_vtx_offset += cmd_list->VtxBuffer.Size;
	}

	// Restore modified DX state
	ctx->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
	ctx->RSSetViewports(old.ViewportsCount, old.Viewports);
	ctx->RSSetState(old.RS); if (old.RS) old.RS->Release();
	ctx->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
	ctx->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
	ctx->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
	ctx->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
	ctx->PSSetShader(old.PS, old.PSInstances, old.PSInstancesCount); if (old.PS) old.PS->Release();
	for (UINT i = 0; i < old.PSInstancesCount; i++) if (old.PSInstances[i]) old.PSInstances[i]->Release();
	ctx->VSSetShader(old.VS, old.VSInstances, old.VSInstancesCount); if (old.VS) old.VS->Release();
	ctx->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
	ctx->GSSetShader(old.GS, old.GSInstances, old.GSInstancesCount); if (old.GS) old.GS->Release();
	for (UINT i = 0; i < old.VSInstancesCount; i++) if (old.VSInstances[i]) old.VSInstances[i]->Release();
	ctx->IASetPrimitiveTopology(old.PrimitiveTopology);
	ctx->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
	ctx->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
	ctx->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}

