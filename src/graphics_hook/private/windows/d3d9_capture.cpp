#include "graphics_hook.h"
#include "d3d9_patches.hpp"
#include "game_hook.h"
#include "windows_capture.h"
#include "overlay_ui.h"
#include <d3d9.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <Psapi.h>
#include <windows_helper.h>
#include <LoggerHelper.h>
#include <detours.h>
#include <wrl.h>
using namespace Microsoft::WRL;
typedef HRESULT(STDMETHODCALLTYPE* present_t)(IDirect3DDevice9*, CONST RECT*,
	CONST RECT*, HWND,
	CONST RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* present_ex_t)(IDirect3DDevice9Ex*,
	CONST RECT*, CONST RECT*,
	HWND, CONST RGNDATA*, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* present_swap_t)(IDirect3DSwapChain9*,
	CONST RECT*, CONST RECT*,
	HWND, CONST RGNDATA*,
	DWORD);
typedef HRESULT(STDMETHODCALLTYPE* reset_t)(IDirect3DDevice9*,
	D3DPRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* reset_ex_t)(IDirect3DDevice9*,
	D3DPRESENT_PARAMETERS*,
	D3DDISPLAYMODEEX*);

typedef HRESULT(WINAPI* createfactory1_t)(REFIID, void**);

present_t RealPresent = NULL;
present_ex_t RealPresentEx = NULL;
present_swap_t RealPresentSwap = NULL;
reset_t RealReset = NULL;
reset_ex_t RealResetEx = NULL;


struct d3d9_data {
	HMODULE d3d9;
	IDirect3DDevice9* device; /* do not release */
	uint32_t cx;
	uint32_t cy;
	D3DFORMAT d3d9_format;
	DXGI_FORMAT dxgi_format;
	bool using_shtex;

	LPDIRECT3DTEXTURE9          FontTexture;
	LPDIRECT3DVERTEXBUFFER9     pVB;
	LPDIRECT3DINDEXBUFFER9      pIB;
	int                         VertexBufferSize{ 5000 };
	int                         IndexBufferSize{ 10000 };
	/* shared texture */
	IDirect3DSurface9* d3d9_copytex;
	ID3D11Device* d3d11_device;
	ID3D11DeviceContext* d3d11_context;
	ID3D11Resource* d3d11_tex;
	shtex_data_t* shtex_info;
	HANDLE handle;
	int patch;

	/* shared memory */
	IDirect3DSurface9* copy_surfaces[NUM_BUFFERS];
	IDirect3DQuery9* queries[NUM_BUFFERS];
	shmem_data_t* shmem_info;
	bool texture_mapped[NUM_BUFFERS];
	volatile bool issued_queries[NUM_BUFFERS];
	uint32_t pitch;
	int cur_tex;
	int copy_wait;
};

static struct d3d9_data data = {};

///// gui pre declare
static void d3d9_window_update();
static void d3d9_render_draw_data(ImDrawData* draw_data);
static bool d3d9_init_gui(HWND window);
static void d3d9_free_gui();
typedef struct DX9SharedWindowGraphicInfo_t {
	bool bNT_shared;
	HANDLE SharedHandle{ NULL };
	ComPtr<IDirect3DTexture9> CopyTexDX9{ nullptr };
	ComPtr<IDirect3DSurface9> CopySurfaceDX9{ nullptr };
	ComPtr<IDirect3DTexture9> WindowTexDX9{ nullptr };
	ComPtr<IDirect3DSurface9> WindowSurfaceDX9{ nullptr };
	ComPtr<IDirect3DSurface9> TempSurfaceDX9{ nullptr };
	ComPtr<ID3D11Texture2D> WindowTexDX11{ nullptr };
	ComPtr<ID3D11Texture2D> CopyTexDX11{ nullptr };
	ComPtr<ID3D11Texture2D> TestTexDX11{ nullptr };

}DX9SharedWindowGraphicInfo_t;
static std::unordered_map<uint64_t,std::shared_ptr<DX9SharedWindowGraphicInfo_t>>  SharedWindowGraphicInfos;

static void d3d9_free()
{
	if (!data.device) {
		return;
	}
	

	if (data.using_shtex) {
		if (data.d3d11_tex)
			data.d3d11_tex->Release();
		if (data.d3d11_context)
			data.d3d11_context->Release();
		if (data.d3d11_device)
			data.d3d11_device->Release();
		if (data.d3d9_copytex)
			data.d3d9_copytex->Release();
	}
	else {
		for (size_t i = 0; i < NUM_BUFFERS; i++) {
			if (data.copy_surfaces[i]) {
				if (data.texture_mapped[i])
					data.copy_surfaces[i]->UnlockRect();
				data.copy_surfaces[i]->Release();
			}
			if (data.queries[i])
				data.queries[i]->Release();
		}
	}

	if (data.FontTexture) {
		data.FontTexture->Release();
	}
	if (data.pVB) {
		data.pVB->Release();
	}
	if (data.pIB) {
		data.pIB->Release();
	}
	memset(&data, 0, sizeof(data));
	d3d9_free_gui();
	capture_free();

}

static DXGI_FORMAT d3d9_to_dxgi_format(D3DFORMAT format)
{
	switch ((unsigned long)format) {
	case D3DFMT_A2B10G10R10:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case D3DFMT_A8R8G8B8:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case D3DFMT_X8R8G8B8:
		return DXGI_FORMAT_B8G8R8X8_UNORM;
	}

	return DXGI_FORMAT_UNKNOWN;
}

const static D3D_FEATURE_LEVEL feature_levels[] = {
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
};


struct CUSTOMVERTEX
{
	float    pos[3];
	D3DCOLOR col;
	float    uv[2];
};
#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
#define IMGUI_COL_TO_DX9_ARGB(_COL)     (_COL)
#else
#define IMGUI_COL_TO_DX9_ARGB(_COL)     (((_COL) & 0xFF00FF00) | (((_COL) & 0xFF0000) >> 16) | (((_COL) & 0xFF) << 16))
#endif
static bool check_device_format(D3DFORMAT format) {
	IDirect3D9* pd3d = nullptr;
	if (data.device->GetDirect3D(&pd3d) != D3D_OK) {
		return false;
	}
	D3DDEVICE_CREATION_PARAMETERS param = {};
	D3DDISPLAYMODE mode = {};
	if (data.device->GetCreationParameters(&param) != D3D_OK || data.device->GetDisplayMode(0, &mode) != D3D_OK)
	{
		pd3d->Release();
		return false;
	}
	// Font texture should support linear filter, color blend and write to render-target
	bool support = (pd3d->CheckDeviceFormat(param.AdapterOrdinal, param.DeviceType, mode.Format, D3DUSAGE_DYNAMIC | D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, format)) == D3D_OK;
	pd3d->Release();
	return support;
}
static bool create_fonts_texture() {
	ImGuiIO& io = ImGui::GetIO();
	if (data.FontTexture) {
		return true;
	}
	bool result{ false };
	unsigned char* pixels;
	int width, height, bytes_per_pixel;
	get_font_tex_data_RGBA32(&pixels, &width, &height, &bytes_per_pixel);
	bool rgba_support = check_device_format(D3DFMT_A8B8G8R8);
	if (!rgba_support && io.Fonts->TexPixelsUseColors)
	{
		ImU32* dst_start = (ImU32*)malloc((size_t)width * height * bytes_per_pixel);
		for (ImU32* src = (ImU32*)pixels, *dst = dst_start, *dst_end = dst_start + (size_t)width * height; dst < dst_end; src++, dst++)
			*dst = IMGUI_COL_TO_DX9_ARGB(*src);
		pixels = (unsigned char*)dst_start;
	}

	if (data.device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, rgba_support ? D3DFMT_A8B8G8R8 : D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &data.FontTexture, nullptr) < 0)
		goto font_clean;
	D3DLOCKED_RECT tex_locked_rect;
	if (data.FontTexture->LockRect(0, &tex_locked_rect, nullptr, 0) != D3D_OK)
		goto font_clean;
	for (int y = 0; y < height; y++)
		memcpy((unsigned char*)tex_locked_rect.pBits + (size_t)tex_locked_rect.Pitch * y, pixels + (size_t)width * bytes_per_pixel * y, (size_t)width * bytes_per_pixel);
	data.FontTexture->UnlockRect(0);

	// Store our identifier
	set_font_tex((ImTextureID)data.FontTexture);
	result = true;
font_clean:
	if (!rgba_support && io.Fonts->TexPixelsUseColors) {
		free(pixels);
	}
	return result;
}
static bool d3d9_init_gui(HWND new_window) {
	if (main_window != new_window) {
		if (main_window) {
			destroy_overlay_ui();
		}
		init_overlay_ui(new_window);
	}
	set_render_window(new_window);

	create_fonts_texture();
	return true;
}

static void setup_render_state(ImDrawData* draw_data) {
	// Setup viewport
	D3DVIEWPORT9 vp;
	vp.X = vp.Y = 0;
	vp.Width = (DWORD)draw_data->DisplaySize.x;
	vp.Height = (DWORD)draw_data->DisplaySize.y;
	vp.MinZ = 0.0f;
	vp.MaxZ = 1.0f;
	data.device->SetViewport(&vp);

	// Setup render state: fixed-pipeline, alpha-blending, no face culling, no depth testing, shade mode (for gradient), bilinear sampling.
	data.device->SetPixelShader(nullptr);
	data.device->SetVertexShader(nullptr);
	data.device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	data.device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	data.device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	data.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	data.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	data.device->SetRenderState(D3DRS_ZENABLE, FALSE);
	data.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	data.device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	data.device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	data.device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	data.device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
	data.device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
	data.device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	data.device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
	data.device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	data.device->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	data.device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	data.device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	data.device->SetRenderState(D3DRS_CLIPPING, TRUE);
	data.device->SetRenderState(D3DRS_LIGHTING, FALSE);
	data.device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	data.device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	data.device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	data.device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	data.device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	data.device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	data.device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	data.device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	data.device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	data.device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	// Setup orthographic projection matrix
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	// Being agnostic of whether <d3dx9.h> or <DirectXMath.h> can be used, we aren't relying on D3DXMatrixIdentity()/D3DXMatrixOrthoOffCenterLH() or DirectX::XMMatrixIdentity()/DirectX::XMMatrixOrthographicOffCenterLH()
	{
		float L = draw_data->DisplayPos.x + 0.5f;
		float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x + 0.5f;
		float T = draw_data->DisplayPos.y + 0.5f;
		float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y + 0.5f;
		D3DMATRIX mat_identity = { { { 1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f } } };
		D3DMATRIX mat_projection =
		{ { {
			2.0f / (R - L),   0.0f,         0.0f,  0.0f,
			0.0f,         2.0f / (T - B),   0.0f,  0.0f,
			0.0f,         0.0f,         0.5f,  0.0f,
			(L + R) / (L - R),  (T + B) / (B - T),  0.5f,  1.0f
		} } };
		data.device->SetTransform(D3DTS_WORLD, &mat_identity);
		data.device->SetTransform(D3DTS_VIEW, &mat_identity);
		data.device->SetTransform(D3DTS_PROJECTION, &mat_projection);
	}
}
static void d3d9_render_draw_data(ImDrawData* draw_data) {
	// Avoid rendering when minimized
	if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
		return;

	// Create and grow buffers if needed
	//ImGui_ImplDX9_Data* bd = ImGui_ImplDX9_GetBackendData();

	if (!data.pVB || data.VertexBufferSize < draw_data->TotalVtxCount)
	{
		if (data.pVB) { data.pVB->Release(); data.pVB = nullptr; }
		data.VertexBufferSize = draw_data->TotalVtxCount + 5000;
		if (data.device->CreateVertexBuffer(data.VertexBufferSize * sizeof(CUSTOMVERTEX), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT, &data.pVB, nullptr) < 0)
			return;
	}
	if (!data.pIB || data.IndexBufferSize < draw_data->TotalIdxCount)
	{
		if (data.pIB) { data.pIB->Release(); data.pIB = nullptr; }
		data.IndexBufferSize = draw_data->TotalIdxCount + 10000;
		if (data.device->CreateIndexBuffer(data.IndexBufferSize * sizeof(ImDrawIdx), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT, &data.pIB, nullptr) < 0)
			return;
	}

	// Backup the DX9 state
	IDirect3DStateBlock9* d3d9_state_block = nullptr;
	if (data.device->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block) < 0)
		return;
	if (d3d9_state_block->Capture() < 0)
	{
		d3d9_state_block->Release();
		return;
	}

	// Backup the DX9 transform (DX9 documentation suggests that it is included in the StateBlock but it doesn't appear to)
	D3DMATRIX last_world, last_view, last_projection;
	data.device->GetTransform(D3DTS_WORLD, &last_world);
	data.device->GetTransform(D3DTS_VIEW, &last_view);
	data.device->GetTransform(D3DTS_PROJECTION, &last_projection);

	// Allocate buffers
	CUSTOMVERTEX* vtx_dst;
	ImDrawIdx* idx_dst;
	if (data.pVB->Lock(0, (UINT)(draw_data->TotalVtxCount * sizeof(CUSTOMVERTEX)), (void**)&vtx_dst, D3DLOCK_DISCARD) < 0)
	{
		d3d9_state_block->Release();
		return;
	}
	if (data.pIB->Lock(0, (UINT)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)), (void**)&idx_dst, D3DLOCK_DISCARD) < 0)
	{
		data.pVB->Unlock();
		d3d9_state_block->Release();
		return;
	}

	// Copy and convert all vertices into a single contiguous buffer, convert colors to DX9 default format.
	// FIXME-OPT: This is a minor waste of resource, the ideal is to use imconfig.h and
	//  1) to avoid repacking colors:   #define IMGUI_USE_BGRA_PACKED_COLOR
	//  2) to avoid repacking vertices: #define IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT struct ImDrawVert { ImVec2 pos; float z; ImU32 col; ImVec2 uv; }
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
		for (int i = 0; i < cmd_list->VtxBuffer.Size; i++)
		{
			vtx_dst->pos[0] = vtx_src->pos.x;
			vtx_dst->pos[1] = vtx_src->pos.y;
			vtx_dst->pos[2] = 0.0f;
			vtx_dst->col = IMGUI_COL_TO_DX9_ARGB(vtx_src->col);
			vtx_dst->uv[0] = vtx_src->uv.x;
			vtx_dst->uv[1] = vtx_src->uv.y;
			vtx_dst++;
			vtx_src++;
		}
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		idx_dst += cmd_list->IdxBuffer.Size;
	}
	data.pVB->Unlock();
	data.pIB->Unlock();
	data.device->SetStreamSource(0, data.pVB, 0, sizeof(CUSTOMVERTEX));
	data.device->SetIndices(data.pIB);
	data.device->SetFVF(D3DFVF_CUSTOMVERTEX);

	// Setup desired DX state
	setup_render_state(draw_data);

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	int global_vtx_offset = 0;
	int global_idx_offset = 0;
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
					setup_render_state(draw_data);
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

				// Apply Scissor/clipping rectangle, Bind texture, Draw
				const RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
				const LPDIRECT3DTEXTURE9 texture = (LPDIRECT3DTEXTURE9)pcmd->GetTexID();
				data.device->SetTexture(0, texture);
				data.device->SetScissorRect(&r);
				data.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, pcmd->VtxOffset + global_vtx_offset, 0, (UINT)cmd_list->VtxBuffer.Size, pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3);
			}
		}
		global_idx_offset += cmd_list->IdxBuffer.Size;
		global_vtx_offset += cmd_list->VtxBuffer.Size;
	}

	// Restore the DX9 transform
	data.device->SetTransform(D3DTS_WORLD, &last_world);
	data.device->SetTransform(D3DTS_VIEW, &last_view);
	data.device->SetTransform(D3DTS_PROJECTION, &last_projection);

	// Restore the DX9 state
	d3d9_state_block->Apply();
	d3d9_state_block->Release();
}

static inline bool shex_init_d3d11()
{
	PFN_D3D11_CREATE_DEVICE create_device;
	createfactory1_t create_factory;
	D3D_FEATURE_LEVEL level_used;
	IDXGIFactory1* factory;
	IDXGIAdapter1* adapter;
	HMODULE d3d11;
	HMODULE dxgi;
	HRESULT hr;
	
	uint32_t createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	d3d11 = load_system_library("d3d11.dll");
	if (!d3d11) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to load D3D11");
		return false;
	}

	dxgi = load_system_library("dxgi.dll");
	if (!dxgi) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to load DXGI");
		return false;
	}

	create_factory =
		(createfactory1_t)GetProcAddress(dxgi, "CreateDXGIFactory1");
	if (!create_factory) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to get CreateDXGIFactory1 address");
		return false;
	}

	create_device = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(
		d3d11, "D3D11CreateDevice");
	if (!create_device) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to get D3D11CreateDevice address");
		return false;
	}

	hr = create_factory(__uuidof(IDXGIFactory1), (void**)&factory);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to create factory object {}", hr);
		return false;
	}

	hr = factory->EnumAdapters1(0, &adapter);
	factory->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to get adapter {}", hr);
		return false;
	}

	hr = create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, createFlags,
		feature_levels,
		sizeof(feature_levels) / sizeof(D3D_FEATURE_LEVEL),
		D3D11_SDK_VERSION, &data.d3d11_device, &level_used,
		&data.d3d11_context);
	adapter->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init: Failed to create D3D11 device {}", hr);
		return false;
	}

	return true;
}

static inline bool d3d9_shtex_init_shtex()
{
	IDXGIResource* res;
	HRESULT hr;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = data.cx;
	desc.Height = data.cy;
	desc.Format = data.dxgi_format;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = data.d3d11_device->CreateTexture2D(
		&desc, nullptr, (ID3D11Texture2D**)&data.d3d11_tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_init_shtex: Failed to create D3D11 texture {}",
			hr);
		return false;
	}

	hr = data.d3d11_tex->QueryInterface(__uuidof(IDXGIResource),
		(void**)&res);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_init_shtex: Failed to query IDXGIResource {}",
			hr);
		return false;
	}

	hr = res->GetSharedHandle(&data.handle);
	res->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_init_shtex: Failed to get shared handle {}",
			hr);
		return false;
	}

	return true;
}



static uint8_t saved_data[MAX_PATCH_SIZE];
static uint8_t* patch_addr = nullptr;
static size_t patch_size = 0;
static BOOL was_d3d9ex = false;
static BOOL* p_is_d3d9 = nullptr;
static DWORD protect_val;
static inline bool d3d9_patch(){
	if (patch_addr || p_is_d3d9) {
		return true;
	}
	d3d9_offsets_t offsets = global_hook_info->offsets.d3d9;
	if (offsets.d3d9_clsoff && offsets.is_d3d9ex_clsoff) {
		uint8_t* device_ptr = (uint8_t*)(data.device);
		uint8_t* d3d9_ptr =
			*(uint8_t**)(device_ptr + offsets.d3d9_clsoff);
		p_is_d3d9 = (BOOL*)(d3d9_ptr + offsets.is_d3d9ex_clsoff);
	}
	else {
		patch_addr = get_d3d9_patch_addr(data.d3d9, data.patch);
	}

	if (p_is_d3d9) {
		was_d3d9ex = *p_is_d3d9;
		*p_is_d3d9 = true;

	}
	else if (patch_addr) {
		patch_size = patch[data.patch].size;
		VirtualProtect(patch_addr, patch_size, PAGE_EXECUTE_READWRITE,
			&protect_val);
		memcpy(saved_data, patch_addr, patch_size);
		memcpy(patch_addr, patch[data.patch].data, patch_size);
	}
	return true;
}
static inline bool d3d9_unpatch(){
	if (p_is_d3d9) {
		*p_is_d3d9 = was_d3d9ex;
		p_is_d3d9 = nullptr;

	}
	else if (patch_addr && patch_size) {
		memcpy(patch_addr, saved_data, patch_size);
		VirtualProtect(patch_addr, patch_size, protect_val,
			&protect_val);

		patch_addr = nullptr;
	}
	return true;
}
static inline bool d3d9_shtex_init_copytex()
{
	d3d9_offsets_t offsets = global_hook_info->offsets.d3d9;
	uint8_t* patch_addr = nullptr;
	BOOL* p_is_d3d9 = nullptr;
	uint8_t saved_data[MAX_PATCH_SIZE];
	size_t patch_size = 0;
	BOOL was_d3d9ex = false;
	IDirect3DTexture9* tex;
	DWORD protect_val;
	HRESULT hr;

	if (offsets.d3d9_clsoff && offsets.is_d3d9ex_clsoff) {
		uint8_t* device_ptr = (uint8_t*)(data.device);
		uint8_t* d3d9_ptr =
			*(uint8_t**)(device_ptr + offsets.d3d9_clsoff);
		p_is_d3d9 = (BOOL*)(d3d9_ptr + offsets.is_d3d9ex_clsoff);
	}
	else {
		patch_addr = get_d3d9_patch_addr(data.d3d9, data.patch);
	}

	if (p_is_d3d9) {
		was_d3d9ex = *p_is_d3d9;
		*p_is_d3d9 = true;

	}
	else if (patch_addr) {
		patch_size = patch[data.patch].size;
		VirtualProtect(patch_addr, patch_size, PAGE_EXECUTE_READWRITE,
			&protect_val);
		memcpy(saved_data, patch_addr, patch_size);
		memcpy(patch_addr, patch[data.patch].data, patch_size);
	}

	hr = data.device->CreateTexture(data.cx, data.cy, 1,
		D3DUSAGE_RENDERTARGET, data.d3d9_format,
		D3DPOOL_DEFAULT, &tex, &data.handle);

	if (p_is_d3d9) {
		*p_is_d3d9 = was_d3d9ex;

	}
	else if (patch_addr && patch_size) {
		memcpy(patch_addr, saved_data, patch_size);
		VirtualProtect(patch_addr, patch_size, protect_val,
			&protect_val);
	}

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_init_copytex: Failed to create shared texture {}",
			hr);
		return false;
	}

	hr = tex->GetSurfaceLevel(0, &data.d3d9_copytex);
	tex->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_init_copytex: Failed to get surface level {}",
			hr);
		return false;
	}

	return true;
}

static bool d3d9_shtex_init(HWND window)
{
	data.using_shtex = true;

	if (!shex_init_d3d11()) {
		return false;
	}
	if (!d3d9_shtex_init_shtex()) {
		return false;
	}
	if (!d3d9_shtex_init_copytex()) {
		return false;
	}
	if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
		data.dxgi_format, false,
		(uintptr_t)data.handle)) {
		return false;
	}

	SIMPLELOG_LOGGER_INFO(nullptr, "d3d9 shared texture capture successful");
	return true;
}

static bool d3d9_shmem_init_buffers(size_t buffer)
{
	HRESULT hr;

	hr = data.device->CreateOffscreenPlainSurface(
		data.cx, data.cy, data.d3d9_format, D3DPOOL_SYSTEMMEM,
		&data.copy_surfaces[buffer], nullptr);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shmem_init_buffers: Failed to create surface{}",hr);
		return false;
	}

	if (buffer == 0) {
		D3DLOCKED_RECT rect;
		hr = data.copy_surfaces[buffer]->LockRect(&rect, nullptr,
			D3DLOCK_READONLY);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shmem_init_buffers: Failed to lock buffer{}",hr);
			return false;
		}

		data.pitch = rect.Pitch;
		data.copy_surfaces[buffer]->UnlockRect();
	}

	hr = data.device->CreateQuery(D3DQUERYTYPE_EVENT,
		&data.queries[buffer]);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shmem_init_buffers: Failed to create query {}", hr);
		return false;
	}

	return true;
}

static bool d3d9_shmem_init(HWND window)
{
	data.using_shtex = false;

	for (size_t i = 0; i < NUM_BUFFERS; i++) {
		if (!d3d9_shmem_init_buffers(i)) {
			return false;
		}
	}
	if (!capture_init_shmem(&data.shmem_info, window, data.cx, data.cy,
		data.pitch, data.dxgi_format, false)) {
		return false;
	}

	SIMPLELOG_LOGGER_TRACE(nullptr, "d3d9 memory capture successful");
	return true;
}

static bool d3d9_get_swap_desc(D3DPRESENT_PARAMETERS& pp)
{
	IDirect3DSwapChain9* swap = nullptr;
	HRESULT hr;

	hr = data.device->GetSwapChain(0, &swap);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"d3d9_get_swap_desc: Failed to get swap chain {}", hr);
		return false;
	}

	hr = swap->GetPresentParameters(&pp);
	swap->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_get_swap_desc: Failed to get presentation parameters {}",hr);
		return false;
	}

	return true;
}

static bool d3d9_init_format_backbuffer(HWND& window)
{
	IDirect3DSurface9* back_buffer = nullptr;
	D3DPRESENT_PARAMETERS pp;
	D3DSURFACE_DESC desc;
	HRESULT hr;

	if (!d3d9_get_swap_desc(pp)) {
		return false;
	}

	hr = data.device->GetRenderTarget(0, &back_buffer);
	if (FAILED(hr)) {
		return false;
	}

	hr = back_buffer->GetDesc(&desc);
	back_buffer->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_init_format_backbuffer: Failed to get backbuffer descriptor{}",hr);
		return false;
	}

	data.d3d9_format = desc.Format;
	data.dxgi_format = d3d9_to_dxgi_format(desc.Format);
	window = pp.hDeviceWindow;

	data.cx = desc.Width;
	data.cy = desc.Height;

	return true;
}

static bool d3d9_init_format_swapchain(HWND& window)
{
	D3DPRESENT_PARAMETERS pp;

	if (!d3d9_get_swap_desc(pp)) {
		return false;
	}

	data.dxgi_format = d3d9_to_dxgi_format(pp.BackBufferFormat);
	data.d3d9_format = pp.BackBufferFormat;
	window = pp.hDeviceWindow;

	data.cx = pp.BackBufferWidth;
	data.cy = pp.BackBufferHeight;

	return true;
}

static void d3d9_init(IDirect3DDevice9* device)
{
	IDirect3DDevice9Ex* d3d9ex = nullptr;
	bool has_d3d9ex_bool_offset =
		global_hook_info->offsets.d3d9.d3d9_clsoff &&
		global_hook_info->offsets.d3d9.is_d3d9ex_clsoff;
	bool success;
	HWND window = nullptr;
	HRESULT hr;

	data.d3d9 = get_system_module("d3d9.dll");
	data.device = device;

	hr = device->QueryInterface(__uuidof(IDirect3DDevice9Ex),
		(void**)&d3d9ex);
	if (SUCCEEDED(hr)) {
		d3d9ex->Release();
		data.patch = -1;
	}
	else if (!has_d3d9ex_bool_offset) {
		data.patch = get_d3d9_patch(data.d3d9);
	}
	else {
		data.patch = -1;
	}

	if (!d3d9_init_format_backbuffer(window)) {
		if (!d3d9_init_format_swapchain(window)) {
			return;
		}
	}
	d3d9_init_gui(window);

	if (global_hook_info->force_shmem ||
		(!d3d9ex && data.patch == -1 && !has_d3d9ex_bool_offset)) {
		success = d3d9_shmem_init(window);
	}
	else {
		success = d3d9_shtex_init(window);
	}

	if (!success)
		d3d9_free();
}

static void d3d9_remove_window(uint64_t id)
{
	auto itr=SharedWindowGraphicInfos.find(id);
	if (itr== SharedWindowGraphicInfos.end()) {
		return;
	}
	SharedWindowGraphicInfos.erase(id);
}

static void d3d9_window_update()
{
	HRESULT hr;
	std::set<uint64_t> needDel;
	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
		std::inserter(needDel, needDel.begin()),
		[](const std::pair<uint64_t, std::shared_ptr<DX9SharedWindowGraphicInfo_t>>& key_value)
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
				d3d9_remove_window(id);
			}
		}
		auto res = SharedWindowGraphicInfos.emplace(id, std::make_shared<DX9SharedWindowGraphicInfo_t>());
		if (!res.second) {
			continue;
		}
		auto& pinfo = res.first->second;
		pinfo->SharedHandle = sharedHandle;
		pinfo->bNT_shared = windowInfo->Info->bNT_shared;
		if (pinfo->bNT_shared) {
			ComPtr<ID3D11Device1> dev;
			hr = data.d3d11_device->QueryInterface(IID_PPV_ARGS(&dev));
			if (FAILED(hr)) {
				d3d9_remove_window(id);
				continue;
			}
			hr = dev->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d9_remove_window(id);
				continue;
			}

		}
		else {
			hr = data.d3d11_device->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				d3d9_remove_window(id);
				continue;
			}
		}
		
		//D3D11_TEXTURE2D_DESC desc = {};
		//desc.Width = data.cx;
		//desc.Height = data.cy;
		//desc.Format = data.dxgi_format;
		//desc.MipLevels = 1;
		//desc.ArraySize = 1;
		//desc.SampleDesc.Count = 1;
		//desc.Usage = D3D11_USAGE_DEFAULT;
		//desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
		//desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		//hr = data.d3d11_device->CreateTexture2D(&desc, nullptr, &pinfo->CopyTexDX11);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		//ComPtr< IDXGIResource> dgxiRes;
		//hr = pinfo->CopyTexDX11.As(&dgxiRes);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		//HANDLE copyHandle;
		//hr = dgxiRes->GetSharedHandle(&copyHandle);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		
		//test code check data in dx11 texture
		//memset(&desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
		//desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		//desc.Width = data.cx;
		//desc.Height = data.cy;
		//desc.Format = data.dxgi_format;
		//desc.MipLevels = 1;
		//desc.ArraySize = 1;
		//desc.SampleDesc.Count = 1;
		//desc.Usage = D3D11_USAGE_STAGING;
		//hr = data.d3d11_device->CreateTexture2D(&desc, nullptr, &pinfo->TestTexDX11);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}

		HANDLE copyHandle{NULL};
		d3d9_patch();
		hr = data.device->CreateTexture(windowInfo->Info->render_width, windowInfo->Info->render_height, 1,
			D3DUSAGE_RENDERTARGET, data.d3d9_format, D3DPOOL_DEFAULT, &pinfo->CopyTexDX9, &copyHandle);
		d3d9_unpatch();
		if (FAILED(hr)) {
			d3d9_remove_window(id);
			continue;
		}
		hr = pinfo->CopyTexDX9->GetSurfaceLevel(0, &pinfo->CopySurfaceDX9);
		if (FAILED(hr)) {
			d3d9_remove_window(id);
			continue;
		}

		hr = data.d3d11_device->OpenSharedResource(copyHandle, IID_PPV_ARGS(&pinfo->CopyTexDX11));
		if (FAILED(hr)) {
			d3d9_remove_window(id);
			continue;
		}
		windowInfo->WindowTextureID = (intptr_t)pinfo->CopyTexDX9.Get();

		//test code check data in dx9 texture
		//hr = data.device->CreateTexture(desc.Width, desc.Height, 1,
		//	D3DUSAGE_DYNAMIC, data.d3d9_format, D3DPOOL_DEFAULT, &pinfo->WindowTexDX9, NULL);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		//hr = pinfo->WindowTexDX9->GetSurfaceLevel(0, &pinfo->WindowSurfaceDX9);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		//hr = data.device->CreateOffscreenPlainSurface(desc.Width, desc.Height,
		//	data.d3d9_format,D3DPOOL_SYSTEMMEM,&pinfo->TempSurfaceDX9,NULL);
		//if (FAILED(hr)) {
		//	d3d9_remove_window(id);
		//	continue;
		//}
		
	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
	for (auto& pair : SharedWindowGraphicInfos) {
		auto& pSharedWindowGraphicInfo=pair.second;
		ComPtr<IDXGIKeyedMutex>  pDXGIKeyedMutex;
		if (pSharedWindowGraphicInfo->bNT_shared) {
			hr = pSharedWindowGraphicInfo->WindowTexDX11.As(&pDXGIKeyedMutex);
			if (FAILED(hr)) {
				needDel.insert(pair.first);
				continue;
			}
			hr = pDXGIKeyedMutex->AcquireSync(1, 0);
			if (hr != WAIT_OBJECT_0) {
				continue;
			}
		}
		data.d3d11_context->CopyResource(pSharedWindowGraphicInfo->CopyTexDX11.Get(), pSharedWindowGraphicInfo->WindowTexDX11.Get());
		if (pSharedWindowGraphicInfo->bNT_shared) {
			pDXGIKeyedMutex->ReleaseSync(0);
		}
		
		//test code check data in dx9 texture
		//hr = data.device->GetRenderTargetData(pSharedWindowGraphicInfo->CopySurfaceDX9.Get(), pSharedWindowGraphicInfo->TempSurfaceDX9.Get());
		//hr =  data.device->UpdateSurface(pSharedWindowGraphicInfo->TempSurfaceDX9.Get(), NULL, pSharedWindowGraphicInfo->WindowSurfaceDX9.Get(), NULL);
		//D3D11_TEXTURE2D_DESC desc;
		//pSharedWindowGraphicInfo->CopyTexDX11->GetDesc(&desc);
		//D3DLOCKED_RECT outrect;
		//pSharedWindowGraphicInfo->WindowSurfaceDX9->LockRect(&outrect,nullptr, D3DLOCK_READONLY);
		//pSharedWindowGraphicInfo->WindowSurfaceDX9->UnlockRect();
		
		//test code check data in dx11 texture
		//D3D11_MAPPED_SUBRESOURCE map;
		//data.d3d11_context->CopyResource(pSharedWindowGraphicInfo->TestTexDX11.Get(), pSharedWindowGraphicInfo->CopyTexDX11.Get());
		//data.d3d11_context->Map(pSharedWindowGraphicInfo->TestTexDX11.Get(), 0, D3D11_MAP_READ, 0, &map);
		//data.d3d11_context->Unmap(pSharedWindowGraphicInfo->TestTexDX11.Get(), 0);

	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
}
static inline HRESULT get_backbuffer(IDirect3DDevice9* device,
	IDirect3DSurface9** surface)
{
	static bool use_backbuffer = false;
	static bool checked_exceptions = false;

	//if (!checked_exceptions) {
	//	if (_strcmpi(get_process_name(), "hotd_ng.exe") == 0)
	//		use_backbuffer = true;
	//	checked_exceptions = true;
	//}

	if (use_backbuffer) {
		return device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
			surface);
	}
	else {
		return device->GetRenderTarget(0, surface);
	}
}

static inline void d3d9_shtex_capture(IDirect3DSurface9* backbuffer)
{
	HRESULT hr;

	hr = data.device->StretchRect(backbuffer, nullptr, data.d3d9_copytex,
		nullptr, D3DTEXF_NONE);
	if (FAILED(hr))
		SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shtex_capture: StretchRect failed {}", hr);
}

static void d3d9_shmem_capture_copy(int i)
{
	IDirect3DSurface9* target = data.copy_surfaces[i];
	D3DLOCKED_RECT rect;
	HRESULT hr;

	if (!data.issued_queries[i]) {
		return;
	}
	if (data.queries[i]->GetData(0, 0, 0) != S_OK) {
		return;
	}

	data.issued_queries[i] = false;

	hr = target->LockRect(&rect, nullptr, D3DLOCK_READONLY);
	if (SUCCEEDED(hr)) {
		data.texture_mapped[i] = true;
		shmem_copy_data(i, rect.pBits);
	}
}

static inline void d3d9_shmem_capture(IDirect3DSurface9* backbuffer)
{
	int next_tex;
	HRESULT hr;

	next_tex = (data.cur_tex + 1) % NUM_BUFFERS;
	d3d9_shmem_capture_copy(next_tex);

	if (data.copy_wait < NUM_BUFFERS - 1) {
		data.copy_wait++;
	}
	else {
		IDirect3DSurface9* src = backbuffer;
		IDirect3DSurface9* dst = data.copy_surfaces[data.cur_tex];

		if (shmem_texture_data_lock(data.cur_tex)) {
			dst->UnlockRect();
			data.texture_mapped[data.cur_tex] = false;
			shmem_texture_data_unlock(data.cur_tex);
		}

		hr = data.device->GetRenderTargetData(src, dst);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "d3d9_shmem_capture: GetRenderTargetData failed {}",
				hr);
		}

		data.queries[data.cur_tex]->Issue(D3DISSUE_END);
		data.issued_queries[data.cur_tex] = true;
	}

	data.cur_tex = next_tex;
}

static void d3d9_capture(IDirect3DDevice9* device,
	IDirect3DSurface9* backbuffer)
{
	if (capture_should_stop()) {
		d3d9_free();
	}
	if (capture_should_init()) {
		d3d9_init(device);
	}

	if (data.device != device) {
		d3d9_free();
		return;
	}
	//if (is_capture_ready()) {
	//	if (data.using_shtex)
	//		d3d9_shtex_capture(backbuffer);
	//	else
	//		d3d9_shmem_capture(backbuffer);
	//}

	
	if (is_capture_active() ) {
		d3d9_window_update();
		if (is_overlay_active()) {
			overlay_ui_new_frame();
			data.device->BeginScene();
			d3d9_render_draw_data(overlay_ui_render());
			data.device->EndScene();
		}
	}
}


/* this is used just in case Present calls PresentEx or vise versa. */
static int present_recurse = 0;

static inline void present_begin(IDirect3DDevice9* device,
	IDirect3DSurface9*& backbuffer)
{
	HRESULT hr;
	if (!present_recurse) {
		hr = get_backbuffer(device, &backbuffer);
		if (FAILED(hr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"d3d9_shmem_capture: Failed to get backbuffer {}",(int)hr);
		}

		if (!global_hook_info->capture_overlay) {
			d3d9_capture(device, backbuffer);
		}
	}

	present_recurse++;
}

static inline void present_end(IDirect3DDevice9* device,
	IDirect3DSurface9* backbuffer)
{
	present_recurse--;

	if (!present_recurse) {
		if (global_hook_info->capture_overlay) {
			if (!present_recurse)
				d3d9_capture(device, backbuffer);
		}

		if (backbuffer)
			backbuffer->Release();
	}
}

static bool hooked_reset = false;
static void setup_reset_hooks(IDirect3DDevice9* device);

static HRESULT STDMETHODCALLTYPE hook_present(IDirect3DDevice9* device,
	CONST RECT* src_rect,
	CONST RECT* dst_rect,
	HWND override_window,
	CONST RGNDATA* dirty_region)
{
	IDirect3DSurface9* backbuffer = nullptr;

	if (!hooked_reset)
		setup_reset_hooks(device);

	present_begin(device, backbuffer);

	const HRESULT hr = RealPresent(device, src_rect, dst_rect,
		override_window, dirty_region);

	present_end(device, backbuffer);

	return hr;
}

static HRESULT STDMETHODCALLTYPE hook_present_ex(
	IDirect3DDevice9Ex* device, CONST RECT* src_rect, CONST RECT* dst_rect,
	HWND override_window, CONST RGNDATA* dirty_region, DWORD flags)
{
	IDirect3DSurface9* backbuffer = nullptr;

	if (!hooked_reset)
		setup_reset_hooks(device);

	present_begin(device, backbuffer);

	const HRESULT hr = RealPresentEx(device, src_rect, dst_rect,
		override_window, dirty_region, flags);

	present_end(device, backbuffer);

	return hr;
}

static HRESULT STDMETHODCALLTYPE hook_present_swap(
	IDirect3DSwapChain9* swap, CONST RECT* src_rect, CONST RECT* dst_rect,
	HWND override_window, CONST RGNDATA* dirty_region, DWORD flags)
{
	IDirect3DSurface9* backbuffer = nullptr;
	IDirect3DDevice9* device = nullptr;

	if (!present_recurse) {
		IDirect3DDevice9* temp;
		if (SUCCEEDED(swap->GetDevice(&temp))) {
			device = temp;
		}
	}

	if (device) {
		if (!hooked_reset)
			setup_reset_hooks(device);

		present_begin(device, backbuffer);
	}

	const HRESULT hr = RealPresentSwap(
		swap, src_rect, dst_rect, override_window, dirty_region, flags);

	if (device) {
		present_end(device, backbuffer);
		device->Release();
	}

	return hr;
}

static HRESULT STDMETHODCALLTYPE hook_reset(IDirect3DDevice9* device,
	D3DPRESENT_PARAMETERS* params)
{
	if (is_capture_active())
		d3d9_free();

	return RealReset(device, params);
}

static HRESULT STDMETHODCALLTYPE hook_reset_ex(IDirect3DDevice9* device,
	D3DPRESENT_PARAMETERS* params,
	D3DDISPLAYMODEEX* dmex)
{
	if (is_capture_active())
		d3d9_free();

	return RealResetEx(device, params, dmex);
}

static void setup_reset_hooks(IDirect3DDevice9* device)
{
	IDirect3DDevice9Ex* d3d9ex = nullptr;
	uintptr_t* vtable = *(uintptr_t**)device;
	HRESULT hr;

	DetourTransactionBegin();

	RealReset = (reset_t)vtable[16];
	DetourAttach((PVOID*)&RealReset, hook_reset);

	hr = device->QueryInterface(IID_PPV_ARGS(&d3d9ex));
	if (SUCCEEDED(hr)) {
		RealResetEx = (reset_ex_t)vtable[132];
		DetourAttach((PVOID*)&RealResetEx, hook_reset_ex);

		d3d9ex->Release();
	}

	const LONG error = DetourTransactionCommit();
	const bool success = error == NO_ERROR;

	if (success) {
		SIMPLELOG_LOGGER_TRACE(nullptr,"Hooked IDirect3DDevice9::Reset");
		if (RealResetEx)
			SIMPLELOG_LOGGER_TRACE(nullptr, "Hooked IDirect3DDevice9Ex::ResetEx");
		hooked_reset = true;
	}
	else {
		RealReset = nullptr;
		RealResetEx = nullptr;
	}
}

typedef HRESULT(WINAPI* d3d9create_ex_t)(UINT, IDirect3D9Ex**);
static bool manually_get_d3d9_addrs(HMODULE d3d9_module, void** present_addr,
	void** present_ex_addr,
	void** present_swap_addr)
{
	d3d9create_ex_t create_ex;
	D3DPRESENT_PARAMETERS pp;
	HRESULT hr;

	IDirect3DDevice9Ex* device;
	IDirect3D9Ex* d3d9ex;

	SIMPLELOG_LOGGER_TRACE(nullptr,"D3D9 values invalid, manually obtaining");

	create_ex = (d3d9create_ex_t)GetProcAddress(d3d9_module,
		"Direct3DCreate9Ex");
	if (!create_ex) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to load Direct3DCreate9Ex");
		return false;
	}
	if (FAILED(create_ex(D3D_SDK_VERSION, &d3d9ex))) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to create D3D9 context");
		return false;
	}

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = 1;
	pp.SwapEffect = D3DSWAPEFFECT_FLIP;
	pp.BackBufferFormat = D3DFMT_A8R8G8B8;
	pp.BackBufferCount = 1;
	pp.hDeviceWindow = (HWND)dummy_window;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	hr = d3d9ex->CreateDeviceEx(
		D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummy_window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
		&pp, NULL, &device);
	d3d9ex->Release();

	if (SUCCEEDED(hr)) {
		uintptr_t* vtable = *(uintptr_t**)device;
		IDirect3DSwapChain9* swap;

		*present_addr = (void*)vtable[17];
		*present_ex_addr = (void*)vtable[121];

		hr = device->GetSwapChain(0, &swap);
		if (SUCCEEDED(hr)) {
			vtable = *(uintptr_t**)swap;
			*present_swap_addr = (void*)vtable[3];
			swap->Release();
		}

		device->Release();
	}
	else {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to create D3D9 device");
		return false;
	}

	return true;
}

bool hook_d3d9(void)
{
	HMODULE d3d9_module = get_system_module("d3d9.dll");
	uint32_t d3d9_size;
	void* present_addr = nullptr;
	void* present_ex_addr = nullptr;
	void* present_swap_addr = nullptr;

	if (!d3d9_module) {
		return false;
	}

	MODULEINFO info;
	bool success = !!GetModuleInformation(GetCurrentProcess(), d3d9_module,&info, sizeof(info));
	if (!success) {
		return false;
	}
	d3d9_size = info.SizeOfImage;

	if (global_hook_info->offsets.d3d9.present < d3d9_size &&
		global_hook_info->offsets.d3d9.present_ex < d3d9_size &&
		global_hook_info->offsets.d3d9.present_swap < d3d9_size) {

		present_addr = get_offset_addr(
			d3d9_module, global_hook_info->offsets.d3d9.present);
		present_ex_addr = get_offset_addr(
			d3d9_module, global_hook_info->offsets.d3d9.present_ex);
		present_swap_addr = get_offset_addr(
			d3d9_module,
			global_hook_info->offsets.d3d9.present_swap);
	}
	else {
		if (!dummy_window) {
			return false;
		}

		if (!manually_get_d3d9_addrs(d3d9_module, &present_addr,
			&present_ex_addr,
			&present_swap_addr)) {
			SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to get D3D9 values");
			return true;
		}
	}

	if (!present_addr && !present_ex_addr && !present_swap_addr) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Invalid D3D9 values");
		return true;
	}

	DetourTransactionBegin();

	if (present_swap_addr) {
		RealPresentSwap = (present_swap_t)present_swap_addr;
		DetourAttach((PVOID*)&RealPresentSwap, hook_present_swap);
	}
	if (present_ex_addr) {
		RealPresentEx = (present_ex_t)present_ex_addr;
		DetourAttach((PVOID*)&RealPresentEx, hook_present_ex);
	}
	if (present_addr) {
		RealPresent = (present_t)present_addr;
		DetourAttach((PVOID*)&RealPresent, hook_present);
	}

	const LONG error = DetourTransactionCommit();
	success = error == NO_ERROR;
	if (success) {
		if (RealPresentSwap)
			SIMPLELOG_LOGGER_TRACE(nullptr,"Hooked IDirect3DSwapChain9::Present");
		if (RealPresentEx)
			SIMPLELOG_LOGGER_TRACE(nullptr, "Hooked IDirect3DDevice9Ex::PresentEx");
		if (RealPresent)
			SIMPLELOG_LOGGER_TRACE(nullptr, "Hooked IDirect3DDevice9::Present");
		SIMPLELOG_LOGGER_TRACE(nullptr, "Hooked D3D9");
	}
	else {
		RealPresentSwap = nullptr;
		RealPresentEx = nullptr;
		RealPresent = nullptr;
		SIMPLELOG_LOGGER_TRACE(nullptr, "Failed to attach Detours hook: {}", error);
	}

	return success;
}




void d3d9_free_gui() {
	SharedWindowGraphicInfos.clear();
}