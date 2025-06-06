#include "editor/include/editor_input_manager.h"

#include "editor/include/editor.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor_scene_manager.h"

#include "runtime/engine.h"
#include "runtime/core/base/macro.h"
#include "runtime/function/framework/level/level.h"
#include "runtime/function/framework/world/world_manager.h"
#include "runtime/function/global/global_context.h"
#include "runtime/function/input/input_system.h"

#include "runtime/function/render/render_camera.h"
#include "runtime/function/render/render_system.h"
#include "runtime/function/render/window_system.h"

namespace Piccolo {
void EditorInputManager::initialize() {
    std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;
    ASSERT(window_system);

    // window_system->registerOnResetFunc(std::bind(&EditorInputManager::onReset, this));
    window_system->registerOnCursorPosFunc(std::bind(&EditorInputManager::onCursorPos, this,
                                           std::placeholders::_1, std::placeholders::_2));
    window_system->registerOnCursorEnterFunc(std::bind(&EditorInputManager::onCursorEnter, this, std::placeholders::_1));
    window_system->registerOnScrollFunc(std::bind(&EditorInputManager::onScroll, this,
                                        std::placeholders::_1, std::placeholders::_2));
    window_system->registerOnMouseButtonFunc(std::bind(&EditorInputManager::onMouseButtonClicked, this,
                                             std::placeholders::_1, std::placeholders::_2));
    window_system->registerOnWindowCloseFunc(std::bind(&EditorInputManager::onWindowClosed, this));
    window_system->registerOnKeyFunc(std::bind(&EditorInputManager::onKey, this,
                                     std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

void EditorInputManager::tick() {
    float           camera_speed  = m_camera_speed;
    std::shared_ptr editor_camera = g_editor_global_context.m_scene_manager->getEditorCamera();
    Quaternion      camera_rotate = editor_camera->rotation().inverse();
    Vector3         camera_relative_pos(0, 0, 0);

    if ((unsigned int)EditorCommand::camera_foward & m_editor_command) {
        camera_relative_pos += camera_rotate * Vector3 {0, camera_speed, 0};
    }
    if ((unsigned int)EditorCommand::camera_back & m_editor_command) {
        camera_relative_pos += camera_rotate * Vector3 {0, -camera_speed, 0};
    }
    if ((unsigned int)EditorCommand::camera_left & m_editor_command) {
        camera_relative_pos += camera_rotate * Vector3 {-camera_speed, 0, 0};
    }
    if ((unsigned int)EditorCommand::camera_right & m_editor_command) {
        camera_relative_pos += camera_rotate * Vector3 {camera_speed, 0, 0};
    }
    if ((unsigned int)EditorCommand::camera_up & m_editor_command) {
        camera_relative_pos += Vector3 {0, 0, camera_speed};
    }
    if ((unsigned int)EditorCommand::camera_down & m_editor_command) {
        camera_relative_pos += Vector3 {0, 0, -camera_speed};
    }
    if ((unsigned int)EditorCommand::delete_object & m_editor_command)
        g_editor_global_context.m_scene_manager->onDeleteSelectedGObject();

    editor_camera->move(camera_relative_pos);
}

void EditorInputManager::onKey(int key, int scancode, int action, int mods) {
    if (!g_is_editor_mode) // 只在 Editor Mode 下处理按键事件
        return;

    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_A:
            m_editor_command |= (unsigned int)EditorCommand::camera_left;
            break;
        case GLFW_KEY_S:
            m_editor_command |= (unsigned int)EditorCommand::camera_back;
            break;
        case GLFW_KEY_W:
            m_editor_command |= (unsigned int)EditorCommand::camera_foward;
            break;
        case GLFW_KEY_D:
            m_editor_command |= (unsigned int)EditorCommand::camera_right;
            break;
        case GLFW_KEY_Q:
            m_editor_command |= (unsigned int)EditorCommand::camera_up;
            break;
        case GLFW_KEY_E:
            m_editor_command |= (unsigned int)EditorCommand::camera_down;
            break;
        case GLFW_KEY_T:
            m_editor_command |= (unsigned int)EditorCommand::translation_mode;
            break;
        case GLFW_KEY_R:
            m_editor_command |= (unsigned int)EditorCommand::rotation_mode;
            break;
        case GLFW_KEY_C:
            m_editor_command |= (unsigned int)EditorCommand::scale_mode;
            break;
        case GLFW_KEY_DELETE:
            m_editor_command |= (unsigned int)EditorCommand::delete_object;
            break;
        default:
            break;
        }
    } else if (action == GLFW_RELEASE) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::exit);
            break;
        case GLFW_KEY_A:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_left);
            break;
        case GLFW_KEY_S:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_back);
            break;
        case GLFW_KEY_W:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_foward);
            break;
        case GLFW_KEY_D:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_right);
            break;
        case GLFW_KEY_Q:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_up);
            break;
        case GLFW_KEY_E:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::camera_down);
            break;
        case GLFW_KEY_T:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::translation_mode);
            break;
        case GLFW_KEY_R:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::rotation_mode);
            break;
        case GLFW_KEY_C:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::scale_mode);
            break;
        case GLFW_KEY_DELETE:
            m_editor_command &= (k_complement_control_command ^ (unsigned int)EditorCommand::delete_object);
            break;
        default:
            break;
        }
    }
}

void EditorInputManager::onReset() {
    // todo
}

void EditorInputManager::onCursorPos(double xpos, double ypos) {
    if (!g_is_editor_mode)
        return;

    float velocity = m_camera_speed * 0.1f; // todo: 根据 pivot 调整平移速度
    float angularVelocity = 180.0f / Math::max(m_engine_window_size.x, m_engine_window_size.y); // 180 degrees while moving full screen
    if (m_mouse_x >= 0.0f && m_mouse_y >= 0.0f) {
        if (g_editor_global_context.m_window_system->isMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) { // right mouse button pressed
            glfwSetInputMode(g_editor_global_context.m_window_system->getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_editor_global_context.m_scene_manager->getEditorCamera()->rotate(Vector2(ypos - m_mouse_y, xpos - m_mouse_x) * angularVelocity);
        } else if (g_editor_global_context.m_window_system->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) { // left mouse button pressed
            glfwSetInputMode(g_editor_global_context.m_window_system->getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            g_editor_global_context.m_scene_manager->moveEntity(xpos, ypos,
                                                                m_mouse_x, m_mouse_y,
                                                                m_engine_window_pos, m_engine_window_size, m_cursor_on_axis,
                                                                g_editor_global_context.m_scene_manager->getSelectedObjectMatrix());
        } else if (g_editor_global_context.m_window_system->isMouseButtonDown(GLFW_MOUSE_BUTTON_MIDDLE)) { // middle mouse button pressed
            glfwSetInputMode(g_editor_global_context.m_window_system->getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            Vector2 delta = Vector2(xpos - m_mouse_x, ypos - m_mouse_y); // movement of cursor
            std::shared_ptr editor_camera = g_editor_global_context.m_scene_manager->getEditorCamera();
            Quaternion camera_rotate = editor_camera->rotation().inverse(); // get inverse rotation of camera
            Vector3 camera_relative_pos = camera_rotate * Vector3(-delta.x * velocity, 0, delta.y * velocity); // convert to camera-relative position
            editor_camera->move(camera_relative_pos);
        } else { // no mouse button pressed
            glfwSetInputMode(g_editor_global_context.m_window_system->getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            if (isCursorInRect(m_engine_window_pos, m_engine_window_size)) {
                Vector2 cursor_uv = Vector2((m_mouse_x - m_engine_window_pos.x) / m_engine_window_size.x,
                                            (m_mouse_y - m_engine_window_pos.y) / m_engine_window_size.y);
                updateCursorOnAxis(cursor_uv);
            }
        }
    }
    m_mouse_x = xpos;
    m_mouse_y = ypos;
}

void EditorInputManager::onCursorEnter(int entered) {
    if (!entered) // lost focus
        m_mouse_x = m_mouse_y = -1.0f;
}

void EditorInputManager::onScroll(double xoffset, double yoffset) {
    if (!g_is_editor_mode)
        return;

    if (isCursorInRect(m_engine_window_pos, m_engine_window_size)) {
        if (g_editor_global_context.m_window_system->isMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
            if (yoffset > 0)
                m_camera_speed *= 1.2f;
            else
                m_camera_speed *= 0.8f;
        } else { // wheel scrolled up = zoom in by 2 extra degrees
            g_editor_global_context.m_scene_manager->getEditorCamera()->zoom((float)yoffset * 2.0f);
        }
    }
}

void EditorInputManager::onMouseButtonClicked(int key, int action) {
    if (!g_is_editor_mode)
        return;
    if (m_cursor_on_axis != 3)
        return;

    std::shared_ptr<Level> current_active_level = g_runtime_global_context.m_world_manager->getCurrentActiveLevel().lock();
    if (current_active_level == nullptr)
        return;

    if (isCursorInRect(m_engine_window_pos, m_engine_window_size)) {
        if (key == GLFW_MOUSE_BUTTON_LEFT) {
            Vector2 picked_uv((m_mouse_x - m_engine_window_pos.x) / m_engine_window_size.x,
                              (m_mouse_y - m_engine_window_pos.y) / m_engine_window_size.y);
            size_t  select_mesh_id = g_editor_global_context.m_scene_manager->getGuidOfPickedMesh(picked_uv);

            size_t gobject_id = g_editor_global_context.m_render_system->getGObjectIDByMeshID(select_mesh_id);
            g_editor_global_context.m_scene_manager->onGObjectSelected(gobject_id);
        }
    }
}

void EditorInputManager::onWindowClosed() { g_editor_global_context.m_engine_runtime->shutdownEngine(); }

void EditorInputManager::updateCursorOnAxis(Vector2 cursor_uv) {
    if (g_editor_global_context.m_scene_manager->getEditorCamera()) {
        Vector2 window_size(m_engine_window_size.x, m_engine_window_size.y);
        m_cursor_on_axis = g_editor_global_context.m_scene_manager->updateCursorOnAxis(cursor_uv, window_size);
    }
}

bool EditorInputManager::isCursorInRect(Vector2 pos, Vector2 size) const {
    return pos.x <= m_mouse_x && m_mouse_x <= pos.x + size.x && pos.y <= m_mouse_y && m_mouse_y <= pos.y + size.y;
}
} // namespace Piccolo