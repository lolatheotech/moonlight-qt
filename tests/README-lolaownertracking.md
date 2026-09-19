# LoLa native owner-tracking unit test

This test calls the real dispatch helper using a fake prior window procedure.
It creates no windows, launches no client/stream, binds no ports and never touches
a live process. Run only on the authorized development VM.

With the existing UCRT64 compiler on PATH:

    g++ -std=c++17 -Wall -Wextra -Werror tests/lolaownertracking_test.cpp -o <isolated-output>/overlay-unit.exe -luser32
    <isolated-output>/overlay-unit.exe

Cases: synchronous consecutive owner moves without any timer, negative coordinates,
unrelated owner, absent control, fullscreen-style position change, WM_CLOSE and
cursor forwarding with the original result preserved. Failure exits 1 without a
CRT dialog. This is not a visual dragging/fullscreen/Disconnect acceptance test.

The flight evidence also contains a negative-control run: a copied helper with the
position callback disabled (old forward-only behavior) fails the same test.
Do not mutate the production helper to run that control.