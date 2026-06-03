#include "ClipSoul/SingleInstance.h"


namespace ClipSoul {

SingleInstanceLock::SingleInstanceLock(std::wstring name) {
    mutex_ = CreateMutexW(nullptr, TRUE, name.c_str());
    already_running_ = mutex_ && GetLastError() == ERROR_ALREADY_EXISTS;
}

SingleInstanceLock::~SingleInstanceLock() {
    if (mutex_) {
        CloseHandle(mutex_);
    }
}

bool SingleInstanceLock::Acquired() const {
    return mutex_ != nullptr;
}

bool SingleInstanceLock::AlreadyRunning() const {
    return already_running_;
}

} // namespace ClipSoul
