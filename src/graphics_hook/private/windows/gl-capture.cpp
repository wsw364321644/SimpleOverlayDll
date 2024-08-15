#ifdef _MSC_VER
#pragma warning(disable : 4214) /* nonstandard extension, non-int bitfield */
#pragma warning(disable : 4054) /* function pointer to data pointer */
#endif
#define COBJMACROS

#include "gl-decs.h"
#include "graphics_hook.h"
#include "overlay_ui.h"
#include "dxgi-helpers.hpp"
#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <windows_helper.h>
#include <LoggerHelper.h>
#include <detours.h>
#include <wrl.h>
using namespace Microsoft::WRL;
#define DUMMY_WINDOW_CLASS_NAME L"graphics_hook_gl_dummy_window"

/* clang-format off */

static const GUID GUID_IDXGIFactory1 =
{0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
static const GUID GUID_IDXGIResource =
{0x035f3ab4, 0x482e, 0x4e50, {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};

/* clang-format on */

typedef BOOL(WINAPI *PFN_SwapBuffers)(HDC);
typedef BOOL(WINAPI *PFN_WglSwapLayerBuffers)(HDC, UINT);
typedef BOOL(WINAPI *PFN_WglSwapBuffers)(HDC);
typedef BOOL(WINAPI *PFN_WglDeleteContext)(HGLRC);

PFN_SwapBuffers RealSwapBuffers = NULL;
PFN_WglSwapLayerBuffers RealWglSwapLayerBuffers = NULL;
PFN_WglSwapBuffers RealWglSwapBuffers = NULL;
PFN_WglDeleteContext RealWglDeleteContext = NULL;

static bool darkest_dungeon_fix = false;
static bool functions_initialized = false;


///// gui pre declare
static void gl_window_update();
static void gl_render_draw_data(ImDrawData* draw_data);
static bool gl_init_gui();
static void gl_free_gui();

struct gl_data {
	HDC hdc;
	uint32_t cx;
	uint32_t cy;
	DXGI_FORMAT format;
	GLuint fbo;
	bool using_shtex;
	bool shmem_fallback;


	GLuint          GlVersion;
	GLuint          GlslVersion;
	GLuint          FontTexture;
	GLuint          ShaderHandle;
	GLint           AttribLocationTex;       // Uniforms location
	GLint           AttribLocationProjMtx;
	GLuint          AttribLocationVtxPos;    // Vertex attributes location
	GLuint          AttribLocationVtxUV;
	GLuint          AttribLocationVtxColor;
	unsigned int    VboHandle, ElementsHandle;
	GLsizeiptr      VertexBufferSize;
	GLsizeiptr      IndexBufferSize;
	GLint           GlProfileMask{ 0 };
	bool            IsES2{ false };
	bool            IsES3{ false };
	bool            HasPolygonMode{ false };
	bool            HasClipOrigin{ false };
	bool            IsCompat{ false };
	bool            UseBufferSubData{ false };
	union {
		/* shared texture */
		struct {
			struct shtex_data_t *shtex_info;
			ID3D11Device *d3d11_device;
			ID3D11DeviceContext *d3d11_context;
			ID3D11Texture2D *d3d11_tex;
			IDXGISwapChain *dxgi_swap;
			HANDLE gl_device;
			HANDLE gl_dxobj;
			HANDLE handle;
			HWND hwnd;
			GLuint texture;
		};
		/* shared memory */
		struct {
			struct shmem_data_t *shmem_info;
			int cur_tex;
			int copy_wait;
			GLuint pbos[NUM_BUFFERS];
			GLuint textures[NUM_BUFFERS];
			bool texture_ready[NUM_BUFFERS];
			bool texture_mapped[NUM_BUFFERS];
		};
	};
};

static HMODULE gl = NULL;
static bool nv_capture_available = false;
static struct gl_data data = {0};
__declspec(thread) static int swap_recurse;

static inline bool gl_error(const char *func, const char *str)
{
	GLenum error = glGetError();
	if (error != 0) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"{}: {}: {}", func, str, error);
		return true;
	}

	return false;
}

static void gl_free(void)
{
	capture_free();

	gl_free_gui();

	if (data.ShaderHandle) {
		glDeleteProgram(data.ShaderHandle);
	}
	if (data.VboHandle) {
		glDeleteBuffers(1,&data.VboHandle);
	}
	if (data.ElementsHandle) {
		glDeleteBuffers(1, &data.ElementsHandle);
	}
	if (data.FontTexture) {
		glDeleteTextures(1,&data.FontTexture);
	}

	if (data.using_shtex) {
		if (data.gl_dxobj)
			jimglDXUnregisterObjectNV(data.gl_device,
						  data.gl_dxobj);
		if (data.gl_device)
			jimglDXCloseDeviceNV(data.gl_device);
		if (data.texture)
			glDeleteTextures(1, &data.texture);
		if (data.d3d11_tex)
			data.d3d11_tex->Release();
		if (data.d3d11_context)
			data.d3d11_context->Release();
		if (data.d3d11_device)
			data.d3d11_device->Release();
		if (data.dxgi_swap)
			data.dxgi_swap->Release();
		if (data.hwnd)
			DestroyWindow(data.hwnd);
	} else {
		for (size_t i = 0; i < NUM_BUFFERS; i++) {
			if (data.pbos[i]) {
				if (data.texture_mapped[i]) {
					glBindBuffer(GL_PIXEL_PACK_BUFFER,
						     data.pbos[i]);
					glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
					glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
				}

				glDeleteBuffers(1, &data.pbos[i]);
			}

			if (data.textures[i])
				glDeleteTextures(1, &data.textures[i]);
		}
	}

	if (data.fbo)
		glDeleteFramebuffers(1, &data.fbo);

	gl_error("gl_free", "GL error occurred on free");

	memset(&data, 0, sizeof(data));

	SIMPLELOG_LOGGER_DEBUG(nullptr,"------------------ gl capture freed ------------------");
}

static inline void *base_get_proc(const char *name)
{
	return (void *)GetProcAddress(gl, name);
}

static inline void *wgl_get_proc(const char *name)
{
	return (void *)jimglGetProcAddress(name);
}

static inline void *get_proc(const char *name)
{
	void *func = wgl_get_proc(name);
	if (!func)
		func = base_get_proc(name);

	return func;
}

template<class _Ty>
void get_proc(const char* name, _Ty& ptr) {
	ptr = (_Ty)get_proc(name);
}

static void init_nv_functions(void)
{
	jimglDXSetResourceShareHandleNV = (WGLSETRESOURCESHAREHANDLENVPROC)get_proc("wglDXSetResourceShareHandleNV");
	jimglDXOpenDeviceNV = (WGLDXOPENDEVICENVPROC)get_proc("wglDXOpenDeviceNV");
	jimglDXCloseDeviceNV = (WGLDXCLOSEDEVICENVPROC)get_proc("wglDXCloseDeviceNV");
	jimglDXRegisterObjectNV = (WGLDXREGISTEROBJECTNVPROC)get_proc("wglDXRegisterObjectNV");
	jimglDXUnregisterObjectNV =(WGLDXUNREGISTEROBJECTNVPROC)get_proc("wglDXUnregisterObjectNV");
	jimglDXObjectAccessNV = (WGLDXOBJECTACCESSNVPROC)get_proc("wglDXObjectAccessNV");
	jimglDXLockObjectsNV = (WGLDXLOCKOBJECTSNVPROC)get_proc("wglDXLockObjectsNV");
	jimglDXUnlockObjectsNV = (WGLDXUNLOCKOBJECTSNVPROC)get_proc("wglDXUnlockObjectsNV");

	nv_capture_available =
		!!jimglDXSetResourceShareHandleNV && !!jimglDXOpenDeviceNV &&
		!!jimglDXCloseDeviceNV && !!jimglDXRegisterObjectNV &&
		!!jimglDXUnregisterObjectNV && !!jimglDXObjectAccessNV &&
		!!jimglDXLockObjectsNV && !!jimglDXUnlockObjectsNV;

	if (nv_capture_available)
		SIMPLELOG_LOGGER_INFO(nullptr,"Shared-texture OpenGL capture available");
}

#define GET_PROC(cur_func, ptr, func)                                      \
	do {                                                               \
		get_proc(#func,ptr);                                     \
		if (!ptr) {                                                \
			SIMPLELOG_LOGGER_ERROR(nullptr,"{}: failed to get function '{}'", #cur_func, \
			     #func);                                       \
			success = false;                                   \
		}                                                          \
	} while (false)

static bool init_gl_functions(void)
{
	bool success = true;

	jimglGetProcAddress = (WGLGETPROCADDRESSPROC)base_get_proc("wglGetProcAddress");
	if (!jimglGetProcAddress) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"init_gl_functions: failed to get wglGetProcAddress");
		return false;
	}

	GET_PROC(init_gl_functions, jimglMakeCurrent, wglMakeCurrent);
	GET_PROC(init_gl_functions, jimglGetCurrentDC, wglGetCurrentDC);
	GET_PROC(init_gl_functions, jimglGetCurrentContext,wglGetCurrentContext);
	GET_PROC(init_gl_functions, glTexImage2D, glTexImage2D);
	GET_PROC(init_gl_functions, glReadBuffer, glReadBuffer);
	GET_PROC(init_gl_functions, glGetTexImage, glGetTexImage);
	GET_PROC(init_gl_functions, glDrawBuffer, glDrawBuffer);
	GET_PROC(init_gl_functions, glGetError, glGetError);
	GET_PROC(init_gl_functions, glBufferData, glBufferData);
	GET_PROC(init_gl_functions, glDeleteBuffers, glDeleteBuffers);
	GET_PROC(init_gl_functions, glDeleteTextures, glDeleteTextures);
	GET_PROC(init_gl_functions, glGenBuffers, glGenBuffers);
	GET_PROC(init_gl_functions, glGenTextures, glGenTextures);
	GET_PROC(init_gl_functions, glMapBuffer, glMapBuffer);
	GET_PROC(init_gl_functions, glUnmapBuffer, glUnmapBuffer);
	GET_PROC(init_gl_functions, glBindBuffer, glBindBuffer);
	GET_PROC(init_gl_functions, glGetIntegerv, glGetIntegerv);
	GET_PROC(init_gl_functions, glBindTexture, glBindTexture);
	GET_PROC(init_gl_functions, glGenFramebuffers, glGenFramebuffers);
	GET_PROC(init_gl_functions, glDeleteFramebuffers, glDeleteFramebuffers);
	GET_PROC(init_gl_functions, glBindFramebuffer, glBindFramebuffer);
	GET_PROC(init_gl_functions, glBlitFramebuffer, glBlitFramebuffer);
	GET_PROC(init_gl_functions, glFramebufferTexture2D,glFramebufferTexture2D);
	GET_PROC(init_gl_functions, glActiveTexture, glActiveTexture);
	GET_PROC(init_gl_functions, glGenVertexArrays, glGenVertexArrays);
	GET_PROC(init_gl_functions, glBufferSubData, glBufferSubData);
	GET_PROC(init_gl_functions, glDrawElementsBaseVertex, glDrawElementsBaseVertex);
	GET_PROC(init_gl_functions, glDeleteVertexArrays, glDeleteVertexArrays);
	GET_PROC(init_gl_functions, glIsProgram, glIsProgram);
	GET_PROC(init_gl_functions, glIsEnabled, glIsEnabled);
	GET_PROC(init_gl_functions, glScissor, glScissor);
	GET_PROC(init_gl_functions, glDrawElements, glDrawElements);
	GET_PROC(init_gl_functions, glUseProgram, glUseProgram);
	GET_PROC(init_gl_functions, glBindSampler, glBindSampler);
	GET_PROC(init_gl_functions, glBindVertexArray, glBindVertexArray);
	GET_PROC(init_gl_functions, glBlendEquationSeparate, glBlendEquationSeparate);
	GET_PROC(init_gl_functions, glBlendFuncSeparate, glBlendFuncSeparate);
	GET_PROC(init_gl_functions, glEnable, glEnable);
	GET_PROC(init_gl_functions, glDisable, glDisable);
	GET_PROC(init_gl_functions, glViewport, glViewport);
	GET_PROC(init_gl_functions, glPolygonMode, glPolygonMode);
	GET_PROC(init_gl_functions, glBlendEquation, glBlendEquation);
	GET_PROC(init_gl_functions, glGetString, glGetString);
	GET_PROC(init_gl_functions, glUniform1i, glUniform1i);
	GET_PROC(init_gl_functions, glUniformMatrix4fv, glUniformMatrix4fv);
	GET_PROC(init_gl_functions, glEnableVertexAttribArray, glEnableVertexAttribArray);
	GET_PROC(init_gl_functions, glVertexAttribPointer, glVertexAttribPointer);
	GET_PROC(init_gl_functions, glCreateShader, glCreateShader);
	GET_PROC(init_gl_functions, glShaderSource, glShaderSource);
	GET_PROC(init_gl_functions, glCompileShader, glCompileShader);
	GET_PROC(init_gl_functions, glCreateProgram, glCreateProgram);
	GET_PROC(init_gl_functions, glAttachShader, glAttachShader);
	GET_PROC(init_gl_functions, glLinkProgram, glLinkProgram);
	GET_PROC(init_gl_functions, glDetachShader, glDetachShader);
	GET_PROC(init_gl_functions, glDeleteShader, glDeleteShader);
	GET_PROC(init_gl_functions, glGetUniformLocation, glGetUniformLocation);
	GET_PROC(init_gl_functions, glGetAttribLocation, glGetAttribLocation);
	GET_PROC(init_gl_functions, glTexParameteri, glTexParameteri);
	GET_PROC(init_gl_functions, glPixelStorei, glPixelStorei);

	init_nv_functions();
	return success;
}

static void get_window_size(HDC hdc, uint32_t *cx, uint32_t *cy)
{
	HWND hwnd = WindowFromDC(hdc);
	RECT rc = {0};

	if (darkest_dungeon_fix) {
		*cx = 1920;
		*cy = 1080;
	} else {
		GetClientRect(hwnd, &rc);
		*cx = rc.right;
		*cy = rc.bottom;
	}
}

static inline bool gl_shtex_init_window(void)
{
	data.hwnd = CreateWindowExW(
		0, DUMMY_WINDOW_CLASS_NAME, L"Dummy GL window, ignore",
		WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 2, 2, NULL,
		NULL, GetModuleHandle(NULL), NULL);
	if (!data.hwnd) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_window: failed to create window:{}",
		     GetLastError());
		return false;
	}

	return true;
}

typedef HRESULT(WINAPI *create_dxgi_factory1_t)(REFIID, void **);

static const D3D_FEATURE_LEVEL feature_levels[] = {
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
};

static inline bool gl_shtex_init_d3d11(void)
{
	D3D_FEATURE_LEVEL level_used;
	IDXGIFactory1 *factory;
	IDXGIAdapter *adapter;
	HRESULT hr;

	HMODULE d3d11 = load_system_library("d3d11.dll");
	if (!d3d11) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11: failed to load D3D11.dll: {}",
		     GetLastError());
		return false;
	}

	HMODULE dxgi = load_system_library("dxgi.dll");
	if (!dxgi) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11: failed to load DXGI.dll: {}",
		     GetLastError());
		return false;
	}

	DXGI_SWAP_CHAIN_DESC desc = {0};
	desc.BufferCount = 2;
	desc.BufferDesc.Format = data.format;
	desc.BufferDesc.Width = 2;
	desc.BufferDesc.Height = 2;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SampleDesc.Count = 1;
	desc.Windowed = true;
	desc.OutputWindow = data.hwnd;

	create_dxgi_factory1_t create_factory =
		(create_dxgi_factory1_t)GetProcAddress(dxgi, "CreateDXGIFactory1");
	if (!create_factory) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11: failed to load CreateDXGIFactory1 "
		     "procedure: {}",
		     GetLastError());
		return false;
	}

	PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create =
		(PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
	if (!create) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11: failed to load "
		     "D3D11CreateDeviceAndSwapChain procedure: {}",
		     GetLastError());
		return false;
	}

	hr = create_factory(GUID_IDXGIFactory1, (void **)&factory);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11: failed to create factory {}", hr);
		return false;
	}

	hr = factory->EnumAdapters1(0, (IDXGIAdapter1**)&adapter);
	factory->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "gl_shtex_init_d3d11: failed to create adapter {}", hr);
		return false;
	}

	hr = create(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, feature_levels,
		    sizeof(feature_levels) / sizeof(D3D_FEATURE_LEVEL),
		    D3D11_SDK_VERSION, &desc, &data.dxgi_swap,
		    &data.d3d11_device, &level_used, &data.d3d11_context);
	adapter->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr, "gl_shtex_init_d3d11: failed to create device {}", hr);
		return false;
	}

	return true;
}

static inline bool gl_shtex_init_d3d11_tex(void)
{
	IDXGIResource *dxgi_res;
	HRESULT hr;

	D3D11_TEXTURE2D_DESC desc = {0};
	desc.Width = data.cx;
	desc.Height = data.cy;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = data.format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	hr = data.d3d11_device->CreateTexture2D( &desc, NULL,
					  &data.d3d11_tex);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11_tex: failed to create texture {}",
			hr);
		return false;
	}

	hr = data.d3d11_tex->QueryInterface(GUID_IDXGIResource,
					 (void **)&dxgi_res);
	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11_tex: failed to get IDXGIResource {}",
			hr);
		return false;
	}

	hr = dxgi_res->GetSharedHandle(&data.handle);
	dxgi_res->Release();

	if (FAILED(hr)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_d3d11_tex: failed to get shared handle{}",
			hr);
		return false;
	}

	return true;
}

static inline bool gl_shtex_init_gl_tex(void)
{
	data.gl_device = jimglDXOpenDeviceNV(data.d3d11_device);
	if (!data.gl_device) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_gl_tex: failed to open device");
		return false;
	}

	glGenTextures(1, &data.texture);
	if (gl_error("gl_shtex_init_gl_tex", "failed to generate texture")) {
		return false;
	}

	data.gl_dxobj = jimglDXRegisterObjectNV(data.gl_device, data.d3d11_tex,
						data.texture, GL_TEXTURE_2D,
						WGL_ACCESS_WRITE_DISCARD_NV);
	if (!data.gl_dxobj) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_shtex_init_gl_tex: failed to register object");
		return false;
	}

	return true;
}

static inline bool gl_init_fbo(void)
{
	glGenFramebuffers(1, &data.fbo);
	return !gl_error("gl_init_fbo", "failed to initialize FBO");
}

static bool gl_shtex_init(HWND window)
{
	if (!gl_shtex_init_window()) {
		return false;
	}
	if (!gl_shtex_init_d3d11()) {
		return false;
	}
	if (!gl_shtex_init_d3d11_tex()) {
		return false;
	}
	if (!gl_shtex_init_gl_tex()) {
		return false;
	}
	if (!gl_init_fbo()) {
		return false;
	}
	if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
				data.format, true, (uintptr_t)data.handle)) {
		return false;
	}

	SIMPLELOG_LOGGER_ERROR(nullptr,"gl shared texture capture successful");
	return true;
}

static inline bool gl_shmem_init_data(size_t idx, size_t size)
{
	glBindBuffer(GL_PIXEL_PACK_BUFFER, data.pbos[idx]);
	if (gl_error("gl_shmem_init_data", "failed to bind pbo")) {
		return false;
	}

	glBufferData(GL_PIXEL_PACK_BUFFER, size, 0, GL_STREAM_READ);
	if (gl_error("gl_shmem_init_data", "failed to set pbo data")) {
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, data.textures[idx]);
	if (gl_error("gl_shmem_init_data", "failed to set bind texture")) {
		return false;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, data.cx, data.cy, 0, GL_BGRA,
		     GL_UNSIGNED_BYTE, NULL);
	if (gl_error("gl_shmem_init_data", "failed to set texture data")) {
		return false;
	}

	return true;
}

static inline bool gl_shmem_init_buffers(void)
{
	uint32_t size = data.cx * data.cy * 4;
	GLint last_pbo;
	GLint last_tex;

	glGenBuffers(NUM_BUFFERS, data.pbos);
	if (gl_error("gl_shmem_init_buffers", "failed to generate buffers")) {
		return false;
	}

	glGenTextures(NUM_BUFFERS, data.textures);
	if (gl_error("gl_shmem_init_buffers", "failed to generate textures")) {
		return false;
	}

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &last_pbo);
	if (gl_error("gl_shmem_init_buffers",
		     "failed to save pixel pack buffer")) {
		return false;
	}

	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
	if (gl_error("gl_shmem_init_buffers", "failed to save texture")) {
		return false;
	}

	for (size_t i = 0; i < NUM_BUFFERS; i++) {
		if (!gl_shmem_init_data(i, size)) {
			return false;
		}
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, last_pbo);
	glBindTexture(GL_TEXTURE_2D, last_tex);
	return true;
}

static bool gl_shmem_init(HWND window)
{
	if (!gl_shmem_init_buffers()) {
		return false;
	}
	if (!gl_init_fbo()) {
		return false;
	}
	if (!capture_init_shmem(&data.shmem_info, window, data.cx, data.cy,
				data.cx * 4, data.format, true)) {
		return false;
	}

	SIMPLELOG_LOGGER_ERROR(nullptr,"gl memory capture successful");
	return true;
}

#define INIT_SUCCESS 0
#define INIT_FAILED -1
#define INIT_SHTEX_FAILED -2

static int gl_init(HDC hdc)
{
	HWND window = WindowFromDC(hdc);
	int ret = INIT_FAILED;
	bool success = false;
	RECT rc = {0};

	if (darkest_dungeon_fix) {
		data.cx = 1920;
		data.cy = 1080;
	} else {
		GetClientRect(window, &rc);
		data.cx = rc.right;
		data.cy = rc.bottom;
	}

	data.hdc = hdc;
	data.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	data.using_shtex = nv_capture_available &&
			   !global_hook_info->force_shmem &&
			   !data.shmem_fallback;

	if (data.using_shtex) {
		success = gl_shtex_init(window);
		if (!success)
			ret = INIT_SHTEX_FAILED;
	} else {
		success = gl_shmem_init(window);
	}

	if (!success)
		gl_free();
	else
		ret = INIT_SUCCESS;

	GLint major = 0;
	GLint minor = 0;
	//glGetIntegerv(GL_MAJOR_VERSION, &major);
	//glGetIntegerv(GL_MINOR_VERSION, &minor);
	auto versionStr=glGetString(GL_VERSION);
	sscanf((const char*)versionStr, "%d.%d", &major, &minor);
	data.GlVersion = (GLuint)(major * 100 + minor * 10);

	data.HasPolygonMode = true;
	if (strstr((const char*)versionStr, "OpenGL ES 3")) {
		data.IsES3 = true;
		data.HasPolygonMode = false;
	}else if (strstr((const char*)versionStr, "OpenGL ES 2")) {
		data.IsES2 = true;
		data.HasPolygonMode = false;
	}
	if (data.GlVersion > 200) {
		auto languageVerStr = glGetString(GL_SHADING_LANGUAGE_VERSION);
		sscanf((const char*)languageVerStr, "%d.%d", &major, &minor);
		data.GlslVersion = major * 100 + minor;
	}

	if (data.GlVersion >= 320)
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &data.GlProfileMask);
	data.IsCompat = (data.GlProfileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
	data.HasClipOrigin = (data.GlVersion >= 450);

	gl_init_gui();
	return ret;
}

static void gl_copy_backbuffer(GLuint dst)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, data.fbo);
	if (gl_error("gl_copy_backbuffer", "failed to bind FBO")) {
		return;
	}

	glBindTexture(GL_TEXTURE_2D, dst);
	if (gl_error("gl_copy_backbuffer", "failed to bind texture")) {
		return;
	}

	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, dst, 0);
	if (gl_error("gl_copy_backbuffer", "failed to set frame buffer")) {
		return;
	}

	glReadBuffer(GL_BACK);

	/* darkest dungeon fix */
	darkest_dungeon_fix = glGetError() == GL_INVALID_OPERATION &&
			      _strcmpi(process_name, "Darkest.exe") == 0;

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if (gl_error("gl_copy_backbuffer", "failed to set draw buffer")) {
		return;
	}

	glBlitFramebuffer(0, 0, data.cx, data.cy, 0, 0, data.cx, data.cy,
			  GL_COLOR_BUFFER_BIT, GL_LINEAR);
	gl_error("gl_copy_backbuffer", "failed to blit");
}

static void gl_shtex_capture(void)
{
	GLint last_fbo;
	GLint last_tex;

	jimglDXLockObjectsNV(data.gl_device, 1, &data.gl_dxobj);

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
	if (gl_error("gl_shtex_capture", "failed to get last fbo")) {
		return;
	}

	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
	if (gl_error("gl_shtex_capture", "failed to get last texture")) {
		return;
	}

	gl_copy_backbuffer(data.texture);

	glBindTexture(GL_TEXTURE_2D, last_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);

	jimglDXUnlockObjectsNV(data.gl_device, 1, &data.gl_dxobj);

	data.dxgi_swap->Present(0, 0);
}

static void gl_shmem_capture_copy(int i)
{
	if (data.texture_ready[i]) {
		GLvoid *buffer;

		data.texture_ready[i] = false;

		glBindBuffer(GL_PIXEL_PACK_BUFFER, data.pbos[i]);
		if (gl_error("gl_shmem_capture_queue_copy",
			     "failed to bind pbo")) {
			return;
		}

		buffer = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		if (buffer) {
			data.texture_mapped[i] = true;
			shmem_copy_data(i, buffer);
		}
	}
}

static inline void gl_shmem_capture_stage(GLuint dst_pbo, GLuint src_tex)
{
	glBindTexture(GL_TEXTURE_2D, src_tex);
	if (gl_error("gl_shmem_capture_stage", "failed to bind src_tex")) {
		return;
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, dst_pbo);
	if (gl_error("gl_shmem_capture_stage", "failed to bind dst_pbo")) {
		return;
	}

	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	if (gl_error("gl_shmem_capture_stage", "failed to read src_tex")) {
		return;
	}
}

static void gl_shmem_capture(void)
{
	int next_tex;
	GLint last_fbo;
	GLint last_tex;
	GLint last_pbo;

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
	if (gl_error("gl_shmem_capture", "failed to get last fbo")) {
		return;
	}

	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
	if (gl_error("gl_shmem_capture", "failed to get last texture")) {
		return;
	}

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &last_pbo);
	if (gl_error("gl_shmem_capture", "failed to get last pbo")) {
		return;
	}

	next_tex = (data.cur_tex + 1) % NUM_BUFFERS;
	gl_shmem_capture_copy(next_tex);

	if (data.copy_wait < NUM_BUFFERS - 1) {
		data.copy_wait++;
	} else {
		if (shmem_texture_data_lock(data.cur_tex)) {
			glBindBuffer(GL_PIXEL_PACK_BUFFER,
				     data.pbos[data.cur_tex]);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			data.texture_mapped[data.cur_tex] = false;
			shmem_texture_data_unlock(data.cur_tex);
		}

		gl_copy_backbuffer(data.textures[data.cur_tex]);
		gl_shmem_capture_stage(data.pbos[data.cur_tex],
				       data.textures[data.cur_tex]);
		data.texture_ready[data.cur_tex] = true;
	}

	glBindTexture(GL_TEXTURE_2D, last_tex);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, last_pbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);

	data.cur_tex = next_tex;
}

static void gl_capture(HDC hdc)
{
	static bool critical_failure = false;

	if (critical_failure) {
		return;
	}

	if (!functions_initialized) {
		functions_initialized = init_gl_functions();
		if (!functions_initialized) {
			critical_failure = true;
			return;
		}
	}

	/* reset error flag */
	glGetError();

	if (capture_should_stop()) {
		gl_free();
	}
	if (capture_should_init()) {
		if (gl_init(hdc) == INIT_SHTEX_FAILED) {
			data.shmem_fallback = true;
			gl_init(hdc);
		}
	}
	//if (is_capture_ready() && hdc == data.hdc) {
	//	uint32_t new_cx;
	//	uint32_t new_cy;

	//	/* reset capture if resized */
	//	get_window_size(hdc, &new_cx, &new_cy);
	//	if (new_cx != data.cx || new_cy != data.cy) {
	//		if (new_cx != 0 && new_cy != 0)
	//			gl_free();
	//		return;
	//	}

	//	if (data.using_shtex)
	//		gl_shtex_capture();
	//	else
	//		gl_shmem_capture();
	//}

	if (is_capture_active()) {
		gl_window_update();
		if (is_overlay_active()) {
			overlay_ui_new_frame();
			gl_render_draw_data(overlay_ui_render());
		}
	}
}

static inline void gl_swap_begin(HDC hdc)
{
	const bool first = swap_recurse == 0;
	++swap_recurse;

	if (first && !global_hook_info->capture_overlay)
		gl_capture(hdc);
}

static inline void gl_swap_end(HDC hdc)
{
	--swap_recurse;
	const bool first = swap_recurse == 0;

	if (first && global_hook_info->capture_overlay)
		gl_capture(hdc);
}

static BOOL WINAPI hook_swap_buffers(HDC hdc)
{
	gl_swap_begin(hdc);

	const BOOL ret = RealSwapBuffers(hdc);

	gl_swap_end(hdc);

	return ret;
}

static BOOL WINAPI hook_wgl_swap_buffers(HDC hdc)
{
	gl_swap_begin(hdc);

	const BOOL ret = RealWglSwapBuffers(hdc);

	gl_swap_end(hdc);

	return ret;
}

static BOOL WINAPI hook_wgl_swap_layer_buffers(HDC hdc, UINT planes)
{
	gl_swap_begin(hdc);

	const BOOL ret = RealWglSwapLayerBuffers(hdc, planes);

	gl_swap_end(hdc);

	return ret;
}

static BOOL WINAPI hook_wgl_delete_context(HGLRC hrc)
{
	if (is_capture_active() && functions_initialized) {
		HDC last_hdc = jimglGetCurrentDC();
		HGLRC last_hrc = jimglGetCurrentContext();

		jimglMakeCurrent(data.hdc, hrc);
		gl_free();
		jimglMakeCurrent(last_hdc, last_hrc);
	}

	return RealWglDeleteContext(hrc);
}

static bool gl_register_window(void)
{
	WNDCLASSW wc = {0};
	wc.style = CS_OWNDC;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpfnWndProc = DefWindowProc;
	wc.lpszClassName = DUMMY_WINDOW_CLASS_NAME;

	if (!RegisterClassW(&wc)) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"gl_register_window: failed to register window class: {}",
		     GetLastError());
		return false;
	}

	return true;
}

bool hook_gl(void)
{
	void *wgl_dc_proc;
	void *wgl_slb_proc;
	void *wgl_sb_proc;

	gl = get_system_module("opengl32.dll");
	if (!gl) {
		return false;
	}

	/* "life is feudal: your own" somehow uses both opengl and directx at
	 * the same time, so blacklist it from capturing opengl */

	if (_strcmpi(process_name, "yo_cm_client.exe") == 0 ||
	    _strcmpi(process_name, "cm_client.exe") == 0) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Ignoring opengl for game: %s", process_name);
		return true;
	}

	if (!gl_register_window()) {
		return true;
	}

	wgl_dc_proc = base_get_proc("wglDeleteContext");
	wgl_slb_proc = base_get_proc("wglSwapLayerBuffers");
	wgl_sb_proc = base_get_proc("wglSwapBuffers");

	DetourTransactionBegin();

	RealSwapBuffers = SwapBuffers;
	DetourAttach((PVOID *)&RealSwapBuffers, hook_swap_buffers);
	if (wgl_dc_proc) {
		RealWglDeleteContext = (PFN_WglDeleteContext)wgl_dc_proc;
		DetourAttach((PVOID *)&RealWglDeleteContext,
			     hook_wgl_delete_context);
	}
	if (wgl_slb_proc) {
		RealWglSwapLayerBuffers = (PFN_WglSwapLayerBuffers)wgl_slb_proc;
		DetourAttach((PVOID *)&RealWglSwapLayerBuffers,
			     hook_wgl_swap_layer_buffers);
	}
	if (wgl_sb_proc) {
		RealWglSwapBuffers = (PFN_WglSwapBuffers)wgl_sb_proc;
		DetourAttach((PVOID *)&RealWglSwapBuffers,
			     hook_wgl_swap_buffers);
	}

	const LONG error = DetourTransactionCommit();
	const bool success = error == NO_ERROR;
	if (success) {
		SIMPLELOG_LOGGER_ERROR(nullptr,"Hooked SwapBuffers");
		if (RealWglDeleteContext)
			SIMPLELOG_LOGGER_ERROR(nullptr,"Hooked wglDeleteContext");
		if (RealWglSwapLayerBuffers)
			SIMPLELOG_LOGGER_ERROR(nullptr,"Hooked wglSwapLayerBuffers");
		if (RealWglSwapBuffers)
			SIMPLELOG_LOGGER_ERROR(nullptr,"Hooked wglSwapBuffers");
		SIMPLELOG_LOGGER_ERROR(nullptr,"Hooked GL");
	} else {
		RealSwapBuffers = NULL;
		RealWglDeleteContext = NULL;
		RealWglSwapLayerBuffers = NULL;
		RealWglSwapBuffers = NULL;
		SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to attach Detours hook: %ld", error);
	}

	return success;
}

///// gui

typedef struct GlSharedWindowGraphicInfo_t {
	bool bNT_shared;
	HANDLE SharedHandle{ NULL };
	ComPtr<ID3D11Texture2D> WindowTexDX11{ nullptr };
	ComPtr<ID3D11Texture2D> CopyTexDX11{ nullptr };

	ComPtr<ID3D11Texture2D> TestTexDX11{ nullptr };
	GLuint GlTexture;
	HANDLE GlObj;
}GlSharedWindowGraphicInfo_t;
static std::unordered_map<uint64_t, std::shared_ptr<GlSharedWindowGraphicInfo_t>>  SharedWindowGraphicInfos;

static void gl_remove_window(uint64_t id)
{
	auto itr = SharedWindowGraphicInfos.find(id);
	if (itr == SharedWindowGraphicInfos.end()) {
		return;
	}
	auto info=itr->second;
	if (info->GlObj) {
		jimglDXUnregisterObjectNV(data.gl_device, &info->GlObj);
	}
	if (info->GlTexture) {
		glDeleteTextures(1,&info->GlTexture);
	}
	SharedWindowGraphicInfos.erase(id);
}

static void gl_window_update() {
	HRESULT hr;
	std::set<uint64_t> needDel;
	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
		std::inserter(needDel, needDel.begin()),
		[](const std::pair<uint64_t, std::shared_ptr<GlSharedWindowGraphicInfo_t>>& key_value)
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
				gl_remove_window(id);
			}
		}
		auto res = SharedWindowGraphicInfos.emplace(id, std::make_shared<GlSharedWindowGraphicInfo_t>());
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
				gl_remove_window(id);
				continue;
			}
			hr = dev->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				gl_remove_window(id);
				continue;
			}
		}
		else {
			hr = data.d3d11_device->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&(pinfo->WindowTexDX11)));
			if (FAILED(hr)) {
				gl_remove_window(id);
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

		hr = data.d3d11_device->CreateTexture2D(&desc, NULL, &pinfo->CopyTexDX11);
		if (FAILED(hr)) {
			gl_remove_window(id);
			continue;
		}
		
		glGenTextures(1, &pinfo->GlTexture);
		if (gl_error("gl_window_update", "failed to generate texture")) {
			gl_remove_window(id);
			continue;
		}

		pinfo->GlObj = jimglDXRegisterObjectNV(data.gl_device, pinfo->CopyTexDX11.Get(),
			pinfo->GlTexture, GL_TEXTURE_2D,
			WGL_ACCESS_READ_WRITE_NV);
		if (!pinfo->GlObj) {
			SIMPLELOG_LOGGER_ERROR(nullptr, "gl_window_update: failed to register object");
			gl_remove_window(id);
			continue;
		}

		jimglDXLockObjectsNV(data.gl_device, 1, &pinfo->GlObj);
		windowInfo->WindowTextureID = (intptr_t)pinfo->GlTexture;
	}
	for (auto& id : needDel) {
		gl_remove_window(id);
	}
	for (auto& pair : SharedWindowGraphicInfos) {
		ComPtr<IDXGIKeyedMutex>  pDXGIKeyedMutex;
		auto& pSharedWindowGraphicInfo = pair.second;
		jimglDXUnlockObjectsNV(data.gl_device, 1, &pSharedWindowGraphicInfo->GlObj);
		if (pSharedWindowGraphicInfo->bNT_shared) {
			hr = pSharedWindowGraphicInfo->WindowTexDX11->QueryInterface(IID_PPV_ARGS(&pDXGIKeyedMutex));
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
		jimglDXLockObjectsNV(data.gl_device, 1, &pSharedWindowGraphicInfo->GlObj);
	}
	for (auto& id : needDel) {
		SharedWindowGraphicInfos.erase(id);
	}
}

static void gl_setup_render_state(ImDrawData* draw_data, int fb_width, int fb_height, GLuint vertex_array_object) {
	// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_SCISSOR_TEST);

	if (data.GlVersion >= 310)
		glDisable(GL_PRIMITIVE_RESTART);
	if (data.HasPolygonMode)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Support for GL 4.5 rarely used glClipControl(GL_UPPER_LEFT)
#if defined(GL_CLIP_ORIGIN)
	bool clip_origin_lower_left = true;
	if (data.HasClipOrigin)
	{
		GLenum current_clip_origin = 0; glGetIntegerv(GL_CLIP_ORIGIN, (GLint*)&current_clip_origin);
		if (current_clip_origin == GL_UPPER_LEFT)
			clip_origin_lower_left = false;
	}
#endif

	// Setup viewport, orthographic projection matrix
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
#if defined(GL_CLIP_ORIGIN)
	if (!clip_origin_lower_left) { float tmp = T; T = B; B = tmp; } // Swap top and bottom if origin is upper left
#endif
	const float ortho_projection[4][4] =
	{
		{ 2.0f / (R - L),   0.0f,         0.0f,   0.0f },
		{ 0.0f,         2.0f / (T - B),   0.0f,   0.0f },
		{ 0.0f,         0.0f,        -1.0f,   0.0f },
		{ (R + L) / (L - R),  (T + B) / (B - T),  0.0f,   1.0f },
	};
	glUseProgram(data.ShaderHandle);
	glUniform1i(data.AttribLocationTex, 0);
	glUniformMatrix4fv(data.AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

	if (data.GlVersion >= 330 || data.IsES3)
		glBindSampler(0, 0); // We use combined texture/sampler state. Applications using GL 3.3 and GL ES 3.0 may set that otherwise.

	(void)vertex_array_object;

	glBindVertexArray(vertex_array_object);

	// Bind vertex/index buffers and setup attributes for ImDrawVert
	glBindBuffer(GL_ARRAY_BUFFER, data.VboHandle);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ElementsHandle);
	glEnableVertexAttribArray(data.AttribLocationVtxPos);
	glEnableVertexAttribArray(data.AttribLocationVtxUV);
	glEnableVertexAttribArray(data.AttribLocationVtxColor);
	glVertexAttribPointer(data.AttribLocationVtxPos, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos));
	glVertexAttribPointer(data.AttribLocationVtxUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, uv));
	glVertexAttribPointer(data.AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col));
}

static void gl_render_draw_data(ImDrawData* draw_data) {
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0)
		return;

	// Backup GL state
	GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
	glActiveTexture(GL_TEXTURE0);
	GLuint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&last_program);
	GLuint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&last_texture);
	GLuint last_sampler; if (data.GlVersion >= 330 || data.IsES3) { glGetIntegerv(GL_SAMPLER_BINDING, (GLint*)&last_sampler); }
	else { last_sampler = 0; }
	GLuint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer);

	GLuint last_vertex_array_object; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&last_vertex_array_object);

	GLint last_polygon_mode[2]; if (data.HasPolygonMode) { glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode); }

	GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
	GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
	GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
	GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
	GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
	GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
	GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
	GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
	GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
	GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
	GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
	GLboolean last_enable_stencil_test = glIsEnabled(GL_STENCIL_TEST);
	GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean last_enable_primitive_restart = (data.GlVersion >= 310) ? glIsEnabled(GL_PRIMITIVE_RESTART) : GL_FALSE;


	// Setup desired GL state
	// Recreate the VAO every time (this is to easily allow multiple GL contexts to be rendered to. VAO are not shared among GL contexts)
	// The renderer would actually work without any VAO bound, but then our VertexAttrib calls would overwrite the default one currently bound.
	GLuint vertex_array_object = 0;

	glGenVertexArrays(1, &vertex_array_object);

	gl_setup_render_state(draw_data, fb_width, fb_height, vertex_array_object);

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	// Render command lists
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];

		// Upload vertex/index buffers
		// - OpenGL drivers are in a very sorry state nowadays....
		//   During 2021 we attempted to switch from glBufferData() to orphaning+glBufferSubData() following reports
		//   of leaks on Intel GPU when using multi-viewports on Windows.
		// - After this we kept hearing of various display corruptions issues. We started disabling on non-Intel GPU, but issues still got reported on Intel.
		// - We are now back to using exclusively glBufferData(). So bd->UseBufferSubData IS ALWAYS FALSE in this code.
		//   We are keeping the old code path for a while in case people finding new issues may want to test the bd->UseBufferSubData path.
		// - See https://github.com/ocornut/imgui/issues/4468 and please report any corruption issues.
		const GLsizeiptr vtx_buffer_size = (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
		const GLsizeiptr idx_buffer_size = (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
		if (data.UseBufferSubData)
		{
			if (data.VertexBufferSize < vtx_buffer_size)
			{
				data.VertexBufferSize = vtx_buffer_size;
				glBufferData(GL_ARRAY_BUFFER, data.VertexBufferSize, nullptr, GL_STREAM_DRAW);
			}
			if (data.IndexBufferSize < idx_buffer_size)
			{
				data.IndexBufferSize = idx_buffer_size;
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.IndexBufferSize, nullptr, GL_STREAM_DRAW);
			}
			glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_buffer_size, (const GLvoid*)cmd_list->VtxBuffer.Data);
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_size, (const GLvoid*)cmd_list->IdxBuffer.Data);
		}
		else
		{
			glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);
		}

		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != nullptr)
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					gl_setup_render_state(draw_data, fb_width, fb_height, vertex_array_object);
				else
					pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
				ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y));

				// Bind texture, Draw
				glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
				if (data.GlVersion >= 320)
					glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)), (GLint)pcmd->VtxOffset);
				else
					glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)));
			}
		}
	}

	// Destroy the temporary VAO

	glDeleteVertexArrays(1, &vertex_array_object);


	// Restore modified GL state
	// This "glIsProgram()" check is required because if the program is "pending deletion" at the time of binding backup, it will have been deleted by now and will cause an OpenGL error. See #6220.
	if (last_program == 0 || glIsProgram(last_program)) glUseProgram(last_program);
	glBindTexture(GL_TEXTURE_2D, last_texture);
	if (data.GlVersion >= 330 || data.IsES3)
		glBindSampler(0, last_sampler);
	glActiveTexture(last_active_texture);
	glBindVertexArray(last_vertex_array_object);
	glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
	glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
	glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
	if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (last_enable_stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
	if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (data.GlVersion >= 310) { if (last_enable_primitive_restart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART); }

	// Desktop OpenGL 3.0 and OpenGL 3.1 had separate polygon draw modes for front-facing and back-facing faces of polygons
	if (data.HasPolygonMode) { if (data.GlVersion <= 310 || data.IsCompat) { glPolygonMode(GL_FRONT, (GLenum)last_polygon_mode[0]); glPolygonMode(GL_BACK, (GLenum)last_polygon_mode[1]); } else { glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]); } }


	glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
	glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
}

static bool gl_create_fonts_texture() {
	ImGuiIO& io = ImGui::GetIO();
	// Build texture atlas
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

	// Upload texture to graphics system
	// (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
	GLint last_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	glGenTextures(1, &data.FontTexture);
	glBindTexture(GL_TEXTURE_2D, data.FontTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifdef GL_UNPACK_ROW_LENGTH // Not on WebGL/ES
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// Store our identifier
	io.Fonts->SetTexID((ImTextureID)(intptr_t)data.FontTexture);

	// Restore state
	glBindTexture(GL_TEXTURE_2D, last_texture);

	return true;
}
static bool gl_init_gui() {
	if (data.ShaderHandle) {
		return true;
	}
	GLint last_texture, last_array_buffer;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);

	GLint last_pixel_unpack_buffer;
	if (data.GlVersion >= 210) { glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &last_pixel_unpack_buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); }

	GLint last_vertex_array;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);


	// Parse GLSL version string
	int glsl_version = data.GlslVersion;

	const GLchar* vertex_shader_glsl_120 =
		"uniform mat4 ProjMtx;\n"
		"attribute vec2 Position;\n"
		"attribute vec2 UV;\n"
		"attribute vec4 Color;\n"
		"varying vec2 Frag_UV;\n"
		"varying vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"    Frag_UV = UV;\n"
		"    Frag_Color = Color;\n"
		"    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
		"}\n";

	const GLchar* vertex_shader_glsl_130 =
		"uniform mat4 ProjMtx;\n"
		"in vec2 Position;\n"
		"in vec2 UV;\n"
		"in vec4 Color;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"    Frag_UV = UV;\n"
		"    Frag_Color = Color;\n"
		"    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
		"}\n";

	const GLchar* vertex_shader_glsl_300_es =
		"precision highp float;\n"
		"layout (location = 0) in vec2 Position;\n"
		"layout (location = 1) in vec2 UV;\n"
		"layout (location = 2) in vec4 Color;\n"
		"uniform mat4 ProjMtx;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"    Frag_UV = UV;\n"
		"    Frag_Color = Color;\n"
		"    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
		"}\n";

	const GLchar* vertex_shader_glsl_410_core =
		"layout (location = 0) in vec2 Position;\n"
		"layout (location = 1) in vec2 UV;\n"
		"layout (location = 2) in vec4 Color;\n"
		"uniform mat4 ProjMtx;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"    Frag_UV = UV;\n"
		"    Frag_Color = Color;\n"
		"    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
		"}\n";

	const GLchar* fragment_shader_glsl_120 =
		"#ifdef GL_ES\n"
		"    precision mediump float;\n"
		"#endif\n"
		"uniform sampler2D Texture;\n"
		"varying vec2 Frag_UV;\n"
		"varying vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV.st);\n"
		"}\n";

	const GLchar* fragment_shader_glsl_130 =
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"out vec4 Out_Color;\n"
		"void main()\n"
		"{\n"
		"    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";

	const GLchar* fragment_shader_glsl_300_es =
		"precision mediump float;\n"
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"layout (location = 0) out vec4 Out_Color;\n"
		"void main()\n"
		"{\n"
		"    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";

	const GLchar* fragment_shader_glsl_410_core =
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"uniform sampler2D Texture;\n"
		"layout (location = 0) out vec4 Out_Color;\n"
		"void main()\n"
		"{\n"
		"    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";

	// Select shaders matching our GLSL versions
	const GLchar* vertex_shader = nullptr;
	const GLchar* fragment_shader = nullptr;
	if (glsl_version < 130)
	{
		vertex_shader = vertex_shader_glsl_120;
		fragment_shader = fragment_shader_glsl_120;
	}
	else if (glsl_version >= 410)
	{
		vertex_shader = vertex_shader_glsl_410_core;
		fragment_shader = fragment_shader_glsl_410_core;
	}
	else if (glsl_version == 300)
	{
		vertex_shader = vertex_shader_glsl_300_es;
		fragment_shader = fragment_shader_glsl_300_es;
	}
	else
	{
		vertex_shader = vertex_shader_glsl_130;
		fragment_shader = fragment_shader_glsl_130;
	}

	char GlslVersionString[24];
	sprintf(GlslVersionString, "#version %d", data.GlslVersion);
	// Create shaders
	const GLchar* vertex_shader_with_version[2] = { GlslVersionString, vertex_shader };
	GLuint vert_handle = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vert_handle, 2, vertex_shader_with_version, nullptr);
	glCompileShader(vert_handle);
	//CheckShader(vert_handle, "vertex shader");

	const GLchar* fragment_shader_with_version[2] = { GlslVersionString, fragment_shader };
	GLuint frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(frag_handle, 2, fragment_shader_with_version, nullptr);
	glCompileShader(frag_handle);
	//(frag_handle, "fragment shader");

	// Link
	data.ShaderHandle = glCreateProgram();
	glAttachShader(data.ShaderHandle, vert_handle);
	glAttachShader(data.ShaderHandle, frag_handle);
	glLinkProgram(data.ShaderHandle);
	//CheckProgram(data.ShaderHandle, "shader program");

	glDetachShader(data.ShaderHandle, vert_handle);
	glDetachShader(data.ShaderHandle, frag_handle);
	glDeleteShader(vert_handle);
	glDeleteShader(frag_handle);

	data.AttribLocationTex = glGetUniformLocation(data.ShaderHandle, "Texture");
	data.AttribLocationProjMtx = glGetUniformLocation(data.ShaderHandle, "ProjMtx");
	data.AttribLocationVtxPos = (GLuint)glGetAttribLocation(data.ShaderHandle, "Position");
	data.AttribLocationVtxUV = (GLuint)glGetAttribLocation(data.ShaderHandle, "UV");
	data.AttribLocationVtxColor = (GLuint)glGetAttribLocation(data.ShaderHandle, "Color");

	// Create buffers
	glGenBuffers(1, &data.VboHandle);
	glGenBuffers(1, &data.ElementsHandle);

	gl_create_fonts_texture();

	// Restore modified GL state
	glBindTexture(GL_TEXTURE_2D, last_texture);
	glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);

	if (data.GlVersion >= 210) { glBindBuffer(GL_PIXEL_UNPACK_BUFFER, last_pixel_unpack_buffer); }
	glBindVertexArray(last_vertex_array);


	return true;
}
static void gl_free_gui() {
	std::set<uint64_t> tempSet;
	std::transform(SharedWindowGraphicInfos.cbegin(), SharedWindowGraphicInfos.cend(),
		std::inserter(tempSet, tempSet.begin()),
		[](const std::pair<uint64_t, std::shared_ptr<GlSharedWindowGraphicInfo_t>>& key_value)
		{ return key_value.first; });
	for (auto id:tempSet) {
		gl_remove_window(id);
	}

}