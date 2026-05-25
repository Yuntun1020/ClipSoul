#include "ClipSoul/App.h"
#include "ClipSoul/SingleInstance.h"
#include "ClipSoul/Win32Util.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    ClipSoul::EnableDpiAwareness();
    ClipSoul::SingleInstanceLock lock(L"Local\\ClipSoul.SingleInstance");
    if (lock.AlreadyRunning()) {
        MessageBoxW(nullptr, L"程序已启动！", L"ClipSoul", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    ClipSoul::App app(instance);
    return app.Run(show_command);
}
