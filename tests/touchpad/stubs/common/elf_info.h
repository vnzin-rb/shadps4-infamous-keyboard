// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>
#include "common/types.h"

// Only game metadata is mocked. Controller, bindings, pad API and report structures are real.
namespace Common {
class ElfInfo {
public:
    static constexpr u32 FW_350 = 0x3500000;
    u32 firmware = 0x1700000;
    static ElfInfo& Instance() {
        static ElfInfo info;
        return info;
    }
    std::string_view GameSerial() const {
        return "CUSA00004";
    }
    u32 FirmwareVer() const {
        return firmware;
    }
};
} // namespace Common
