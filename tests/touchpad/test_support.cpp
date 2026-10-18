// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <SDL3/SDL.h>
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/libraries/libs.h"
#include "core/user_settings.h"
#include "input/input_handler.h"
#include "input/input_mouse.h"
#include "sdl_window.h"

Frontend::WindowSDL* g_window = nullptr;
float mouse_test_dx = 0, mouse_test_dy = 0;

namespace {
[[noreturn]] void UnexpectedDependency() {
    std::cerr << "FAIL: touchpad test invoked an unrelated hardware/settings dependency\n";
    std::abort();
}
} // namespace

void assert_fail_impl() {
    UnexpectedDependency();
}
[[noreturn]] void unreachable_impl() {
    UnexpectedDependency();
}
namespace Common::Log {
std::array<Level, NUM_LOG_CLASSES> g_class_levels = [] {
    std::array<Level, NUM_LOG_CLASSES> levels;
    levels.fill(Level::Off);
    return levels;
}();
void VLog(Class, Level, const char*, int, const char*, fmt::string_view, fmt::format_args) {
    UnexpectedDependency();
}
} // namespace Common::Log

std::shared_ptr<UserSettingsImpl> UserSettingsImpl::GetInstance() {
    UnexpectedDependency();
}
std::shared_ptr<EmulatorSettingsImpl> EmulatorSettingsImpl::GetInstance() {
    static auto settings = std::make_shared<EmulatorSettingsImpl>();
    return settings;
}
EmulatorSettingsImpl::EmulatorSettingsImpl() = default;
EmulatorSettingsImpl::~EmulatorSettingsImpl() = default;

namespace Common::FS {
const std::filesystem::path& GetUserPath(PathType type) {
    if (type != PathType::UserDir) {
        UnexpectedDependency();
    }
    static const std::filesystem::path path{TEST_USER_DIR};
    return path;
}
} // namespace Common::FS

namespace Overlay {
void ShowVolume() {
    UnexpectedDependency();
}
} // namespace Overlay
namespace ImGuiEmuSettings {
void OpenInGameSettingsDialog() {
    UnexpectedDependency();
}
} // namespace ImGuiEmuSettings
bool capture_gamepad_input = false;
namespace ImGui::Core {
bool IsGamepadInputCaptured() {
    return capture_gamepad_input;
}
} // namespace ImGui::Core
void LinkSymbolImpl(Core::Loader::SymbolsResolver*, const char*, const char*, u16, const char*, u64,
                    Core::Loader::SymbolType) {
    UnexpectedDependency();
}

int touchpad_mode_events = 0;
int gyro_mode_events = 0;
User* UserManager::GetUserByID(s32) {
    UnexpectedDependency();
}
User* UserManager::GetUserByPlayerIndex(s32) {
    UnexpectedDependency();
}
void UserManager::LoginUser(User*, s32) {
    UnexpectedDependency();
}

extern "C" {
SDL_MouseButtonFlags SDL_GetRelativeMouseState(float* x, float* y) {
    *x = mouse_test_dx;
    *y = mouse_test_dy;
    mouse_test_dx = mouse_test_dy = 0;
    return 0;
}
SDL_MouseButtonFlags SDL_GetMouseState(float*, float*) {
    UnexpectedDependency();
}
bool SDL_PushEvent(SDL_Event* event) {
    if (event->type == SDL_EVENT_MOUSE_TO_GYRO) {
        ++gyro_mode_events;
        Input::ToggleMouseModeTo(Input::MouseMode::Gyro);
        return true;
    }
    if (event->type != SDL_EVENT_MOUSE_TO_TOUCHPAD) {
        UnexpectedDependency();
    }
    ++touchpad_mode_events;
    return true;
}
bool SDL_RumbleGamepad(SDL_Gamepad*, Uint16, Uint16, Uint32) {
    UnexpectedDependency();
}
SDL_JoystickID* SDL_GetGamepads(int*) {
    UnexpectedDependency();
}
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad*) {
    UnexpectedDependency();
}
void SDL_CloseGamepad(SDL_Gamepad*) {
    UnexpectedDependency();
}
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID) {
    UnexpectedDependency();
}
bool SDL_SetGamepadSensorEnabled(SDL_Gamepad*, SDL_SensorType, bool) {
    UnexpectedDependency();
}
float SDL_GetGamepadSensorDataRate(SDL_Gamepad*, SDL_SensorType) {
    UnexpectedDependency();
}
void SDL_free(void*) {
    UnexpectedDependency();
}
SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID) {
    UnexpectedDependency();
}
}
