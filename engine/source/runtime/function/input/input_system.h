#pragma once

#include "runtime/core/math/math.h"

// input_system 是 Game Mode 下输入系统，Editor Mode 对应逻辑在 editor_input_manager 中
// initialize() 函数会注册输入事件回调函数到 window_system 中

namespace Piccolo {
enum class GameCommand : unsigned int {
    forward  = 1 << 0,                 // W
    backward = 1 << 1,                 // S
    left     = 1 << 2,                 // A
    right    = 1 << 3,                 // D
    jump     = 1 << 4,                 // SPACE
    squat    = 1 << 5,                 // not implemented yet
    sprint   = 1 << 6,                 // LEFT SHIFT
    fire     = 1 << 7,                 // not implemented yet
    free_camera = 1 << 8,              // F
    invalid  = (unsigned int)(1 << 31) // lost focus
};

extern unsigned int k_complement_control_command;

class InputSystem {
public:
    void initialize();
    void tick();
    void clear();

public:
    void onKey(int key, int scancode, int action, int mods);
    void onCursorPos(double current_cursor_x, double current_cursor_y);

public:
    Radian getCursorDeltaYaw() const { return m_cursor_delta_yaw; }
    Radian getCursorDeltaPitch() const { return m_cursor_delta_pitch; }
    unsigned int getGameCommand() const { return m_game_command; }

    void resetGameCommand() { m_game_command = 0; }

private:
    void calculateCursorDeltaAngles();

    unsigned int m_game_command {0};

    int m_last_cursor_x {0};
    int m_last_cursor_y {0};

    int m_cursor_delta_x {0};
    int m_cursor_delta_y {0};

    Radian m_cursor_delta_yaw {0};
    Radian m_cursor_delta_pitch {0};
};
} // namespace Piccolo
