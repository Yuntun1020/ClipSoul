#include "TestHarness.h"

#include "ClipSoul/SingleInstance.h"

#include <Windows.h>

TEST_CASE(SingleInstanceLockDetectsAlreadyRunningMutex) {
    const std::wstring name = L"Local\\ClipSoulTest-" + std::to_wstring(GetCurrentProcessId());

    {
        ClipSoul::SingleInstanceLock first(name);
        REQUIRE(first.Acquired());
        REQUIRE(!first.AlreadyRunning());

        ClipSoul::SingleInstanceLock second(name);
        REQUIRE(second.Acquired());
        REQUIRE(second.AlreadyRunning());
    }

    ClipSoul::SingleInstanceLock third(name);
    REQUIRE(third.Acquired());
    REQUIRE(!third.AlreadyRunning());
}
