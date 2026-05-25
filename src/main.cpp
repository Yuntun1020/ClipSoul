#include "ClipSoul/App.h"
#include "ClipSoul/Win32Util.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    ClipSoul::EnableDpiAwareness();
    ClipSoul::App app(instance);
    return app.Run(show_command);
}
