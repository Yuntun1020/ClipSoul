#include "ClipSoul/App.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    ClipSoul::App app(instance);
    return app.Run(show_command);
}
