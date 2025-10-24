#include <windows.h>
#include "Window.h"
#include "iostream"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();
    Window win(hInstance, 800, 600, true);
    
    while(win.update()) {
        win.writeBackground(Black);
        for(int i = 0; i < 10000000; i++)
            win.writePoint(200, 200, White);

        WCHAR buffer[64];
        swprintf(buffer, 64, L"FPS: %.1f", win.getFPS());
        win.writeText(10, 10, buffer, White);

        win.present();
    }

    return 0;
}
