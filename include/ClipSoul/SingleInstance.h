#pragma once

#include <Windows.h>

#include <string>

namespace ClipSoul {

class SingleInstanceLock {
public:
    explicit SingleInstanceLock(std::wstring name);
    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    bool Acquired() const;
    bool AlreadyRunning() const;

private:
    HANDLE mutex_ = nullptr;
    bool already_running_ = false;
};

} // namespace ClipSoul
