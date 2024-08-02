#include "graphics_hook.h"
#include "overlay_ui.h"	
#include "gui_shader.h"
#include "windows_capture.h"
#if COMPILE_D3D12_HOOK
#include <windows_helper.h>
#include <d3d11on12.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <inttypes.h>
#include <detours.h>

#include "dxgi-helpers.hpp"
#include <wrl.h>
using namespace Microsoft::WRL;

#define MAX_BACKBUFFERS 8

typedef HRESULT(STDMETHODCALLTYPE *PFN_ExecuteCommandLists)(
	ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

static PFN_ExecuteCommandLists RealExecuteCommandLists = nullptr;

typedef struct d3d12_data_t {
	uint32_t cx;
	uint32_t cy;
	DXGI_FORMAT format;
	bool using_shtex;
	bool multisampled;
	bool dxgi_1_4;

	ID3D12Device* device;
	ID3D11Device *device11;
	ID3D11DeviceContext *context11;
	ID3D11On12Device *device11on12;

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
		struct {
			shtex_data_t *shtex_info;
			ID3D11Resource *backbuffer11[MAX_BACKBUFFERS];
			ID3D11RenderTargetView* backbufferRTV[MAX_BACKBUFFERS];
			UINT backbuffer_count;
			UINT cur_backbuffer;
			ID3D11Texture2D *copy_tex;
			HANDLE handle;
		};
	};
}d3d12_data_t;
static d3d12_data_t data = {};

///// gui pre declare
static void d3d12_window_update();
static void d3d12_render_draw_data(IDXGISwapChain* swap,ImDrawData* draw_data);
static bool d3d12_init_gui(HWND window);
static void d3d12_free_gui();
static int const                    NUM_FRAMES_IN_FLIGHT = 3;


extern thread_local int dxgi_presenting;
extern ID3D12CommandQueue *dxgi_possible_swap_queues[8];
extern size_t dxgi_possible_swap_queue_count;
extern bool dxgi_present_attempted;

void d3d12_free(void)
{
	if (data.copy_tex)
		data.copy_tex->Release();
	for (size_t i = 0; i < data.backbuffer_count; i++) {
		if (data.backbuffer11[i])
			data.backbuffer11[i]->Release();
		if (data.backbufferRTV[i])
			data.backbufferRTV[i]->Release();
	}
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

	if (data.device11)
		data.device11->Release();
	if (data.context11)
		data.context11->Release();
	if (data.device11on12)
		data.device11on12->Release();

	memset(&data, 0, sizeof(data));

	d3d12_free_gui();
	capture_free();

	SIMPLELOG_LOGGER_TRACE(nullptr,"----------------- d3d12 capture freed ----------------");
}

struct bb_info {
	ID3D12Resource *backbuffer[MAX_BACKBUFFERS];
	UINT count;
};

static bool create_d3d12_tex(bb_info &bb)
{
	D3D11_RESOURCE_FLAGS rf11 = { D3D11_BIND_RENDER_TARGET };
	HRESULT hr;

	if (!bb.count)
		return false;

	data.backbuffer_count = bb.count;

	for (UINT i = 0; i < bb.count; i++) {
		hr = data.device11on12->CreateWrappedResource(
			bb.backbuffer[i], &rf11, D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_PRESENT | D3D12_RESOURCE_STATE_RENDER_TARGET,
			IID_PPV_ARGS(&data.backbuffer11[i]));
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d12_tex: failed to create backbuffer11 {}",
				hr);
			return false;
		}
		hr = data.device11->CreateRenderTargetView(data.backbuffer11[i], NULL, &data.backbufferRTV[i]);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "create_d3d12_tex: failed to create backbufferRTV {}",
				hr);
			return false;
		}
	}

	D3D11_TEXTURE2D_DESC desc11 = {};
	desc11.Width = data.cx;
	desc11.Height = data.cy;
	desc11.MipLevels = 1;
	desc11.ArraySize = 1;
	desc11.Format = apply_dxgi_format_typeless(
		data.format, global_hook_info->allow_srgb_alias);
	desc11.SampleDesc.Count = 1;
	desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	hr = data.device11->CreateTexture2D(&desc11, nullptr, &data.copy_tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d12_tex: creation of d3d11 copy tex failed {}",
			hr);
		return false;
	}

	IDXGIResource *dxgi_res;
	hr = data.copy_tex->QueryInterface(&dxgi_res);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d12_tex: failed to query IDXGIResource interface from texture {}",
			hr);
		return false;
	}

	hr = dxgi_res->GetSharedHandle(&data.handle);
	dxgi_res->Release();
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"create_d3d12_tex: failed to get shared handle", hr);
		return false;
	}

	return true;
}

static bool d3d12_init_11on12(ID3D12Device *device)
{
	static HMODULE d3d11 = nullptr;
	static PFN_D3D11ON12_CREATE_DEVICE create_11_on_12 = nullptr;
	static bool initialized_11 = false;
	static bool initialized_func = false;

	if (!initialized_11 && !d3d11) {
		d3d11 = load_system_library("d3d11.dll");
		if (!d3d11) {
			SIMPLELOG_LOGGER_TRACE(nullptr,"d3d12_init_11on12: failed to load d3d11");
		}
		initialized_11 = true;
	}

	if (!d3d11) {
		return false;
	}

	if (!initialized_func && !create_11_on_12) {
		create_11_on_12 = (PFN_D3D11ON12_CREATE_DEVICE)GetProcAddress(
			d3d11, "D3D11On12CreateDevice");
		if (!create_11_on_12) {
			SIMPLELOG_LOGGER_TRACE(nullptr,"d3d12_init_11on12: Failed to get "
			     "D3D11On12CreateDevice address");
		}

		initialized_func = true;
	}

	if (!create_11_on_12) {
		return false;
	}

	bool created = false;

	for (size_t i = 0; i < dxgi_possible_swap_queue_count; ++i) {
		SIMPLELOG_LOGGER_DEBUG(nullptr,"d3d12_init_11on12: creating 11 device: queue={}",
		     (uint64_t)(uintptr_t)dxgi_possible_swap_queues[i]);
		auto desc=dxgi_possible_swap_queues[i]->GetDesc();
		if (desc.Type != D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT) {
			continue;
		}
		IUnknown *const queue = dxgi_possible_swap_queues[i];
		const HRESULT hr = create_11_on_12(device, 0, nullptr, 0,
						   &queue, 1, 0, &data.device11,
						   &data.context11, nullptr);
		created = SUCCEEDED(hr);
		if (created) {
			break;
		}

		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d12_init_11on12: failed to create 11 device {}", hr);
	}

	if (!created) {
		return false;
	}

	memset(dxgi_possible_swap_queues, 0, sizeof(dxgi_possible_swap_queues));
	dxgi_possible_swap_queue_count = 0;
	dxgi_present_attempted = false;

	const HRESULT hr =
		data.device11->QueryInterface(IID_PPV_ARGS(&data.device11on12));
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d12_init_11on12: failed to query 11on12 device {}", hr);
		return false;
	}

	return true;
}

static bool d3d12_shtex_init(ID3D12Device *device, HWND window, bb_info &bb)
{
	if (!d3d12_init_11on12(device)) {
		return false;
	}
	if (!create_d3d12_tex(bb)) {
		return false;
	}
	if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
				data.format, false, (uintptr_t)data.handle)) {
		return false;
	}

	SIMPLELOG_LOGGER_TRACE(nullptr,"d3d12 shared texture capture successful");
	return true;
}

static inline bool d3d12_init_format(IDXGISwapChain *swap, HWND &window,
				     bb_info &bb)
{
	DXGI_SWAP_CHAIN_DESC desc;
	IDXGISwapChain3 *swap3;
	HRESULT hr;

	hr = swap->GetDesc(&desc);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d12_init_format: swap->GetDesc failed", hr);
		return false;
	}

	print_swap_desc(&desc);

	data.format = strip_dxgi_format_srgb(desc.BufferDesc.Format);
	data.multisampled = desc.SampleDesc.Count > 1;
	window = desc.OutputWindow;
	data.cx = desc.BufferDesc.Width;
	data.cy = desc.BufferDesc.Height;

	hr = swap->QueryInterface(&swap3);
	if (SUCCEEDED(hr)) {
		data.dxgi_1_4 = true;
		SIMPLELOG_LOGGER_TRACE(nullptr,"We're DXGI1.4 boys!");
		swap3->Release();
	}

	bb.count = desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD
			   ? 1
			   : desc.BufferCount;

	if (bb.count == 1)
		data.dxgi_1_4 = false;

	if (bb.count > MAX_BACKBUFFERS) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Somehow it's using more than the max backbuffers.  "
		     "Not sure why anyone would do that.");
		bb.count = 1;
		data.dxgi_1_4 = false;
	}

	for (UINT i = 0; i < bb.count; i++) {
		hr = swap->GetBuffer(i, IID_PPV_ARGS(&bb.backbuffer[i]));
		if (SUCCEEDED(hr)) {
			bb.backbuffer[i]->Release();
		} else {
			return false;
		}
	}

	return true;
}

static void d3d12_init(IDXGISwapChain *swap)
{
	const HRESULT hr = swap->GetDevice(IID_PPV_ARGS(&data.device));
	if (SUCCEEDED(hr)) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"d3d12_init: device={}",
		     (uint64_t)(uintptr_t)data.device);

		HWND window;
		bb_info bb = {};
		if (d3d12_init_format(swap, window, bb)) {
			if (global_hook_info->force_shmem) {
				SIMPLELOG_LOGGER_TRACE(nullptr,"d3d12_init: shared memory capture currently "
				     "unsupported; ignoring");
			}

			if (!d3d12_shtex_init(data.device, window, bb)) {
				d3d12_free();
				return;
			}

			d3d12_init_gui(window);
		}

		data.device->Release();
	} else {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d12_init: failed to get device from swap {}", hr);
	}
}

static inline void d3d12_copy_texture(ID3D11Resource *dst, ID3D11Resource *src)
{
	if (data.multisampled) {
		data.context11->ResolveSubresource(dst, 0, src, 0, data.format);
	} else {
		data.context11->CopyResource(dst, src);
	}
}

static inline void d3d12_shtex_capture(IDXGISwapChain *swap)
{
	bool dxgi_1_4 = data.dxgi_1_4;
	UINT cur_idx;

	if (dxgi_1_4) {
		IDXGISwapChain3 *swap3 =
			reinterpret_cast<IDXGISwapChain3 *>(swap);
		cur_idx = swap3->GetCurrentBackBufferIndex();
	} else {
		cur_idx = data.cur_backbuffer;
	}

	ID3D11Resource *backbuffer = data.backbuffer11[cur_idx];

	data.device11on12->AcquireWrappedResources(&backbuffer, 1);
	d3d12_copy_texture(data.copy_tex, backbuffer);
	data.device11on12->ReleaseWrappedResources(&backbuffer, 1);
	data.context11->Flush();

	if (!dxgi_1_4) {
		if (++data.cur_backbuffer >= data.backbuffer_count)
			data.cur_backbuffer = 0;
	}
}

void d3d12_capture(void *swap_ptr, void *)
{
	IDXGISwapChain *swap = (IDXGISwapChain *)swap_ptr;

	if (capture_should_stop()) {
		d3d12_free();
	}
	if (capture_should_init()) {
		d3d12_init(swap);
	}
	//if (is_capture_ready()) {
	//	d3d12_shtex_capture(swap);
	//}

	if (is_capture_active()) {
		d3d12_window_update();
		if (is_overlay_active()) {
			overlay_ui_new_frame();
			d3d12_render_draw_data(swap,overlay_ui_render());
		}
	}
}

static bool try_append_queue_if_unique(ID3D12CommandQueue *queue)
{
	for (size_t i = 0; i < dxgi_possible_swap_queue_count; ++i) {
		if (dxgi_possible_swap_queues[i] == queue)
			return false;
	}

	dxgi_possible_swap_queues[dxgi_possible_swap_queue_count] = queue;
	++dxgi_possible_swap_queue_count;
	return true;
}

static HRESULT STDMETHODCALLTYPE
hook_execute_command_lists(ID3D12CommandQueue *queue, UINT NumCommandLists,
			   ID3D12CommandList *const *ppCommandLists)
{
	SIMPLELOG_LOGGER_TRACE(nullptr,"ExecuteCommandLists callback: queue={}",
		     (uint64_t)(uintptr_t)queue);

	if (dxgi_possible_swap_queue_count <
	    _countof(dxgi_possible_swap_queues)) {
		if ((dxgi_presenting > 0) &&
		    (queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)) {
			if (try_append_queue_if_unique(queue)) {
				SIMPLELOG_LOGGER_TRACE(nullptr,"Remembering D3D12 queue from present: queue={}",
				     (uint64_t)(uintptr_t)queue);
			}
		} else if (dxgi_present_attempted &&
			   (queue->GetDesc().Type ==
			    D3D12_COMMAND_LIST_TYPE_DIRECT)) {
			if (try_append_queue_if_unique(queue)) {
				SIMPLELOG_LOGGER_TRACE(nullptr,"Remembering D3D12 queue from first direct submit after present: queue={}",
				     (uint64_t)(uintptr_t)queue);
			}
		} else {
			SIMPLELOG_LOGGER_TRACE(nullptr,"Ignoring D3D12 queue={}",
				     (uint64_t)(uintptr_t)queue);
		}
	}

	return RealExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

static bool
manually_get_d3d12_addrs(HMODULE d3d12_module,
			 PFN_ExecuteCommandLists *execute_command_lists_addr)
{
	PFN_D3D12_CREATE_DEVICE create =
		(PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12_module,
							"D3D12CreateDevice");
	if (!create) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Failed to load D3D12CreateDevice");
		return false;
	}

	bool success = false;
	ID3D12Device *device;
	if (SUCCEEDED(create(NULL, D3D_FEATURE_LEVEL_11_0,
			     IID_PPV_ARGS(&device)))) {
		D3D12_COMMAND_QUEUE_DESC desc{};
		ID3D12CommandQueue *queue;
		HRESULT hr =
			device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));
		success = SUCCEEDED(hr);
		if (success) {
			void **queue_vtable = *(void ***)queue;
			*execute_command_lists_addr =
				(PFN_ExecuteCommandLists)queue_vtable[10];

			queue->Release();
		} else {
			SIMPLELOG_LOGGER_TRACE(nullptr,"Failed to create D3D12 command queue");
		}

		device->Release();
	} else {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Failed to create D3D12 device");
	}

	return success;
}

bool hook_d3d12(void)
{
	HMODULE d3d12_module = get_system_module("d3d12.dll");
	if (!d3d12_module) {
		SIMPLELOG_LOGGER_TRACE(nullptr,
			"Failed to find d3d12.dll. Skipping hook attempt.");
		return false;
	}

	PFN_ExecuteCommandLists execute_command_lists_addr = nullptr;
	if (!manually_get_d3d12_addrs(d3d12_module,
				      &execute_command_lists_addr)) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Failed to get D3D12 values");
		return true;
	}

	if (!execute_command_lists_addr) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Invalid D3D12 values");
		return true;
	}

	DetourTransactionBegin();

	RealExecuteCommandLists = execute_command_lists_addr;
	DetourAttach(&(PVOID &)RealExecuteCommandLists,
		     hook_execute_command_lists);

	const LONG error = DetourTransactionCommit();
	const bool success = error == NO_ERROR;
	if (success) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Hooked ID3D12CommandQueue::ExecuteCommandLists");
		SIMPLELOG_LOGGER_TRACE(nullptr,"Hooked D3D12");
	} else {
		RealExecuteCommandLists = nullptr;
		SIMPLELOG_LOGGER_TRACE(nullptr,"Failed to attach Detours hook: %ld", error);
	}

	return success;
}

#endif


/// gui code

struct VERTEX_CONSTANT_BUFFER_DX12
{
	float   mvp[4][4];
};

static bool d3d12_create_font_texture() {
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
		data.device11->CreateTexture2D(&desc, &subResource, &pTexture);
		IM_ASSERT(pTexture != nullptr);

		// Create texture view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		data.device11->CreateShaderResourceView(pTexture, &srvDesc, &data.pFontTextureView);
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
		data.device11->CreateSamplerState(&desc, &data.pFontSampler);
	}
	return true;
}
static bool d3d12_init_gui(HWND window) {
	if (!data.device11) {
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
		//if (data.device11->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &data.pVertexShader) != S_OK)
		//{
		//	vertexShaderBlob->Release();
		//	return false;
		//}

		if (data.device11->CreateVertexShader(g_preCompiled_pVertexShaderBlob, sizeof(g_preCompiled_pVertexShaderBlob), nullptr, &data.pVertexShader) != S_OK)
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

		//if (data.device11->CreateInputLayout(local_layout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &data.pInputLayout) != S_OK)
		//{
		//	vertexShaderBlob->Release();
		//	return false;
		//}
		//vertexShaderBlob->Release();

		if (data.device11->CreateInputLayout(local_layout, 3, g_preCompiled_pVertexShaderBlob, sizeof(g_preCompiled_pVertexShaderBlob), &data.pInputLayout) != S_OK)
		{
			return false;
		}

		// Create the constant buffer
		{
			D3D11_BUFFER_DESC desc;
			desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX12);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			data.device11->CreateBuffer(&desc, nullptr, &data.pVertexConstantBuffer);
		}
	}

	// Create the pixel shader
	{
		//ID3DBlob* pixelShaderBlob;
		//if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
		//	return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
		//if (data.device11->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &data.pPixelShader) != S_OK)
		//{
		//	pixelShaderBlob->Release();
		//	return false;
		//}
		//pixelShaderBlob->Release();
		if (data.device11->CreatePixelShader(g_preCompiled_pPixelShaderBlob, sizeof(g_preCompiled_pPixelShaderBlob), nullptr, &data.pPixelShader) != S_OK)
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
		data.device11->CreateBlendState(&desc, &data.pBlendState);
	}

	// Create the rasterizer state
	{
		D3D11_RASTERIZER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.ScissorEnable = true;
		desc.DepthClipEnable = true;
		data.device11->CreateRasterizerState(&desc, &data.pRasterizerState);
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
		data.device11->CreateDepthStencilState(&desc, &data.pDepthStencilState);
	}

	d3d12_create_font_texture();

	return true;
}

typedef struct DX12SharedWindowGraphicInfo_t {

	HANDLE SharedHandle{ NULL };
	ComPtr<ID3D11Texture2D> WindowTexDX11{ nullptr };
	ComPtr<ID3D11Texture2D> CopyTexDX11{ nullptr };
	ComPtr<ID3D11ShaderResourceView> CopySRVDX11{ nullptr };
	ComPtr<ID3D11Texture2D> TestTexDX11{ nullptr };
}DX12SharedWindowGraphicInfo_t;
static std::unordered_map<uint64_t, std::shared_ptr<DX12SharedWindowGraphicInfo_t>>  SharedWindowGraphicInfos;

static void d3d12_remove_window(uint64_t id)
{
	auto itr = SharedWindowGraphicInfos.find(id);
	if (itr == SharedWindowGraphicInfos.end()) {
		return;
	}
	SharedWindowGraphicInfos.erase(id);
}
static void d3d12_window_update() {
	HRESULT hr;
	std::set<uint64_t> needDel;
	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
		std::inserter(needDel, needDel.begin()),
		[](const std::pair<uint64_t, std::shared_ptr<DX12SharedWindowGraphicInfo_t>>& key_value)
		{ return key_value.first; });

	for (auto& windowInfo : SharedWindowInfos) {
		auto& id = windowInfo->Id;
		HANDLE sharedHandle = (HANDLE)windowInfo->Info->shared_handle;
		needDel.erase(id);
		if (SharedWindowGraphicInfos.contains(id)) {
			if (SharedWindowGraphicInfos[id]->SharedHandle == sharedHandle) {
				continue;
			}
			else {
				d3d12_remove_window(id);
			}
		}
		auto res = SharedWindowGraphicInfos.emplace(id, std::make_shared<DX12SharedWindowGraphicInfo_t>());
		if (!res.second) {
			continue;
		}
		auto& pinfo = res.first->second;
		pinfo->SharedHandle = sharedHandle;
		if (windowInfo->Info->bNT_shared) {
			ComPtr<ID3D11Device1> dev;
			hr = data.device11->QueryInterface(IID_PPV_ARGS(&dev));
			if (FAILED(hr)) {
				d3d12_remove_window(id);
				continue;
			}
			hr = dev->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d12_remove_window(id);
				continue;
			}
		}
		else {
			hr = data.device11->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d12_remove_window(id);
				continue;
			}
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = windowInfo->Info->render_width;
		desc.Height = windowInfo->Info->render_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = apply_dxgi_format_typeless(
			data.format, global_hook_info->allow_srgb_alias);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;

		hr = data.device11->CreateTexture2D(&desc, NULL, &pinfo->CopyTexDX11);
		if (FAILED(hr)) {
			d3d12_remove_window(id);
			continue;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = data.format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		hr = data.device11->CreateShaderResourceView(pinfo->CopyTexDX11.Get(), &srvDesc, &pinfo->CopySRVDX11);
		if (FAILED(hr)) {
			d3d12_remove_window(id);
			continue;
		}

		windowInfo->WindowTextureID = (intptr_t)pinfo->CopySRVDX11.Get();
	}
	for (auto& id : needDel) {
		d3d12_remove_window(id);
	}
	for (auto& pair : SharedWindowGraphicInfos) {
		auto& pSharedWindowGraphicInfo = pair.second;
		ComPtr<IDXGIKeyedMutex>  pDXGIKeyedMutex;
		hr = pSharedWindowGraphicInfo->WindowTexDX11->QueryInterface(IID_PPV_ARGS(&pDXGIKeyedMutex));
		if (FAILED(hr)) {
			needDel.insert(pair.first);
			continue;
		}
		hr = pDXGIKeyedMutex->AcquireSync(1, 0);
		if (hr != WAIT_OBJECT_0) {
			continue;
		}
		data.context11->CopyResource(pSharedWindowGraphicInfo->CopyTexDX11.Get(), pSharedWindowGraphicInfo->WindowTexDX11.Get());
		pDXGIKeyedMutex->ReleaseSync(0);
	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
}



static void d3d12_setup_render_state(ImDrawData* draw_data, ID3D11DeviceContext* ctx) {
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
	ctx->OMSetRenderTargets(1, &data.backbufferRTV[data.cur_backbuffer], nullptr);
}

static void d3d12_render_draw_data(IDXGISwapChain* swap, ImDrawData* draw_data) {

	bool dxgi_1_4 = data.dxgi_1_4;
	UINT cur_idx;
	if (dxgi_1_4) {
		IDXGISwapChain3* swap3 =
			reinterpret_cast<IDXGISwapChain3*>(swap);
		cur_idx = swap3->GetCurrentBackBufferIndex();
		data.cur_backbuffer = cur_idx;
	}
	else {
		cur_idx = data.cur_backbuffer;
	}
	ID3D11Resource* backbuffer = data.backbuffer11[cur_idx];
	data.device11on12->AcquireWrappedResources(&backbuffer, 1);


	// Avoid rendering when minimized
	if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
		return;

	ID3D11DeviceContext* ctx = data.context11;

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
		if (data.device11->CreateBuffer(&desc, nullptr, &data.pVB) < 0)
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
		if (data.device11->CreateBuffer(&desc, nullptr, &data.pIB) < 0)
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
		VERTEX_CONSTANT_BUFFER_DX12* constant_buffer = (VERTEX_CONSTANT_BUFFER_DX12*)mapped_resource.pData;
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
		ID3D11RenderTargetView* RenderTarget;
		ID3D11DepthStencilView* DepthStencil;
	};
	BACKUP_DX11_STATE old = {};
	old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	ctx->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
	ctx->RSGetViewports(&old.ViewportsCount, old.Viewports);
	ctx->RSGetState(&old.RS);
	ctx->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
	ctx->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
	ctx->OMGetRenderTargets(1, &old.RenderTarget, &old.DepthStencil);
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
	d3d12_setup_render_state(draw_data, ctx);

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
					d3d12_setup_render_state(draw_data, ctx);
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
	ctx->OMSetRenderTargets(1,&old.RenderTarget, old.DepthStencil); if (old.RenderTarget) old.RenderTarget->Release(); if (old.DepthStencil) old.DepthStencil->Release();
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

	data.device11on12->ReleaseWrappedResources(&backbuffer, 1);
	data.context11->Flush();

	if (!dxgi_1_4) {
		if (++data.cur_backbuffer >= data.backbuffer_count)
			data.cur_backbuffer = 0;
	}
}

static void d3d12_free_gui() {
	SharedWindowGraphicInfos.clear();
}