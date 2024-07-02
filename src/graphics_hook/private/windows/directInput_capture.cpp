#include "directInput_capture.h"

#include <stdio.h>
#include <Windows.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <detours.h>
#pragma comment(lib, "dxguid.lib")
//// By Timb3r
//// Source: https://guidedhacking.com/forums/gamephreakers-game-modding-tutorials.450/2018/08/introduction-to-vtable-hooking/
//void* HookVTableFunction(void* pVTable, void* fnHookFunc, int nOffset)
//{
//    intptr_t ptrVtable = *((intptr_t*)pVTable); // Pointer to our chosen vtable
//    intptr_t ptrFunction = ptrVtable + sizeof(intptr_t) * nOffset; // The offset to the function (remember it's a zero indexed array with a size of four bytes)
//    intptr_t ptrOriginal = *((intptr_t*)ptrFunction); // Save original address
//
//    // Edit the memory protection so we can modify it
//    MEMORY_BASIC_INFORMATION mbi;
//    VirtualQuery((LPCVOID)ptrFunction, &mbi, sizeof(mbi));
//    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
//
//    // Overwrite the old function with our new one
//    *((intptr_t*)ptrFunction) = (intptr_t)fnHookFunc;
//
//    // Restore the protection
//    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);
//
//    // Return the original function address incase we want to call it
//    return (void*)ptrOriginal;
//}
typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE , DWORD , REFIID , LPVOID* , LPUNKNOWN );

typedef HRESULT(WINAPI* IDirectInputDeviceA_GetDeviceData_t)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
IDirectInputDeviceA_GetDeviceData_t RealGetDeviceDataA = NULL;

typedef HRESULT(WINAPI* IDirectInputDeviceW_GetDeviceData_t)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
IDirectInputDeviceW_GetDeviceData_t RealGetDeviceDataW = NULL;

HRESULT WINAPI HookGetDeviceDataA(IDirectInputDevice8A* pThis, DWORD a, LPDIDEVICEOBJECTDATA b, LPDWORD c, DWORD d)
{
    HRESULT ret = RealGetDeviceDataA(pThis, a, b, c, d);
    if (ret == DI_OK)
    {
        for (DWORD i = 0; i < *c; i++)
        {
            if (LOBYTE(b[i].dwData) > 0)
            {
                switch (b[i].dwOfs)
                {
                case DIK_W:
                    printf("[W]");
                    break;
                case DIK_S:
                    printf("[S]");
                    break;
                case DIK_A:
                    printf("[A]");
                    break;
                case DIK_D:
                    printf("[D]");
                    break;
                }
            }
        }
    }
    return ret;
}
HRESULT WINAPI HookGetDeviceDataW(IDirectInputDevice8W* pThis, DWORD a, LPDIDEVICEOBJECTDATA b, LPDWORD c, DWORD d)
{
    HRESULT ret = RealGetDeviceDataW(pThis, a, b, c, d);
    if (ret == DI_OK)
    {
        for (DWORD i = 0; i < *c; i++)
        {
            if (LOBYTE(b[i].dwData) > 0)
            {
                switch (b[i].dwOfs)
                {
                case DIK_W:
                    printf("[W]");
                    break;
                case DIK_S:
                    printf("[S]");
                    break;
                case DIK_A:
                    printf("[A]");
                    break;
                case DIK_D:
                    printf("[D]");
                    break;
                }
            }
        }
    }
    return ret;
}

bool hook_directinput() {
    auto dhandle=GetModuleHandleA("dinput8.dll");
    if (!dhandle) {
        return false;
    }
    DirectInput8Create_t pDirectInput8Create=(DirectInput8Create_t)GetProcAddress(dhandle, "DirectInput8Create");

    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(NULL);
    IDirectInput8A* pDirectInputA = NULL;
    IDirectInputDevice8A* lpdiKeyboardA;
    IDirectInput8W* pDirectInputW = NULL;
    IDirectInputDevice8W* lpdiKeyboardW;

    if (pDirectInput8Create(hInst, DIRECTINPUT_VERSION, IID_IDirectInput8A, (LPVOID*)&pDirectInputA, NULL) != DI_OK)
    {
        printf("[-] Failed to acquire DirectInput handle\n");
        return false;
    }
    
    if (pDirectInputA->CreateDevice(GUID_SysKeyboard, &lpdiKeyboardA, NULL) != DI_OK)
    {
        printf("[-] Unable to attach to kbd\n");
        pDirectInputA->Release();
        return false;
    }

    DetourTransactionBegin();
    RealGetDeviceDataA = (IDirectInputDeviceA_GetDeviceData_t)(*(intptr_t**)pDirectInputA)[10];
    DetourAttach((PVOID*)&RealGetDeviceDataA, HookGetDeviceDataA);
    LONG error = DetourTransactionCommit();
    bool success = error == NO_ERROR;
    lpdiKeyboardA->Release();
    pDirectInputA->Release();




    if (pDirectInput8Create(hInst, DIRECTINPUT_VERSION, IID_IDirectInput8W, (LPVOID*)&pDirectInputW, NULL) != DI_OK)
    {
        printf("[-] Failed to acquire DirectInput handle\n");
        return false;
    }

    if (pDirectInputW->CreateDevice(GUID_SysKeyboard, &lpdiKeyboardW, NULL) != DI_OK)
    {
        printf("[-] Unable to attach to kbd\n");
        pDirectInputA->Release();
        return false;
    }
    DetourTransactionBegin();
    RealGetDeviceDataW = (IDirectInputDeviceW_GetDeviceData_t)(*(intptr_t**)pDirectInputW)[10];
    DetourAttach((PVOID*)&RealGetDeviceDataW, HookGetDeviceDataW);
    error = DetourTransactionCommit();
    success = error == NO_ERROR;
    lpdiKeyboardA->Release();
    pDirectInputA->Release();

    return true;
    //// Set the data type to keyboard
    //lpdiKeyboard->SetDataFormat(&c_dfDIKeyboard);
    //// We want non-exclusive access i.e sharing it with other applications
    //lpdiKeyboard->SetCooperativeLevel(GetActiveWindow(), DISCL_NONEXCLUSIVE);

    //// Attempt to acquire the device
    //if (lpdiKeyboard->Acquire() == DI_OK)
    //{
    //    printf("[+] Acquired keyboard.\nPress escape to exit...\n");

    //    // Start looping and grabbing the data from the device
    //    while (1)
    //    {
    //        // Poll for new data
    //        lpdiKeyboard->Poll();
    //        // This is how the data is returned as an array 256 bytes for each key
    //        BYTE  diKeys[256] = { 0 };
    //        // Get the state
    //        if (lpdiKeyboard->GetDeviceState(256, diKeys) == DI_OK)
    //        {
    //            // Check if the escape was pressed
    //            if (diKeys[DIK_ESCAPE] & 0x80) {
    //                break;
    //            }
    //        }
    //        // We don't need realtime access, don't flood the CPU
    //        Sleep(100);
    //    }
    //    // Unacquire the keyboard
    //    lpdiKeyboard->Unacquire();
    //}
    // Free the keyboard and device objects

}
