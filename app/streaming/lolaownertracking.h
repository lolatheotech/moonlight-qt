#pragma once
#include <windows.h>

// Keep the SDL/native procedure in the chain: it owns WM_SIZE/WM_MOVE and
// WM_CLOSE semantics. Reposition only after it has processed the owner change.
// This synchronous path also runs while native title-bar dragging pumps messages.
inline LRESULT DispatchLoLaOwnerMessage(
    WNDPROC previous, HWND owner, UINT message, WPARAM wParam, LPARAM lParam,
    HWND control, HWND (*ownerOf)(HWND), bool (*position)(HWND))
{
    const auto result = previous ? CallWindowProcW(previous, owner, message, wParam, lParam)
                                 : DefWindowProcW(owner, message, wParam, lParam);
    if (message == WM_WINDOWPOSCHANGED && control && ownerOf(control) == owner)
        position(control);
    return result;
}