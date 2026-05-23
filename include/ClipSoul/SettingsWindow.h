#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

namespace ClipSoul {

class App;

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HistoryStore& store, App& app);
    bool Create(HWND owner);
    void Show();
    HWND hwnd() const { return hwnd_; }
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    void LoadToControls();
    void SaveFromControls();
    void ApplyBackdrop();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND limit_edit_ = nullptr;
    HWND paused_check_ = nullptr;
    HWND startup_check_ = nullptr;
    HistoryStore& store_;
    App& app_;
};

} // namespace ClipSoul
