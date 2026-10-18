// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <unordered_set>
#include <utility>

#include <SDL3/SDL.h>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "core/libraries/system/userservice.h"
#include "core/user_settings.h"
#include "input/controller.h"

namespace Input {

using Libraries::Pad::OrbisPadButtonDataOffset;

namespace {

void CalculateOrientation(const Libraries::Pad::OrbisFVector3& angular_velocity, float delta_time,
                          const Libraries::Pad::OrbisFQuaternion& last_orientation,
                          Libraries::Pad::OrbisFQuaternion& orientation) {
    if (delta_time > 1.0f) {
        orientation = last_orientation;
        return;
    }
    Libraries::Pad::OrbisFQuaternion q = last_orientation;
    const Libraries::Pad::OrbisFQuaternion omega = {angular_velocity.x, angular_velocity.y,
                                                    angular_velocity.z, 0.0f};

    const Libraries::Pad::OrbisFQuaternion q_omega = {
        q.w * omega.x + q.x * omega.w + q.y * omega.z - q.z * omega.y,
        q.w * omega.y + q.y * omega.w + q.z * omega.x - q.x * omega.z,
        q.w * omega.z + q.z * omega.w + q.x * omega.y - q.y * omega.x,
        q.w * omega.w - q.x * omega.x - q.y * omega.y - q.z * omega.z};

    const Libraries::Pad::OrbisFQuaternion q_dot = {0.5f * q_omega.x, 0.5f * q_omega.y,
                                                    0.5f * q_omega.z, 0.5f * q_omega.w};

    q.x += q_dot.x * delta_time;
    q.y += q_dot.y * delta_time;
    q.z += q_dot.z * delta_time;
    q.w += q_dot.w * delta_time;

    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q.x /= norm;
    q.y /= norm;
    q.z /= norm;
    q.w /= norm;
    orientation = q;
}

} // namespace

GameController::GameController() : m_states_queue(64) {}

State GameController::ReadState() {
    std::lock_guard lock{m_state_mutex};
    return m_state;
}

int GameController::ReadStates(State* states, int states_num) {
    std::lock_guard lock{m_state_mutex};
    if (states_num <= 0) {
        return 0;
    }

    if (!m_state.connected) {
        states[0] = m_state;
        return 1;
    }

    if (states_num == 1) {
        // Retained history can make a later multi-sample read return up to 64 stale reports, so
        // mixed single- and multi-sample reads require dedicated tests.
        states[0] = m_state;
        return 1;
    }

    int read_count = 0;
    while (read_count < states_num) {
        auto state = m_states_queue.Pop();
        if (!state) {
            break;
        }
        states[read_count++] = std::move(*state);
    }
    return read_count;
}

void GameController::Button(OrbisPadButtonDataOffset button, bool is_pressed) {
    std::lock_guard lock{m_state_mutex};
    if (button == OrbisPadButtonDataOffset::TouchPad) {
        m_touchpad_button_pressed = is_pressed;
        is_pressed |=
            std::ranges::any_of(m_virtual_touchpad_buttons, [](bool down) { return down; });
        is_pressed |= m_virtual_swipe_active;
    }
    m_state.OnButton(button, is_pressed);
    PushStateLocked();
}

void GameController::Axis(Input::Axis axis, int value, bool smooth) {
    std::lock_guard lock{m_state_mutex};
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    m_state.OnAxis(axis, value, timestamp, smooth);
    PushStateLocked(timestamp);
}

void GameController::UpdateGyro(const float gyro[3]) {
    std::lock_guard lock{m_state_mutex};
    std::memcpy(gyro_buf, gyro, sizeof(gyro_buf));
}

void GameController::UpdateAcceleration(const float acceleration[3]) {
    std::lock_guard lock{m_state_mutex};
    std::memcpy(accel_buf, acceleration, sizeof(accel_buf));
}

void GameController::UpdateMotion(const float gyro[3], const float acceleration[3]) {
    std::lock_guard lock{m_state_mutex};
    std::memcpy(gyro_buf, gyro, sizeof(gyro_buf));
    std::memcpy(accel_buf, acceleration, sizeof(accel_buf));
}

void GameController::SetVirtualMotionButton(VirtualMotionButton button, bool is_pressed) {
    const auto index = std::to_underlying(button);
    if (index < 0 || index >= m_virtual_motion_buttons.size()) {
        return;
    }
    std::lock_guard lock{m_state_mutex};
    if (m_virtual_motion_buttons[index] == is_pressed) {
        return; // Key repeat must not restart the shake or recenter the aim.
    }
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    if (button == VirtualMotionButton::Sideways) {
        if (is_pressed) {
            m_orientation_before_sideways = m_state.orientation;
            // A quarter turn around the controller's forward axis.
            constexpr float half_sqrt_two = std::numbers::sqrt2_v<float> / 2;
            m_state.orientation = {0, 0, half_sqrt_two, half_sqrt_two};
        } else {
            m_state.orientation = m_orientation_before_sideways;
        }
        m_last_orientation_update = timestamp;
    } else {
        m_shake_start_timestamp = is_pressed ? timestamp : 0;
    }
    m_virtual_motion_buttons[index] = is_pressed;
    PushStateLocked(timestamp);
}

void GameController::ApplyVirtualMotionLocked(u64 timestamp) {
    if (m_virtual_motion_buttons[0]) {
        // Rotate world gravity into controller space, keeping acceleration consistent
        // with the sideways quaternion and subsequent mouse gyro aiming.
        const auto& q = m_state.orientation;
        m_state.acceleration = {9.81f * 2 * (q.x * q.y + q.w * q.z),
                                9.81f * (1 - 2 * (q.x * q.x + q.z * q.z)),
                                9.81f * 2 * (q.y * q.z - q.w * q.x)};
    }
    if (m_virtual_motion_buttons[1]) {
        // Translational shaking, in m/s^2 (SDL sensor units). Use elapsed time,
        // not poll count, so input events and guest report reads cannot speed it up.
        const u64 elapsed = timestamp >= m_shake_start_timestamp
                                ? timestamp - m_shake_start_timestamp : 0;
        const float phase = static_cast<float>(elapsed % 200'000) / 200'000;
        m_state.acceleration.x += 30.0f * std::sin(2 * std::numbers::pi_v<float> * phase);
    }
}

void GameController::PollState() {
    std::lock_guard lock{m_state_mutex};
    PushStateLocked();
}

void GameController::ResetOrientation() {
    std::lock_guard lock{m_state_mutex};
    m_state.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    m_orientation_before_sideways = m_state.orientation;
    if (m_virtual_motion_buttons[0]) {
        constexpr float half_sqrt_two = std::numbers::sqrt2_v<float> / 2;
        m_state.orientation = {0, 0, half_sqrt_two, half_sqrt_two};
    }
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::SetTouchpadState(int touch_index, bool touch_down, float x, float y) {
    if (touch_index < 0 || touch_index >= 2) {
        return;
    }

    std::lock_guard lock{m_state_mutex};
    // The keyboard gesture owns finger zero until release. Mouse polling must not lift it.
    if (touch_index == 0 && m_virtual_swipe_active) {
        return;
    }
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    UpdateTouchpadStateLocked(touch_index, touch_down, x, y, timestamp);
    PushStateLocked(timestamp);
}

void GameController::SetVirtualTouchpadButton(VirtualTouchpadButton button, bool is_pressed) {
    const auto index = std::to_underlying(button);
    if (index < 0 || index >= m_virtual_touchpad_buttons.size()) {
        return;
    }

    std::lock_guard lock{m_state_mutex};
    m_virtual_touchpad_buttons[index] = is_pressed;
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    // Left and center share the first finger; left takes priority while both are held.
    // Right always owns the second finger, including when it is the only one held.
    if (button == VirtualTouchpadButton::Right) {
        UpdateTouchpadStateLocked(1, is_pressed, 0.75f, 0.5f, timestamp);
    } else if (!m_virtual_swipe_active) {
        const bool left = m_virtual_touchpad_buttons[0];
        const bool center = m_virtual_touchpad_buttons[1];
        UpdateTouchpadStateLocked(0, left || center, left ? 0.25f : 0.5f, 0.5f, timestamp);
    }
    const bool virtual_click =
        std::ranges::any_of(m_virtual_touchpad_buttons, [](bool down) { return down; });
    m_state.OnButton(OrbisPadButtonDataOffset::TouchPad,
                     m_touchpad_button_pressed || virtual_click || m_virtual_swipe_active);
    // Publish contact and click together, so readers cannot observe a partial update.
    PushStateLocked(timestamp);
}

void GameController::SetVirtualTouchpadSwipeDown(bool is_pressed) {
    SetVirtualTouchpadSwipe(VirtualTouchpadSwipe::Down, is_pressed);
}

void GameController::SetVirtualTouchpadSwipe(VirtualTouchpadSwipe direction, bool is_pressed) {
    const auto index = std::to_underlying(direction);
    if (index < 0 || index >= m_virtual_swipe_buttons.size()) {
        return;
    }
    std::lock_guard lock{m_state_mutex};
    if (m_virtual_swipe_buttons[index] == is_pressed) {
        return; // Key repeat must not restart the gesture or change its contact ID.
    }
    m_virtual_swipe_buttons[index] = is_pressed;
    if (m_virtual_swipe_active && m_virtual_swipe_buttons[std::to_underlying(m_swipe_direction)]) {
        return; // A second direction cannot interrupt a finger that is still held.
    }
    const auto held = std::ranges::find(m_virtual_swipe_buttons, true);
    const bool active = held != m_virtual_swipe_buttons.end();
    if (active) {
        m_swipe_direction = static_cast<VirtualTouchpadSwipe>(held - m_virtual_swipe_buttons.begin());
    }
    const u64 timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    m_virtual_swipe_active = active;
    m_swipe_start_timestamp = active ? timestamp : 0;
    // Start each gesture with a fresh contact, independently of a preceding fixed touch.
    UpdateTouchpadStateLocked(0, false, 0.5f, is_pressed ? 0.1f : 0.9f, timestamp);
    if (!active && (m_virtual_touchpad_buttons[0] || m_virtual_touchpad_buttons[1])) {
        UpdateTouchpadStateLocked(0, true, m_virtual_touchpad_buttons[0] ? 0.25f : 0.5f,
                                 0.5f, timestamp);
    }
    const bool virtual_click =
        std::ranges::any_of(m_virtual_touchpad_buttons, [](bool down) { return down; });
    m_state.OnButton(OrbisPadButtonDataOffset::TouchPad,
                     m_touchpad_button_pressed || virtual_click || m_virtual_swipe_active);
    PushStateLocked(timestamp);
}

void GameController::UpdateTouchpadStateLocked(int touch_index, bool touch_down, float x, float y,
                                               u64 timestamp) {
    const bool was_pressed = m_state.touchpad[0].state || m_state.touchpad[1].state;
    auto& touch = m_state.touchpad[touch_index];
    if (touch_down && !touch.state) {
        // A long-held finger must keep its ID unique even after the 7-bit counter wraps.
        const auto& other = m_state.touchpad[1 - touch_index];
        if (other.state && m_next_touch_id == other.ID) {
            m_next_touch_id = m_next_touch_id == 127 ? 1 : m_next_touch_id + 1;
        }
        touch.ID = m_next_touch_id;
        m_next_touch_id = m_next_touch_id == 127 ? 1 : m_next_touch_id + 1;
    }
    m_state.OnTouchpad(touch_index, touch_down, x, y);
    const bool is_pressed = m_state.touchpad[0].state || m_state.touchpad[1].state;
    if (!was_pressed && is_pressed) {
        m_touch_down_timestamp = timestamp;
    } else if (was_pressed && !is_pressed) {
        m_touch_down_timestamp = 0;
    }
}

void GameController::ConnectController(SDL_Gamepad* pad) {
    std::lock_guard lock{m_state_mutex};
    m_sdl_gamepad = pad;
    if (override_colour) {
        SetLightBarRGB({});
    }
    m_states_queue.Clear();
    if (!m_state.connected) {
        ++m_state.connected_count;
        if (m_state.connected_count == 0) {
            m_state.connected_count = 1;
        }
    }
    m_state.connected = true;
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::DisconnectController() {
    std::lock_guard lock{m_state_mutex};
    m_states_queue.Clear();
    m_sdl_gamepad = nullptr;

    const u8 connected_count = m_state.connected_count;
    m_state = {};
    m_state.connected_count = connected_count;
    std::fill(gyro_buf, gyro_buf + 3, 0.0f);
    std::fill(accel_buf, accel_buf + 3, 0.0f);
    accel_buf[1] = 9.81f;
    m_next_touch_id = 1;
    m_virtual_touchpad_buttons = {};
    m_touchpad_button_pressed = false;
    m_virtual_swipe_active = false;
    m_virtual_swipe_buttons = {};
    m_swipe_start_timestamp = 0;
    m_virtual_motion_buttons = {};
    m_shake_start_timestamp = 0;
    m_orientation_before_sideways = {0, 0, 0, 1};
    m_touch_down_timestamp = 0;
    m_state.connected = false;
    m_last_orientation_update = 0;
    PushStateLocked();
}

void GameController::UpdateOrientationLocked(u64 timestamp) {
    if (m_last_orientation_update == 0 || timestamp <= m_last_orientation_update) {
        m_last_orientation_update = timestamp;
        return;
    }
    const float delta_time =
        static_cast<float>(timestamp - m_last_orientation_update) / 1'000'000.f;
    Libraries::Pad::OrbisFQuaternion orientation{};
    CalculateOrientation(m_state.angularVelocity, delta_time, m_state.orientation, orientation);
    m_state.orientation = orientation;
    m_last_orientation_update = timestamp;
}

void GameController::PushStateLocked(u64 timestamp) {
    if (timestamp == 0) {
        timestamp = Libraries::Kernel::sceKernelGetProcessTime();
    }
    m_state.UpdateAxisSmoothing(timestamp);
    m_state.OnGyro(gyro_buf);
    m_state.OnAccel(accel_buf);
    UpdateOrientationLocked(timestamp);
    ApplyVirtualMotionLocked(timestamp);
    if (m_virtual_swipe_active) {
        // One continuous stroke in 300 ms, then hold the endpoint until key release.
        const u64 elapsed = timestamp >= m_swipe_start_timestamp
                                ? timestamp - m_swipe_start_timestamp : 0;
        const float progress = std::min(static_cast<float>(elapsed) / 300'000.0f, 1.0f);
        float x = 0.5f, y = 0.5f;
        switch (m_swipe_direction) {
        case VirtualTouchpadSwipe::Up: y = 0.9f - 0.8f * progress; break;
        case VirtualTouchpadSwipe::Down: y = 0.1f + 0.8f * progress; break;
        case VirtualTouchpadSwipe::Left: x = 0.9f - 0.8f * progress; break;
        case VirtualTouchpadSwipe::Right: x = 0.1f + 0.8f * progress; break;
        }
        UpdateTouchpadStateLocked(0, true, x, y, timestamp);
    }
    m_state.time = timestamp;
    m_state.touch_time_since_held_down =
        m_touch_down_timestamp == 0 ? 0 : timestamp - m_touch_down_timestamp;
    m_states_queue.Push(m_state);
}

void GameController::SetLightBarRGB(u8 const r, u8 const g, u8 const b) {
    if (override_colour.has_value()) {
        if (m_sdl_gamepad) {
            SDL_SetGamepadLED(m_sdl_gamepad, override_colour->r, override_colour->g,
                              override_colour->b);
        }
        return;
    }
    colour = {r, g, b};
    if (m_sdl_gamepad != nullptr) {
        SDL_SetGamepadLED(m_sdl_gamepad, r, g, b);
    }
}

void GameController::SetLightBarRGB(Colour const c) {
    SetLightBarRGB(c.r, c.g, c.b);
}

Colour GameController::GetLightBarRGB() {
    return colour;
}

void GameController::PollLightColour() {
    if (m_sdl_gamepad != nullptr) {
        SDL_SetGamepadLED(m_sdl_gamepad, colour.r, colour.g, colour.b);
    }
}

void GameControllers::ResetLightbarColors() {
    for (auto& c : controllers) {
        auto const* u = UserManagement.GetUserByID(c->user_id);
        if (!u || !c->m_sdl_gamepad) {
            continue;
        }
        auto const i = u->user_color - 1;
        if (i < 0 || i > 3) {
            continue;
        }
        auto const& col = g_user_colours[i];
        c->override_colour = std::nullopt;
        c->SetLightBarRGB(col);
    }
}

bool GameController::SetVibration(u8 smallMotor, u8 largeMotor) {
    if (m_sdl_gamepad != nullptr) {
        return SDL_RumbleGamepad(m_sdl_gamepad, (smallMotor / 255.0f) * 0xFFFF,
                                 (largeMotor / 255.0f) * 0xFFFF, -1);
    }
    return true;
}

static bool is_first_check = true;

void GameControllers::TryOpenSDLControllers() {
    using namespace Libraries::UserService;
    int controller_count;
    SDL_JoystickID* new_joysticks = SDL_GetGamepads(&controller_count);
    LOG_INFO(Input, "{} controllers are currently connected", controller_count);

    std::unordered_set<SDL_JoystickID> assigned_ids;
    std::array<bool, 4> slot_taken{false, false, false, false};

    for (int i = 0; i < 4; i++) {
        SDL_Gamepad* pad = controllers[i]->m_sdl_gamepad;
        if (pad) {
            SDL_JoystickID id = SDL_GetGamepadID(pad);
            bool still_connected = false;
            for (int j = 0; j < controller_count; j++) {
                if (new_joysticks[j] == id) {
                    still_connected = true;
                    assigned_ids.insert(id);
                    slot_taken[i] = true;
                    break;
                }
            }
            if (!still_connected) {
                SDL_CloseGamepad(pad);
                controllers[i]->DisconnectController();
                controllers[i]->user_id = -1;
                slot_taken[i] = false;
            }
        }
    }

    for (int j = 0; j < controller_count; j++) {
        SDL_JoystickID id = new_joysticks[j];
        if (assigned_ids.contains(id))
            continue;

        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (!pad) {
            continue;
        }

        for (int i = 0; i < 4; i++) {
            if (!slot_taken[i]) {
                auto u = UserManagement.GetUserByPlayerIndex(i + 1);
                if (!u) {
                    LOG_INFO(Input, "User {} not found", i + 1);
                    continue; // for now, if you don't specify who Player N is in the config,
                              // Player N won't be registered at all
                }
                auto* c = controllers[i];
                LOG_INFO(Input, "Gamepad registered for slot {}! Handle: {}", i,
                         SDL_GetGamepadID(pad));
                slot_taken[i] = true;
                c->user_id = u->user_id;
                UserManagement.LoginUser(u, i + 1);
                c->ConnectController(pad);
                if (EmulatorSettings.IsMotionControlsEnabled()) {
                    if (SDL_SetGamepadSensorEnabled(c->m_sdl_gamepad, SDL_SENSOR_GYRO, true)) {
                        const float poll_rate =
                            SDL_GetGamepadSensorDataRate(c->m_sdl_gamepad, SDL_SENSOR_GYRO);
                        LOG_INFO(Input, "Gyro initialized, poll rate: {}", poll_rate);
                    } else {
                        LOG_ERROR(Input, "Failed to initialize gyro controls for gamepad {}",
                                  c->user_id);
                    }
                    if (SDL_SetGamepadSensorEnabled(c->m_sdl_gamepad, SDL_SENSOR_ACCEL, true)) {
                        const float poll_rate =
                            SDL_GetGamepadSensorDataRate(c->m_sdl_gamepad, SDL_SENSOR_ACCEL);
                        LOG_INFO(Input, "Accel initialized, poll rate: {}", poll_rate);
                    } else {
                        LOG_ERROR(Input, "Failed to initialize accel controls for gamepad {}",
                                  c->user_id);
                    }
                }
                break;
            }
        }
    }
    if (is_first_check) [[unlikely]] {
        is_first_check = false;
        if (controller_count == 0) {
            auto u = UserManagement.GetUserByPlayerIndex(1);
            controllers[0]->user_id = u->user_id;
            controllers[0]->ConnectController(nullptr);
            UserManagement.LoginUser(u, 1);
        }
    }
    SDL_free(new_joysticks);
}
u8 GameControllers::GetGamepadIndexFromJoystickId(SDL_JoystickID id) {
    auto g = SDL_GetGamepadFromID(id);
    ASSERT(g != nullptr);
    for (int i = 0; i < 5; i++) {
        if (controllers[i]->m_sdl_gamepad == g) {
            return i;
        }
    }
    // LOG_TRACE(Input, "Gamepad index: {}", index);
    return -1;
}

std::optional<u8> GameControllers::GetControllerIndexFromUserID(s32 user_id) {
    auto const u = UserManagement.GetUserByID(user_id);
    if (!u) {
        return std::nullopt;
    }
    return u->player_index - 1;
}

std::optional<u8> GameControllers::GetControllerIndexFromControllerID(s32 controller_id) {
    if (controller_id < 1 || controller_id > 5) {
        return std::nullopt;
    }
    return controller_id - 1;
}

} // namespace Input
