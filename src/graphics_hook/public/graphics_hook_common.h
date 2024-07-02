#pragma once

#ifdef __cplusplus
#if defined( _WIN32 ) && !defined( _X360 )
#if defined( SONKWO_HOOK_EXPORTS )
#define SONKWO_API extern "C" __declspec( dllexport ) 
#elif defined( SONKWO_HOOK_NODLL )
#define SONKWO_API extern "C"
#else
#define SONKWO_API extern "C" __declspec( dllimport ) 
#endif 
#elif defined( GNUC )
#if defined( SONKWO_HOOK_EXPORTS )
#define SONKWO_API extern "C" __attribute__ ((visibility("default"))) 
#else
#define SONKWO_API extern "C" 
#endif 
#else // !WIN32
#if defined( SONKWO_API_EXPORTS )
#define SONKWO_API extern "C"  
#else
#define SONKWO_API extern "C" 
#endif 
#endif
#else
#if defined( _WIN32 ) && !defined( _X360 )
#if defined( SONKWO_API_EXPORTS )
#define SONKWO_API  __declspec( dllexport ) 
#elif defined( SONKWO_API_NODLL )
#define SONKWO_API 
#else
#define SONKWO_API __declspec( dllimport ) 
#endif 
#elif defined( GNUC )
#if defined( SONKWO_API_EXPORTS )
#define SONKWO_API __attribute__ ((visibility("default"))) 
#else
#define SONKWO_API 
#endif 
#else // !WIN32
#if defined( SONKWO_API_EXPORTS )
#define SONKWO_API
#else
#define SONKWO_API
#endif 
#endif
#endif

#define SONKWO_CALLTYPE __cdecl