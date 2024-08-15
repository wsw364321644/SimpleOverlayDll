#pragma once
#include <simple_os_defs.h>
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long GLulong;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef ptrdiff_t GLintptrARB;
typedef ptrdiff_t GLsizeiptrARB;


#define GL_INVALID_OPERATION 0x0502

#define GL_UNSIGNED_BYTE 0x1401

#define GL_RGB 0x1907
#define GL_RGBA 0x1908

#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601

#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#define GL_BUFFER_ACCESS 0x88BB
#define GL_BUFFER_MAPPED 0x88BC
#define GL_BUFFER_MAP_POINTER 0x88BD
#define GL_STREAM_DRAW 0x88E0
#define GL_STREAM_READ 0x88E1
#define GL_STREAM_COPY 0x88E2
#define GL_STATIC_DRAW 0x88E4
#define GL_STATIC_READ 0x88E5
#define GL_STATIC_COPY 0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF

#define GL_TEXTURE_2D 0x0DE1
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6

#define WGL_ACCESS_READ_ONLY_NV 0x0000
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002

#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1


#ifndef GL_VERSION_1_0
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_TRIANGLES                      0x0004
#define GL_ONE                            1
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408
#define GL_POLYGON_MODE                   0x0B40
#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_STENCIL_TEST                   0x0B90
#define GL_VIEWPORT                       0x0BA2
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_BOX                    0x0C10
#define GL_SCISSOR_TEST                   0x0C11
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_PACK_ALIGNMENT                 0x0D05
#define GL_TEXTURE_2D                     0x0DE1
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_RGBA                           0x1908
#define GL_FILL                           0x1B02
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_EXTENSIONS                     0x1F03
#define GL_LINEAR                         0x2601
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
typedef void (WINAPI* GLPOLYGONMODEPROC) (GLenum face, GLenum mode);
typedef void (WINAPI* GLSCISSORPROC) (GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (WINAPI* GLTEXPARAMETERIPROC) (GLenum target, GLenum pname, GLint param);
typedef void (WINAPI* GLTEXIMAGE2DPROC) (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (WINAPI* GLCLEARPROC) (GLbitfield mask);
typedef void (WINAPI* GLCLEARCOLORPROC) (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (WINAPI* GLDISABLEPROC) (GLenum cap);
typedef void (WINAPI* GLENABLEPROC) (GLenum cap);
typedef void (WINAPI* GLFLUSHPROC) (void);
typedef void (WINAPI* GLPIXELSTOREIPROC) (GLenum pname, GLint param);
typedef void (WINAPI* GLREADPIXELSPROC) (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels);
typedef GLenum(WINAPI* GLGETERRORPROC) (void);
typedef void (WINAPI* GLGETINTEGERVPROC) (GLenum pname, GLint* data);
typedef const GLubyte* (WINAPI* GLGETSTRINGPROC) (GLenum name);
typedef GLboolean(WINAPI* GLISENABLEDPROC) (GLenum cap);
typedef void (WINAPI* GLVIEWPORTPROC) (GLint x, GLint y, GLsizei width, GLsizei height);
#endif

#ifndef GL_VERSION_1_1
typedef float GLclampf;
typedef double GLclampd;
#define GL_TEXTURE_BINDING_2D             0x8069
typedef void (WINAPI* GLDRAWELEMENTSPROC) (GLenum mode, GLsizei count, GLenum type, const void* indices);
typedef void (WINAPI* GLBINDTEXTUREPROC) (GLenum target, GLuint texture);
typedef void (WINAPI* GLDELETETEXTURESPROC) (GLsizei n, const GLuint* textures);
typedef void (WINAPI* GLGENTEXTURESPROC) (GLsizei n, GLuint* textures);

#endif

#ifndef GL_VERSION_1_3
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0  0x84C0
#endif

#ifndef GL_VERSION_1_4
#define GL_BLEND_DST_RGB                  0x80C8
#define GL_BLEND_SRC_RGB                  0x80C9
#define GL_BLEND_DST_ALPHA                0x80CA
#define GL_BLEND_SRC_ALPHA                0x80CB
#define GL_FUNC_ADD                       0x8006
typedef void (WINAPI* GLBLENDFUNCSEPARATEPROC) (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
typedef void (WINAPI* GLBLENDEQUATIONPROC) (GLenum mode);
#endif

#ifndef GL_VERSION_1_5
typedef intptr_t GLsizeiptr;
typedef intptr_t GLintptr;
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_ARRAY_BUFFER_BINDING           0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING   0x8895
#define GL_STREAM_DRAW                    0x88E0
typedef void (WINAPI* GLBUFFERSUBDATAPROC) (GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
#endif

#ifndef GL_VERSION_2_0
typedef char GLchar;
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_BLEND_EQUATION_RGB             0x8009
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED    0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE       0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE     0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE       0x8625
#define GL_VERTEX_ATTRIB_ARRAY_POINTER    0x8645
#define GL_BLEND_EQUATION_ALPHA           0x883D
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_CURRENT_PROGRAM                0x8B8D
#define GL_UPPER_LEFT                     0x8CA2
typedef void (WINAPI* GLBLENDEQUATIONSEPARATEPROC) (GLenum modeRGB, GLenum modeAlpha);
typedef void (WINAPI* GLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (WINAPI* GLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint(WINAPI* GLCREATEPROGRAMPROC) (void);
typedef GLuint(WINAPI* GLCREATESHADERPROC) (GLenum type);
typedef void (WINAPI* GLDELETEPROGRAMPROC) (GLuint program);
typedef void (WINAPI* GLDELETESHADERPROC) (GLuint shader);
typedef void (WINAPI* GLDETACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (WINAPI* GLDISABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (WINAPI* GLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef GLint(WINAPI* GLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar* name);
typedef void (WINAPI* GLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint* params);
typedef void (WINAPI* GLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (WINAPI* GLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint* params);
typedef void (WINAPI* GLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLint(WINAPI* GLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar* name);
typedef void (WINAPI* GLGETVERTEXATTRIBIVPROC) (GLuint index, GLenum pname, GLint* params);
typedef void (WINAPI* GLGETVERTEXATTRIBPOINTERVPROC) (GLuint index, GLenum pname, void** pointer);
typedef GLboolean(WINAPI* GLISPROGRAMPROC) (GLuint program);
typedef void (WINAPI* GLLINKPROGRAMPROC) (GLuint program);
typedef void (WINAPI* GLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (WINAPI* GLUSEPROGRAMPROC) (GLuint program);
typedef void (WINAPI* GLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (WINAPI* GLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void (WINAPI* GLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
#endif


#ifndef GL_VERSION_3_0
#define GL_MAJOR_VERSION                  0x821B
#define GL_MINOR_VERSION                  0x821C
#define GL_NUM_EXTENSIONS                 0x821D
#define GL_FRAMEBUFFER_SRGB               0x8DB9
#define GL_VERTEX_ARRAY_BINDING           0x85B5
typedef void (WINAPI* GLGENVERTEXARRAYSPROC) (GLsizei , GLuint*);
typedef void (WINAPI* GLGETBOOLEANI_VPROC) (GLenum target, GLuint index, GLboolean* data);
typedef void (WINAPI* GLGETINTEGERI_VPROC) (GLenum target, GLuint index, GLint* data);
typedef const GLubyte* (WINAPI* GLGETSTRINGIPROC) (GLenum name, GLuint index);
typedef void (WINAPI* GLBINDVERTEXARRAYPROC) (GLuint array);
typedef void (WINAPI* GLDELETEVERTEXARRAYSPROC) (GLsizei n, const GLuint* arrays);
#endif

#ifndef GL_VERSION_3_1
#define GL_PRIMITIVE_RESTART              0x8F9D
#endif

#ifndef GL_VERSION_3_2
typedef struct __GLsync* GLsync;
typedef uint64_t GLuint64;
typedef int64_t GLint64;
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x00000002
#define GL_CONTEXT_PROFILE_MASK           0x9126
typedef void (WINAPI* GLDRAWELEMENTSBASEVERTEXPROC) (GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex);
typedef void (WINAPI* GLGETINTEGER64I_VPROC) (GLenum target, GLuint index, GLint64* data);
#endif

#ifndef GL_VERSION_3_3
#define GL_SAMPLER_BINDING                0x8919
typedef void (WINAPI* GLBINDSAMPLERPROC) (GLuint unit, GLuint sampler);
#endif
typedef void(WINAPI *GLTEXIMAGE2DPROC)(GLenum target, GLint level,
				       GLint internal_format, GLsizei width,
				       GLsizei height, GLint border,
				       GLenum format, GLenum type,
				       const GLvoid *data);
typedef void(WINAPI *GLGETTEXIMAGEPROC)(GLenum target, GLint level,
					GLenum format, GLenum type,
					GLvoid *img);
typedef void(WINAPI *GLREADBUFFERPROC)(GLenum);
typedef void(WINAPI *GLDRAWBUFFERPROC)(GLenum mode);
typedef void(WINAPI *GLGETINTEGERVPROC)(GLenum pname, GLint *params);
typedef GLenum(WINAPI *GLGETERRORPROC)();
typedef BOOL(WINAPI *WGLSWAPLAYERBUFFERSPROC)(HDC, UINT);
typedef BOOL(WINAPI *WGLSWAPBUFFERSPROC)(HDC);
typedef BOOL(WINAPI *WGLDELETECONTEXTPROC)(HGLRC);
typedef PROC(WINAPI *WGLGETPROCADDRESSPROC)(LPCSTR);
typedef BOOL(WINAPI *WGLMAKECURRENTPROC)(HDC, HGLRC);
typedef HDC(WINAPI *WGLGETCURRENTDCPROC)();
typedef HGLRC(WINAPI *WGLGETCURRENTCONTEXTPROC)();
typedef HGLRC(WINAPI *WGLCREATECONTEXTPROC)(HDC);
typedef void(WINAPI *GLBUFFERDATAARBPROC)(GLenum target, GLsizeiptrARB size,
					  const GLvoid *data, GLenum usage);
typedef void(WINAPI *GLDELETEBUFFERSARBPROC)(GLsizei n, const GLuint *buffers);
typedef void(WINAPI *GLDELETETEXTURESPROC)(GLsizei n, const GLuint *buffers);
typedef void(WINAPI *GLGENBUFFERSARBPROC)(GLsizei n, GLuint *buffers);
typedef void(WINAPI *GLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef GLvoid *(WINAPI *GLMAPBUFFERPROC)(GLenum target, GLenum access);
typedef GLboolean(WINAPI *GLUNMAPBUFFERPROC)(GLenum target);
typedef void(WINAPI *GLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void(WINAPI *GLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void(WINAPI *GLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void(WINAPI *GLDELETEFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void(WINAPI *GLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void(WINAPI *GLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0,
					    GLint srcX1, GLint srcY1,
					    GLint dstX0, GLint dstY0,
					    GLint dstX1, GLint dstY1,
					    GLbitfield mask, GLenum filter);
typedef void(WINAPI *GLFRAMEBUFFERTEXTURE2DPROC)(GLenum target,
						 GLenum attachment,
						 GLenum textarget,
						 GLuint texture, GLint level);
typedef BOOL(WINAPI *WGLSETRESOURCESHAREHANDLENVPROC)(void *, HANDLE);
typedef HANDLE(WINAPI *WGLDXOPENDEVICENVPROC)(void *);
typedef BOOL(WINAPI *WGLDXCLOSEDEVICENVPROC)(HANDLE);
typedef HANDLE(WINAPI *WGLDXREGISTEROBJECTNVPROC)(HANDLE, void *, GLuint,
						  GLenum, GLenum);
typedef BOOL(WINAPI *WGLDXUNREGISTEROBJECTNVPROC)(HANDLE, HANDLE);
typedef BOOL(WINAPI *WGLDXOBJECTACCESSNVPROC)(HANDLE, GLenum);
typedef BOOL(WINAPI *WGLDXLOCKOBJECTSNVPROC)(HANDLE, GLint, HANDLE *);
typedef BOOL(WINAPI *WGLDXUNLOCKOBJECTSNVPROC)(HANDLE, GLint, HANDLE *);
typedef void (WINAPI* GLACTIVETEXTUREPROC) (GLenum);

static GLTEXIMAGE2DPROC glTexImage2D = NULL;
static GLGETTEXIMAGEPROC glGetTexImage = NULL;
static GLREADBUFFERPROC glReadBuffer = NULL;
static GLDRAWBUFFERPROC glDrawBuffer = NULL;
static GLGETINTEGERVPROC glGetIntegerv = NULL;
static GLGETERRORPROC glGetError = NULL;
static WGLGETPROCADDRESSPROC jimglGetProcAddress = NULL;
static WGLMAKECURRENTPROC jimglMakeCurrent = NULL;
static WGLGETCURRENTDCPROC jimglGetCurrentDC = NULL;
static WGLGETCURRENTCONTEXTPROC jimglGetCurrentContext = NULL;
static GLBUFFERDATAARBPROC glBufferData = NULL;
static GLDELETEBUFFERSARBPROC glDeleteBuffers = NULL;
static GLDELETETEXTURESPROC glDeleteTextures = NULL;
static GLGENBUFFERSARBPROC glGenBuffers = NULL;
static GLGENTEXTURESPROC glGenTextures = NULL;
static GLMAPBUFFERPROC glMapBuffer = NULL;
static GLUNMAPBUFFERPROC glUnmapBuffer = NULL;
static GLBINDBUFFERPROC glBindBuffer = NULL;
static GLBINDTEXTUREPROC glBindTexture = NULL;
static GLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
static GLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;
static GLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
static GLBLITFRAMEBUFFERPROC glBlitFramebuffer = NULL;
static GLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
static GLACTIVETEXTUREPROC glActiveTexture = NULL;
static GLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;
static GLBUFFERSUBDATAPROC glBufferSubData = NULL;
static GLDRAWELEMENTSBASEVERTEXPROC glDrawElementsBaseVertex = NULL;
static GLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = NULL;
static GLISPROGRAMPROC glIsProgram = NULL;
static GLISENABLEDPROC glIsEnabled = NULL;
static GLSCISSORPROC glScissor = NULL;
static GLDRAWELEMENTSPROC glDrawElements = NULL;
static GLUSEPROGRAMPROC glUseProgram = NULL;
static GLBINDSAMPLERPROC glBindSampler = NULL;
static GLBINDVERTEXARRAYPROC glBindVertexArray = NULL;
static GLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate = NULL;
static GLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = NULL;
static GLENABLEPROC glEnable = NULL;
static GLDISABLEPROC glDisable = NULL;
static GLVIEWPORTPROC glViewport = NULL;
static GLPOLYGONMODEPROC glPolygonMode = NULL;
static GLBLENDEQUATIONPROC glBlendEquation = NULL;
static GLGETSTRINGPROC glGetString = NULL;
static GLUNIFORM1IPROC glUniform1i = NULL;
static GLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;
static GLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
static GLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
static GLCREATESHADERPROC glCreateShader = NULL;
static GLSHADERSOURCEPROC glShaderSource = NULL;
static GLCOMPILESHADERPROC glCompileShader = NULL;
static GLCREATEPROGRAMPROC glCreateProgram = NULL;
static GLATTACHSHADERPROC glAttachShader = NULL;
static GLLINKPROGRAMPROC glLinkProgram = NULL;
static GLDETACHSHADERPROC glDetachShader = NULL;
static GLDELETESHADERPROC glDeleteShader = NULL;
static GLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
static GLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
static GLTEXPARAMETERIPROC glTexParameteri = NULL;
static GLPIXELSTOREIPROC glPixelStorei = NULL;
static GLDELETEPROGRAMPROC glDeleteProgram = NULL;


static WGLSETRESOURCESHAREHANDLENVPROC jimglDXSetResourceShareHandleNV = NULL;
static WGLDXOPENDEVICENVPROC jimglDXOpenDeviceNV = NULL;
static WGLDXCLOSEDEVICENVPROC jimglDXCloseDeviceNV = NULL;
static WGLDXREGISTEROBJECTNVPROC jimglDXRegisterObjectNV = NULL;
static WGLDXUNREGISTEROBJECTNVPROC jimglDXUnregisterObjectNV = NULL;
static WGLDXOBJECTACCESSNVPROC jimglDXObjectAccessNV = NULL;
static WGLDXLOCKOBJECTSNVPROC jimglDXLockObjectsNV = NULL;
static WGLDXUNLOCKOBJECTSNVPROC jimglDXUnlockObjectsNV = NULL;
