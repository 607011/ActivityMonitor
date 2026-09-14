#include <Windows.h>
#include "GraphWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    GraphWindow window;
    if (!window.Create(hInstance, nCmdShow))
        return 1;

    return window.RunMessageLoop();
}
