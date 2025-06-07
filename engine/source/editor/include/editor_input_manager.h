#pragma once

#include "runtime/core/math/vector2.h"

#include "runtime/function/render/render_camera.h"

#include <vector>

// editor_input_manager 是 Editor Mode 下的输入系统，Game Mode 对应逻辑在 input_system 中
// initialize() 函数会注册输入事件回调函数到 window_system 中

namespace Piccolo {
class PiccoloEditor;

enum class EditorCommand : unsigned int {
    camera_left      = 1 << 0,  // A
    camera_back      = 1 << 1,  // S
    camera_foward    = 1 << 2,  // W
    camera_right     = 1 << 3,  // D
    camera_up        = 1 << 4,  // Q
    camera_down      = 1 << 5,  // E
    translation_mode = 1 << 6,  // T
    rotation_mode    = 1 << 7,  // R
    scale_mode       = 1 << 8,  // C
    sprint           = 1 << 9,  // Left shift
    delete_object    = 1 << 10, // Delete
};

class EditorInputManager {
public:
    void initialize();
    void tick();

public:
    void onKey(int key, int scancode, int action, int mods);
    void onReset();
    void onCursorPos(double xpos, double ypos);
    void onCursorEnter(int entered);
    void onScroll(double xoffset, double yoffset);
    void onMouseButtonClicked(int key, int action);
    void onWindowClosed();

public:
    Vector2 getEngineWindowPos() const { return m_engine_window_pos; };
    Vector2 getEngineWindowSize() const { return m_engine_window_size; };
    float getCameraSpeed() const { return m_camera_speed; };
    unsigned int getEditorCommand() const { return m_editor_command; }

    void setEngineWindowPos(Vector2 new_window_pos) { m_engine_window_pos = new_window_pos; };
    void setEngineWindowSize(Vector2 new_window_size) { m_engine_window_size = new_window_size; };
    void resetEditorCommand() { m_editor_command = 0; }

private:
    void updateCursorOnAxis(Vector2 cursor_uv);
    bool isCursorInRect(Vector2 pos, Vector2 size) const;

    Vector2 m_engine_window_pos {0.0f, 0.0f};
    Vector2 m_engine_window_size {1280.0f, 768.0f};
    float m_mouse_x {0.0f};
    float m_mouse_y {0.0f};
    float m_camera_speed {0.04f};

    size_t m_cursor_on_axis {3};
    unsigned int m_editor_command {0};
};
} // namespace Piccolo
