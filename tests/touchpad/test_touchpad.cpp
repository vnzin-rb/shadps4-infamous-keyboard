// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "common/elf_info.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "input/controller.h"
#include "input/input_handler.h"
#include "input/input_mouse.h"

extern int touchpad_mode_events;
extern bool capture_gamepad_input;
extern float mouse_test_dx, mouse_test_dy;
extern int gyro_mode_events;
namespace Libraries::Pad {
int ProcessStates(OrbisPadData* data, const Input::State* states, s32 num);
}

namespace {
u64 clock_us = 1'000'000;
int checks = 0;
using Input::GameController;
using Finger = Input::VirtualTouchpadButton;
using Libraries::Pad::OrbisPadButtonDataOffset;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool Click(const Input::State& state) {
    return (std::to_underlying(state.buttonsState) &
            std::to_underlying(OrbisPadButtonDataOffset::TouchPad)) != 0;
}

void Expect(GameController& pad, bool left, bool right, bool click) {
    const auto state = pad.ReadState();
    Check(state.touchpad[0].state == left, "first contact state");
    Check(state.touchpad[1].state == right, "second contact state");
    Check(Click(state) == click, "touchpad click state");
    if (left && right) {
        Check(state.touchpad[0].ID != state.touchpad[1].ID, "contact IDs must differ");
    }
    // Verify the actual guest report packing, including right-only compaction to touch[0].
    auto connected = state;
    connected.connected = true;
    Libraries::Pad::OrbisPadData report{};
    Check(Libraries::Pad::ProcessStates(&report, &connected, 1) == 1, "one packed pad report");
    const auto& data = report.touchData;
    Check(data.touchNum == int(left) + int(right), "guest touchNum");
    if (left) {
        Check(data.touch[0].id == state.touchpad[0].ID && data.touch[0].x == state.touchpad[0].x &&
                  data.touch[0].y == state.touchpad[0].y,
              "first guest contact");
    }
    if (right) {
        const int output_index = left ? 1 : 0;
        Check(data.touch[output_index].id == state.touchpad[1].ID &&
                  data.touch[output_index].x == state.touchpad[1].x &&
                  data.touch[output_index].y == state.touchpad[1].y,
              "second/surviving guest contact");
    }
    Check(report.buttons == state.buttonsState, "guest click/button state");
}

void FirmwareAndCapture() {
    GameController pad;
    pad.SetVirtualTouchpadButton(Finger::Left, true);
    pad.SetVirtualTouchpadButton(Finger::Right, true);
    clock_us += 5000;
    pad.PollState();
    auto state = pad.ReadState();
    state.connected = true;
    Libraries::Pad::OrbisPadData report{};
    Common::ElfInfo::Instance().firmware = Common::ElfInfo::FW_350 + 1;
    Libraries::Pad::ProcessStates(&report, &state, 1);
    Check(report.touchData.time_since_touch_held_down == state.touch_time_since_held_down,
          "new firmware receives held time");
    Common::ElfInfo::Instance().firmware = 0x1700000;
    Libraries::Pad::ProcessStates(&report, &state, 1);
    Check(report.touchData.time_since_touch_held_down == 0,
          "older firmware keeps reserved time zero");
    capture_gamepad_input = true;
    Libraries::Pad::ProcessStates(&report, &state, 1);
    Check(report.touchData.touchNum == 0, "GUI interception blocks contacts");
    capture_gamepad_input = false;
    Expect(pad, true, true, true);
}

void PressReleaseOrders() {
    for (bool right_first : {false, true}) {
        for (bool release_right_first : {false, true}) {
            GameController pad;
            const auto first = right_first ? Finger::Right : Finger::Left;
            const auto second = right_first ? Finger::Left : Finger::Right;
            Expect(pad, false, false, false);
            pad.SetVirtualTouchpadButton(first, true);
            Expect(pad, !right_first, right_first, true);
            pad.SetVirtualTouchpadButton(second, true);
            Expect(pad, true, true, true);
            auto held = pad.ReadState();
            Check(held.touchpad[0].x == 480 && held.touchpad[1].x == 1440,
                  "independent horizontal positions");
            Check(held.touchpad[0].y == 470 && held.touchpad[1].y == 470, "vertical positions");
            for (int i = 0; i < 80; ++i) {
                clock_us += 1000;
                pad.SetVirtualTouchpadButton(first, true); // repeated keydown
                pad.SetVirtualTouchpadButton(second, true);
                pad.PollState();
                const auto state = pad.ReadState();
                Expect(pad, true, true, true);
                Check(state.touchpad[0].ID == held.touchpad[0].ID &&
                          state.touchpad[1].ID == held.touchpad[1].ID,
                      "holding/repeat must preserve IDs");
                Check(state.touch_time_since_held_down > 0, "held time must advance");
            }
            pad.SetVirtualTouchpadButton(release_right_first ? Finger::Right : Finger::Left, false);
            Expect(pad, release_right_first, !release_right_first, true);
            const auto survivor = pad.ReadState();
            const int survivor_index = release_right_first ? 0 : 1;
            Check(survivor.touchpad[survivor_index].ID == held.touchpad[survivor_index].ID,
                  "releasing the other finger must preserve survivor ID");
            pad.SetVirtualTouchpadButton(release_right_first ? Finger::Left : Finger::Right, false);
            Expect(pad, false, false, false);
            Check(pad.ReadState().touch_time_since_held_down == 0, "last release resets held time");
            pad.SetVirtualTouchpadButton(first, true);
            Check(pad.ReadState().touchpad[right_first ? 1 : 0].ID !=
                      held.touchpad[right_first ? 1 : 0].ID,
                  "new press gets a new ID");
        }
    }
}

void CenterOverlap() {
    for (bool left_first : {false, true}) {
        for (bool release_left_first : {false, true}) {
            GameController pad;
            pad.SetVirtualTouchpadButton(left_first ? Finger::Left : Finger::Center, true);
            const auto id = pad.ReadState().touchpad[0].ID;
            pad.SetVirtualTouchpadButton(left_first ? Finger::Center : Finger::Left, true);
            pad.SetVirtualTouchpadButton(Finger::Right, true);
            Expect(pad, true, true, true);
            Check(pad.ReadState().touchpad[0].x == 480, "left has priority over center");
            pad.SetVirtualTouchpadButton(release_left_first ? Finger::Left : Finger::Center, false);
            Expect(pad, true, true, true);
            Check(pad.ReadState().touchpad[0].ID == id, "center overlap must preserve contact ID");
            Check(pad.ReadState().touchpad[0].x == (release_left_first ? 960 : 480),
                  "remaining center/left position");
            pad.SetVirtualTouchpadButton(release_left_first ? Finger::Center : Finger::Left, false);
            Expect(pad, false, true, true);
            pad.SetVirtualTouchpadButton(Finger::Right, false);
            Expect(pad, false, false, false);
        }
    }
}

void PhysicalClickAndDisconnect() {
    GameController pad;
    pad.SetVirtualTouchpadButton(Finger::Left, true);
    pad.Button(OrbisPadButtonDataOffset::TouchPad, false);
    Expect(pad, true, false, true);
    pad.Button(OrbisPadButtonDataOffset::TouchPad, true);
    pad.SetVirtualTouchpadButton(Finger::Left, false);
    Expect(pad, false, false, true);
    pad.Button(OrbisPadButtonDataOffset::TouchPad, false);
    Expect(pad, false, false, false);
    pad.SetVirtualTouchpadButton(Finger::Left, true);
    pad.SetVirtualTouchpadButton(Finger::Right, true);
    pad.DisconnectController();
    Expect(pad, false, false, false);
    pad.SetVirtualTouchpadButton(Finger::Right, true);
    Expect(pad, false, true, true);
    pad.SetVirtualTouchpadButton(Finger::Right, false);
    Expect(pad, false, false, false);
}

void UniqueIdsAfterWrap() {
    for (bool right_held : {false, true}) {
        GameController pad;
        const auto held = right_held ? Finger::Right : Finger::Left;
        const auto cycling = right_held ? Finger::Left : Finger::Right;
        pad.SetVirtualTouchpadButton(held, true);
        for (int i = 0; i < 300; ++i) {
            pad.SetVirtualTouchpadButton(cycling, true);
            Expect(pad, true, true, true);
            const auto state = pad.ReadState();
            for (const auto& touch : state.touchpad) {
                Check(touch.ID >= 1 && touch.ID <= 127, "IDs stay within 7 bits");
            }
            pad.SetVirtualTouchpadButton(cycling, false);
            Expect(pad, !right_held, right_held, true);
        }
    }
}

void QueuedReportsAndBounds() {
    GameController pad;
    pad.ConnectController(nullptr);
    pad.SetVirtualTouchpadButton(Finger::Left, true);
    pad.SetVirtualTouchpadButton(Finger::Right, true);
    pad.SetVirtualTouchpadButton(Finger::Left, false);
    pad.SetVirtualTouchpadButton(Finger::Right, false);
    Input::State reports[64];
    const auto count = pad.ReadStates(reports, 64);
    Check(count == 5, "one coherent report per transition plus initial connection");
    const int expected_counts[] = {0, 1, 2, 1, 0};
    for (int i = 0; i < count; ++i) {
        const auto& s = reports[i];
        Check(int(s.touchpad[0].state) + int(s.touchpad[1].state) == expected_counts[i],
              "queued contact counts follow 0-1-2-1-0");
        Check(Click(s) == (expected_counts[i] != 0), "contact/click report is atomic");
    }
    Check(pad.ReadStates(reports, 64) == 0, "queue drains");
    pad.SetTouchpadState(-1, true, 0.5f, 0.5f);
    pad.SetTouchpadState(2, true, 0.5f, 0.5f);
    pad.SetVirtualTouchpadButton(static_cast<Finger>(-1), true);
    pad.SetVirtualTouchpadButton(static_cast<Finger>(3), true);
    Expect(pad, false, false, false);
    Check(pad.ReadStates(reports, 64) == 0, "invalid indices publish nothing");
    // The physical SDL/mouse entry point still supports two independent contacts.
    pad.SetTouchpadState(0, true, 0.1f, 0.2f);
    pad.SetTouchpadState(1, true, 0.8f, 0.9f);
    Expect(pad, true, true, false);
    pad.SetTouchpadState(0, false, 0.1f, 0.2f);
    Expect(pad, false, true, false);
}

void SendKey(SDL_Keycode key, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.key = key;
        event.key.down = down;
        if (Input::UpdatePressedKeys(Input::InputBinding::GetInputEventFromSDLEvent(event))) {
            Input::ActivateOutputsFromInputs();
        }
}

void KeyboardPipeline() {
    auto& pad = *Input::ControllerOutput::controllers[0];
    for (bool unified : {false, true}) {
        EmulatorSettings.SetUseUnifiedInputConfig(unified);
        std::cerr << "Parsing profile, unified=" << unified << '\n';
        Input::ParseInputConfig("CUSA00004");
        std::cerr << "Profile parsed\n";
        for (bool right_first : {false, true}) {
            for (bool right_release_first : {false, true}) {
                const auto first = right_first ? SDLK_PAGEDOWN : SDLK_DELETE;
                const auto second = right_first ? SDLK_DELETE : SDLK_PAGEDOWN;
                Expect(pad, false, false, false);
                SendKey(first, true);
                Expect(pad, !right_first, right_first, true);
                SendKey(second, true);
                Expect(pad, true, true, true);
                const auto held = pad.ReadState();
                for (int i = 0; i < 10; ++i) {
                    SendKey(first, true);
                    SendKey(second, true);
                    Input::ActivateOutputsFromInputs();
                    Expect(pad, true, true, true);
                    Check(pad.ReadState().touchpad[0].ID == held.touchpad[0].ID &&
                              pad.ReadState().touchpad[1].ID == held.touchpad[1].ID,
                          "keyboard repeat retains contact IDs");
                }
                SendKey(right_release_first ? SDLK_PAGEDOWN : SDLK_DELETE, false);
                Expect(pad, right_release_first, !right_release_first, true);
                SendKey(right_release_first ? SDLK_DELETE : SDLK_PAGEDOWN, false);
                Expect(pad, false, false, false);
                Check(touchpad_mode_events == 0, "Delete must not toggle mouse touchpad mode");
            }
        }
    }
    SendKey(SDLK_INSERT, true);
    Check(touchpad_mode_events == 1, "Insert preserves the mouse touchpad hotkey");
    SendKey(SDLK_INSERT, false);
    Expect(pad, false, false, false);
}

void Near(float actual, float expected, const char* message) {
    Check(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
}

void CheckMotionReport(GameController& pad) {
    auto state = pad.ReadState();
    state.connected = true;
    Libraries::Pad::OrbisPadData report{};
    Libraries::Pad::ProcessStates(&report, &state, 1);
    Near(report.acceleration.x, state.acceleration.x * 0.098f, "guest acceleration x");
    Near(report.acceleration.y, state.acceleration.y * 0.098f, "guest acceleration y");
    Near(report.acceleration.z, state.acceleration.z * 0.098f, "guest acceleration z");
    Near(report.angularVelocity.x, state.angularVelocity.x, "guest gyro x");
    Near(report.angularVelocity.y, state.angularVelocity.y, "guest gyro y");
    Near(report.angularVelocity.z, state.angularVelocity.z, "guest gyro z");
    Near(report.orientation.x, state.orientation.x, "guest orientation x");
    Near(report.orientation.y, state.orientation.y, "guest orientation y");
    Near(report.orientation.z, state.orientation.z, "guest orientation z");
    Near(report.orientation.w, state.orientation.w, "guest orientation w");
    Near(state.orientation.x * state.orientation.x + state.orientation.y * state.orientation.y +
             state.orientation.z * state.orientation.z + state.orientation.w * state.orientation.w,
         1, "unit orientation quaternion");
}

void MotionPressHoldRelease() {
    using Motion = Input::VirtualMotionButton;
    for (bool shake_first : {false, true}) {
        for (bool shake_release_first : {false, true}) {
            GameController pad;
            pad.ConnectController(nullptr);
            pad.SetVirtualTouchpadButton(Finger::Left, true);
            pad.SetVirtualTouchpadButton(Finger::Right, true);
            const float accel[3] = {1, 8, 2};
            const float zero[3] = {0, 0, 0};
            pad.UpdateMotion(zero, accel);
            const auto first = shake_first ? Motion::Shake : Motion::Sideways;
            const auto second = shake_first ? Motion::Sideways : Motion::Shake;
            pad.SetVirtualMotionButton(first, true);
            pad.SetVirtualMotionButton(second, true);
            Near(pad.ReadState().orientation.z, std::sqrt(0.5f), "sideways quaternion");
            Near(pad.ReadState().acceleration.x, 9.81f, "sideways gravity x");
            Near(pad.ReadState().acceleration.y, 0, "sideways gravity y");
            // 5 Hz shake: +3g and -3g relative to gravity at quarter periods.
            for (int cycle = 0; cycle < 5; ++cycle) {
                clock_us += 50'000;
                pad.SetVirtualMotionButton(Motion::Shake, true); // repeated keydown
                pad.SetVirtualMotionButton(Motion::Sideways, true);
                pad.PollState();
                Near(pad.ReadState().acceleration.x, 39.81f, "positive shake peak");
                const auto peak = pad.ReadState();
                for (int i = 0; i < 10; ++i) {
                    pad.PollState();
                    Near(pad.ReadState().acceleration.x, peak.acceleration.x,
                         "same timestamp must not accumulate shaking");
                }
                CheckMotionReport(pad);
                Expect(pad, true, true, true);
                clock_us += 100'000;
                pad.PollState();
                Near(pad.ReadState().acceleration.x, -20.19f, "negative shake peak");
                CheckMotionReport(pad);
                clock_us += 50'000;
                pad.PollState();
                Near(pad.ReadState().acceleration.x, 9.81f, "whole shake cycle");
            }
            const auto release_first = shake_release_first ? Motion::Shake : Motion::Sideways;
            const auto release_second = shake_release_first ? Motion::Sideways : Motion::Shake;
            pad.SetVirtualMotionButton(release_first, false);
            Near(pad.ReadState().orientation.z, shake_release_first ? std::sqrt(0.5f) : 0,
                 "release retains only the remaining orientation command");
            clock_us += 50'000;
            pad.PollState();
            Near(pad.ReadState().acceleration.x, shake_release_first ? 9.81f : 31,
                 "release retains only the remaining shake command");
            // Raw sensors can keep updating while the virtual command is held.
            const float latest_accel[3] = {2, 7, 3};
            pad.UpdateAcceleration(latest_accel);
            pad.SetVirtualMotionButton(release_second, false);
            Near(pad.ReadState().acceleration.x, 2, "restore latest raw sensor x");
            Near(pad.ReadState().acceleration.y, 7, "restore latest raw sensor y");
            Near(pad.ReadState().acceleration.z, 3, "restore latest raw sensor z");
            Near(pad.ReadState().orientation.w, 1, "restore original orientation");
            Expect(pad, true, true, true);
            CheckMotionReport(pad);
            pad.SetVirtualMotionButton(Motion::Shake, true);
            clock_us += 50'000;
            pad.PollState();
            Near(pad.ReadState().acceleration.x, 32, "new shake press restarts phase");
            pad.SetVirtualMotionButton(Motion::Sideways, true);
            pad.ResetOrientation();
            Near(pad.ReadState().orientation.z, std::sqrt(0.5f), "reset keeps held sideways pose");
            pad.DisconnectController();
            clock_us += 50'000;
            pad.ConnectController(nullptr);
            Near(pad.ReadState().acceleration.x, 0, "disconnect clears virtual shake");
            Near(pad.ReadState().acceleration.y, 9.81f, "disconnect restores gravity");
            Near(pad.ReadState().orientation.w, 1, "disconnect clears virtual orientation");
            Input::State reports[64];
            pad.ReadStates(reports, 64);
            pad.SetVirtualMotionButton(static_cast<Motion>(-1), true);
            pad.SetVirtualMotionButton(static_cast<Motion>(2), true);
            Check(pad.ReadStates(reports, 64) == 0, "invalid motion command publishes nothing");
        }
    }
}

void MotionKeyboardAndMouse() {
    auto& pad = *Input::ControllerOutput::controllers[0];
    for (bool unified : {false, true}) {
        EmulatorSettings.SetUseUnifiedInputConfig(unified);
        Input::ParseInputConfig("CUSA00004");
        pad.DisconnectController();
        pad.ConnectController(nullptr);
        SendKey(SDLK_DELETE, true);
        SendKey(SDLK_PAGEDOWN, true);
        SendKey(SDLK_G, true);
        SendKey(SDLK_H, true);
        clock_us += 50'000;
        pad.PollState();
        Near(pad.ReadState().acceleration.x, 39.81f, "G + H profile sends shake and sideways");
        SendKey(SDLK_H, false);
        Near(pad.ReadState().acceleration.x, 9.81f, "H release stops shake immediately");
        const int gyro_events_before = gyro_mode_events;
        SendKey(SDLK_F6, true);
        SendKey(SDLK_F6, false);
        Check(gyro_mode_events == gyro_events_before + 1, "F6 toggles real gyro mouse mode");
        SendKey(SDLK_O, true);
        Check((std::to_underlying(pad.ReadState().buttonsState) &
               std::to_underlying(OrbisPadButtonDataOffset::R2)) != 0, "O holds R2 spray");
        for (const auto movement : {std::pair{50.f, 0.f}, std::pair{0.f, 50.f},
                                    std::pair{-50.f, 0.f}, std::pair{0.f, -50.f}}) {
            const auto previous = pad.ReadState().orientation;
            mouse_test_dx = movement.first;
            mouse_test_dy = movement.second;
            Input::MousePolling(&pad, 1, 4);
            clock_us += 20'000;
            pad.PollState();
            const auto state = pad.ReadState();
            Check(std::abs(state.orientation.x - previous.x) +
                      std::abs(state.orientation.y - previous.y) +
                      std::abs(state.orientation.z - previous.z) > 0.0001f,
                  "mouse movement changes aiming orientation in all four directions");
            Near(std::sqrt(state.acceleration.x * state.acceleration.x +
                           state.acceleration.y * state.acceleration.y +
                           state.acceleration.z * state.acceleration.z),
                 9.81f, "aiming keeps rotated gravity magnitude");
            CheckMotionReport(pad);
            Expect(pad, true, true, true);
        }
        SendKey(SDLK_F6, true);
        SendKey(SDLK_F6, false);
        Near(pad.ReadState().angularVelocity.x, 0, "turning F6 off clears mouse gyro x");
        Near(pad.ReadState().angularVelocity.y, 0, "turning F6 off clears mouse gyro y");
        Near(pad.ReadState().angularVelocity.z, 0, "turning F6 off clears mouse gyro z");
        SendKey(SDLK_O, false);
        Check((std::to_underlying(pad.ReadState().buttonsState) &
               std::to_underlying(OrbisPadButtonDataOffset::R2)) == 0, "O release stops spray");
        SendKey(SDLK_H, true);
        Input::ReleaseKeyboardMouseInputs(); // Same function used on window focus loss.
        Near(pad.ReadState().acceleration.x, 0, "focus loss stops shake and sideways");
        Near(pad.ReadState().orientation.w, 1, "focus loss restores orientation");
        Expect(pad, false, false, false);
        SendKey(SDLK_G, true);
        SendKey(SDLK_H, true);
        Input::ParseInputConfig("CUSA00004");
        Near(pad.ReadState().acceleration.x, 0, "reload releases previous motion commands");
        Near(pad.ReadState().orientation.w, 1, "reload releases previous sideways pose");
    }
    // Commands are configurable through the actual INI parser, not hardcoded to G/H.
    const auto path = std::filesystem::path(TEST_USER_DIR) / "input_config/default.ini";
    std::ifstream stream(path);
    const std::string original((std::istreambuf_iterator<char>(stream)), {});
    stream.close();
    auto remapped = original;
    remapped.replace(remapped.find("motion_shake = h"), 16, "motion_shake = t");
    std::ofstream(path) << remapped;
    Input::ParseInputConfig("CUSA00004");
    std::ofstream(path) << original;
    SendKey(SDLK_H, true);
    clock_us += 50'000;
    pad.PollState();
    Near(pad.ReadState().acceleration.x, 0, "old shake binding is inactive after remap");
    SendKey(SDLK_H, false);
    SendKey(SDLK_T, true);
    clock_us += 50'000;
    pad.PollState();
    Near(pad.ReadState().acceleration.x, 30, "remapped shake binding works");
    Input::ReleaseKeyboardMouseInputs();
}

void MotionTimingAndIsolation() {
    using Motion = Input::VirtualMotionButton;
    GameController pad, other;
    pad.ConnectController(nullptr);
    other.ConnectController(nullptr);
    const float gyro[3] = {0.5f, -0.25f, 0.1f};
    const float rest[3] = {0, 9.81f, 0};
    pad.UpdateMotion(gyro, rest);
    clock_us += 40'000;
    pad.PollState();
    const auto before = pad.ReadState();
    pad.SetVirtualMotionButton(Motion::Sideways, true);
    clock_us += 40'000;
    pad.PollState();
    Check(std::abs(pad.ReadState().orientation.x) > 0.001f,
          "live raw gyro continues aiming during sideways override");
    pad.SetVirtualMotionButton(Motion::Sideways, false);
    Near(pad.ReadState().orientation.x, before.orientation.x,
         "release restores preexisting nonidentity orientation x");
    Near(pad.ReadState().orientation.w, before.orientation.w,
         "release restores preexisting nonidentity orientation w");
    pad.SetVirtualMotionButton(Motion::Shake, true);
    const u64 start = clock_us;
    Input::State queue[64];
    pad.ReadStates(queue, 64);
    for (u64 elapsed : {7'000, 25'000, 50'000, 91'000, 150'000, 999'999, 10'050'000}) {
        clock_us = start + elapsed;
        pad.PollState();
        const float expected = 30 * std::sin(2 * 3.14159265358979323846f *
                                            float(elapsed % 200'000) / 200'000);
        Near(pad.ReadState().acceleration.x, expected, "shake independent of irregular polling");
        Near(pad.ReadState().angularVelocity.x, gyro[0], "shake preserves raw gyro");
        CheckMotionReport(pad);
    }
    const int count = pad.ReadStates(queue, 64);
    Check(count == 7, "one queued report per motion poll");
    Libraries::Pad::OrbisPadData guest[64];
    Check(Libraries::Pad::ProcessStates(guest, queue, count) == count, "pack queued motion reports");
    for (int i = 0; i < count; ++i) {
        Near(guest[i].acceleration.x, queue[i].acceleration.x * 0.098f,
             "history preserves individual shake samples");
    }
    pad.SetVirtualMotionButton(Motion::Shake, false);
    Near(pad.ReadState().acceleration.x, 0, "shake release restores raw acceleration");
    Near(pad.ReadState().angularVelocity.x, gyro[0], "shake release preserves live raw gyro");
    other.PollState();
    Near(other.ReadState().acceleration.x, 0, "virtual motion does not leak to another controller");
    Near(other.ReadState().orientation.w, 1, "other controller remains neutral");
}
void DownSwipeTimingAndOwnership() {
    GameController pad, other;
    pad.ConnectController(nullptr);
    pad.SetVirtualTouchpadSwipeDown(true);
    const auto start = clock_us;
    const auto id = pad.ReadState().touchpad[0].ID;
    Expect(pad, true, false, true);
    Check(pad.ReadState().touchpad[0].x == 960, "downward swipe starts at horizontal center");
    Check(pad.ReadState().touchpad[0].y < 100, "downward swipe starts at top");
    u16 previous_y = 0;
    for (u64 elapsed : {4'000, 17'000, 80'000, 150'000, 299'000, 300'000, 600'000}) {
        clock_us = start + elapsed;
        pad.SetVirtualTouchpadSwipeDown(true); // repeated keydown does not restart
        pad.SetTouchpadState(0, false, 0, 0); // mouse polling cannot lift the gesture
        pad.Button(OrbisPadButtonDataOffset::TouchPad, false);
        pad.SetVirtualTouchpadButton(Finger::Center, true);
        pad.SetVirtualTouchpadButton(Finger::Center, false);
        pad.PollState();
        const auto state = pad.ReadState();
        const float progress = std::min(float(elapsed) / 300'000.f, 1.f);
        const auto expected_y = static_cast<u16>((0.1f + 0.8f * progress) * 941);
        Check(state.touchpad[0].y == expected_y, "swipe follows elapsed time, not event count");
        Check(state.touchpad[0].y >= previous_y, "downward swipe never moves upward");
        Check(state.touchpad[0].x == 960, "downward swipe has no horizontal movement");
        Check(state.touchpad[0].ID == id, "swipe keeps one contact ID through mouse/key events");
        previous_y = state.touchpad[0].y;
        Expect(pad, true, false, true); // also checks guest-visible coordinates
    }
    Check(previous_y > 840, "completed downward swipe reaches bottom");
    other.PollState();
    Expect(other, false, false, false);
    pad.SetVirtualTouchpadSwipeDown(false);
    Expect(pad, false, false, false);
    pad.SetVirtualTouchpadSwipeDown(true);
    Check(pad.ReadState().touchpad[0].ID != id, "new swipe gets a new contact ID");
    clock_us += 20'000;
    pad.PollState();
    pad.SetVirtualTouchpadSwipeDown(false);
    clock_us += 400'000;
    pad.PollState();
    Expect(pad, false, false, false); // early release must not resume a delayed stroke

    pad.SetVirtualTouchpadButton(Finger::Left, true);
    pad.SetVirtualTouchpadButton(Finger::Right, true);
    const auto right_id = pad.ReadState().touchpad[1].ID;
    pad.SetVirtualTouchpadSwipeDown(true);
    clock_us += 300'000;
    pad.PollState();
    Expect(pad, true, true, true);
    Check(pad.ReadState().touchpad[1].ID == right_id, "swipe preserves second finger");
    pad.SetVirtualTouchpadSwipeDown(false);
    Expect(pad, true, true, true);
    Check(pad.ReadState().touchpad[0].x == 480 && pad.ReadState().touchpad[0].y == 470,
          "swipe release restores held Delete contact");
    pad.SetVirtualTouchpadButton(Finger::Left, false);
    pad.SetVirtualTouchpadButton(Finger::Right, false);
    pad.Button(OrbisPadButtonDataOffset::TouchPad, true);
    pad.SetVirtualTouchpadSwipeDown(true);
    pad.SetVirtualTouchpadSwipeDown(false);
    Expect(pad, false, false, true);
    pad.Button(OrbisPadButtonDataOffset::TouchPad, false);
    pad.SetVirtualTouchpadSwipeDown(true);
    pad.DisconnectController();
    pad.ConnectController(nullptr);
    clock_us += 400'000;
    pad.PollState();
    Expect(pad, false, false, false);
}

void DownSwipeKeyboardPipeline() {
    auto& pad = *Input::ControllerOutput::controllers[0];
    Input::ReleaseKeyboardMouseInputs();
    for (bool unified : {false, true}) {
        EmulatorSettings.SetUseUnifiedInputConfig(unified);
        Input::ParseInputConfig("CUSA00004");
        pad.DisconnectController();
        pad.ConnectController(nullptr);
        SendKey(SDLK_P, true);
        Expect(pad, true, false, true);
        const auto id = pad.ReadState().touchpad[0].ID;
        clock_us += 150'000;
        SendKey(SDLK_P, true);
        pad.PollState();
        Check(pad.ReadState().touchpad[0].y == 470, "P holds a half-completed downward swipe");
        clock_us += 150'000;
        pad.PollState();
        Check(pad.ReadState().touchpad[0].y > 840, "P completes the downward swipe");
        Check(pad.ReadState().touchpad[0].ID == id, "P repeat preserves swipe ID");
        SendKey(SDLK_P, false);
        Expect(pad, false, false, false);
        SendKey(SDLK_P, true);
        Input::ReleaseKeyboardMouseInputs();
        clock_us += 400'000;
        pad.PollState();
        Expect(pad, false, false, false); // alt-tab/focus loss cannot leave P held
    }
}

void AllSwipeDirections() {
    using Direction = Input::VirtualTouchpadSwipe;
    for (const auto direction : {Direction::Up, Direction::Down, Direction::Left, Direction::Right}) {
        GameController pad;
        pad.ConnectController(nullptr);
        pad.SetVirtualTouchpadSwipe(direction, true);
        const auto start = clock_us;
        const auto initial = pad.ReadState().touchpad[0];
        for (u64 elapsed : {4'000, 80'000, 150'000, 299'000, 300'000, 600'000}) {
            clock_us = start + elapsed;
            pad.PollState();
            const auto finger = pad.ReadState().touchpad[0];
            const float progress = std::min(float(elapsed) / 300'000.f, 1.f);
            const float decreasing = 0.9f - 0.8f * progress;
            const float increasing = 0.1f + 0.8f * progress;
            const float x = direction == Direction::Left ? decreasing :
                            direction == Direction::Right ? increasing : 0.5f;
            const float y = direction == Direction::Up ? decreasing :
                            direction == Direction::Down ? increasing : 0.5f;
            Check(finger.x == static_cast<u16>(1920 * x) && finger.y == static_cast<u16>(941 * y),
                  "all four swipe directions follow the correct guest coordinates");
            Check(finger.ID == initial.ID, "each directional gesture has one continuous contact");
            Expect(pad, true, false, true);
        }
        pad.SetVirtualTouchpadSwipe(direction, false);
        Expect(pad, false, false, false);
    }
    GameController pad;
    pad.SetVirtualTouchpadSwipe(Direction::Up, true);
    const auto first_id = pad.ReadState().touchpad[0].ID;
    clock_us += 100'000;
    pad.SetVirtualTouchpadSwipe(Direction::Right, true);
    pad.PollState();
    Check(pad.ReadState().touchpad[0].ID == first_id, "second direction does not interrupt held swipe");
    pad.SetVirtualTouchpadSwipe(Direction::Up, false);
    Check(pad.ReadState().touchpad[0].ID != first_id, "next held direction starts a fresh gesture");
    clock_us += 300'000;
    pad.PollState();
    Check(pad.ReadState().touchpad[0].x > 1700 && pad.ReadState().touchpad[0].y == 470,
          "releasing the first direction starts the remaining rightward swipe");
    pad.SetVirtualTouchpadSwipe(Direction::Right, false);
    Expect(pad, false, false, false);

    auto& keyboard = *Input::ControllerOutput::controllers[0];
    Input::ReleaseKeyboardMouseInputs();
    for (bool unified : {false, true}) {
        EmulatorSettings.SetUseUnifiedInputConfig(unified);
        Input::ParseInputConfig("CUSA00004");
        for (auto key : {SDLK_1, SDLK_2, SDLK_3, SDLK_4}) {
            SendKey(key, true);
            const auto initial = keyboard.ReadState().touchpad[0];
            clock_us += 300'000;
            keyboard.PollState();
            const auto final = keyboard.ReadState().touchpad[0];
            Check(final.ID == initial.ID, "number keys preserve swipe contact ID");
            Check((key == SDLK_1 && final.y < initial.y && final.x == initial.x) ||
                      (key == SDLK_2 && final.y > initial.y && final.x == initial.x) ||
                      (key == SDLK_3 && final.x < initial.x && final.y == initial.y) ||
                      (key == SDLK_4 && final.x > initial.x && final.y == initial.y),
                  "number keys 1/2/3/4 map to up/down/left/right");
            Expect(keyboard, true, false, true);
            SendKey(key, false);
            Expect(keyboard, false, false, false);
        }
        SendKey(SDLK_P, true);
        const auto alias_id = keyboard.ReadState().touchpad[0].ID;
        clock_us += 150'000;
        SendKey(SDLK_2, true);
        SendKey(SDLK_P, false);
        keyboard.PollState();
        Check(keyboard.ReadState().touchpad[0].ID == alias_id &&
                  keyboard.ReadState().touchpad[0].y == 470,
              "P and 2 aliases do not restart or prematurely release a downward swipe");
        SendKey(SDLK_2, false);
        Expect(keyboard, false, false, false);
    }
}
} // namespace

namespace Libraries::Kernel {
u64 PS4_SYSV_ABI sceKernelGetProcessTime() {
    return clock_us;
}
} // namespace Libraries::Kernel

// No hardware access or user-directory initialization in this test executable.
extern "C" bool SDL_SetGamepadLED(SDL_Gamepad*, Uint8, Uint8, Uint8) {
    return true;
}

int main() {
    try {
        std::cerr << "Running press/hold/release regression\n";
        PressReleaseOrders();
        CenterOverlap();
        PhysicalClickAndDisconnect();
        UniqueIdsAfterWrap();
        QueuedReportsAndBounds();
        FirmwareAndCapture();
        std::cerr << "Running real keyboard binding pipeline\n";
        KeyboardPipeline();
        std::cerr << "Running virtual motion and actual keyboard/mouse pipeline\n";
        MotionPressHoldRelease();
        MotionKeyboardAndMouse();
        MotionTimingAndIsolation();
        std::cerr << "Running timed downward swipe and real keyboard pipeline\n";
        DownSwipeTimingAndOwnership();
        DownSwipeKeyboardPipeline();
        AllSwipeDirections();
        std::cout << "PASS: " << checks << " checks across 13 regression scenarios\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
