#include "ClipSoul/ProcessElevation.h"

namespace ClipSoul {

bool IsProcessElevatedForTest(HANDLE process_token) {
    if (!process_token) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    const BOOL ok =
        GetTokenInformation(process_token, TokenElevation, &elevation, sizeof(elevation), &returned);
    return ok && elevation.TokenIsElevated != 0;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    const bool elevated = IsProcessElevatedForTest(token);
    CloseHandle(token);
    return elevated;
}

} // namespace ClipSoul
