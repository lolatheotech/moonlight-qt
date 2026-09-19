#include "../app/streaming/lolaownertracking.h"
#include <cstdlib>
#include <cstdio>
#include <initializer_list>

static void Require(bool value, const char* expression) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", expression);
        std::exit(1); // a failing unit test must not open a CRT assertion dialog
    }
}
#define CHECK(expression) Require((expression), #expression)

// Headless unit test: no HWND creation, event pump, stream or live app.
static HWND stream = reinterpret_cast<HWND>(1);
static HWND overlay = reinterpret_cast<HWND>(2);
static int ownerX, overlayX, moves, forwarded;
static UINT lastMessage;
static HWND OwnerOf(HWND value) { CHECK(value == overlay); return stream; }
static bool Position(HWND value) {
    CHECK(value == overlay);
    CHECK(forwarded > 0); // downstream native procedure completes first
    overlayX = ownerX + 18;
    ++moves;
    return true;
}
static LRESULT CALLBACK Previous(HWND, UINT message, WPARAM wParam, LPARAM lParam) {
    ++forwarded;
    lastMessage = message;
    if (message == WM_WINDOWPOSCHANGED) ownerX = static_cast<int>(lParam);
    CHECK(wParam == 17);
    return 123;
}
static void Dispatch(HWND owner, UINT message, LPARAM x, HWND control = overlay) {
    CHECK(DispatchLoLaOwnerMessage(Previous, owner, message, 17, x, control, OwnerOf, Position) == 123);
}
int main() {
    // No WM_TIMER: every move must be reflected before dispatch returns.
    for (int x : {0, 40, -1920, 600, 3840}) {
        Dispatch(stream, WM_WINDOWPOSCHANGED, x);
        CHECK(overlayX == x + 18);
    }
    CHECK(moves == 5);
    Dispatch(reinterpret_cast<HWND>(3), WM_WINDOWPOSCHANGED, 90);
    Dispatch(stream, WM_WINDOWPOSCHANGED, 90, nullptr); // destroyed/disabled overlay
    CHECK(moves == 5);
    // Fullscreen changes use the same native positioning path.
    Dispatch(stream, WM_WINDOWPOSCHANGED, 0);
    CHECK(moves == 6 && overlayX == 18);
    Dispatch(stream, WM_CLOSE, 0); // overlay Disconnect must reach SDL
    CHECK(lastMessage == WM_CLOSE && moves == 6);
    Dispatch(stream, WM_SETCURSOR, 0);
    CHECK(lastMessage == WM_SETCURSOR && moves == 6);
    std::puts("PASS: synchronous moves, negative coordinates, ownership, absent overlay, fullscreen position, close/cursor forwarding");
}