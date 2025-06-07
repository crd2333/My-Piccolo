#pragma once

#include "runtime/core/math/vector3.h"

#include "runtime/resource/res_type/components/camera.h"

#include "runtime/function/framework/component/component.h"
#include "runtime/function/framework/component/camera/camera_mode.h"

namespace Piccolo {
class RenderCamera;
class Character;

REFLECTION_TYPE(CameraComponent)
CLASS(CameraComponent : public Component, WhiteListFields) {
    REFLECTION_BODY(CameraComponent)

public:
    CameraComponent() = default;
    void postLoadResource(std::weak_ptr<GObject> parent_object) override;
    void tick(float delta_time) override;

    CameraMode getCameraMode() const { return m_camera_mode; }
    Vector3    getPosition() const { return m_position; }
    void       setCameraMode(CameraMode mode) { m_camera_mode = mode; }

    Vector3 forward() const { return m_forward; }
    Vector3 up() const { return m_up; }
    Vector3 left() const { return m_left; }

    void rotate(Vector2 delta);

private:
    void tickFirstPersonCamera(float delta_time, std::shared_ptr<Character> current_character, float delta_pitch_rad, const Quaternion& q_yaw);
    void tickThirdPersonCamera(float delta_time, std::shared_ptr<Character> current_character, float delta_pitch_rad, const Quaternion& q_yaw);
    void tickFreeCamera(float delta_time, float delta_pitch_rad, const Quaternion& q_yaw);

    void updateCameraRenderData();

    META(Enable)
    CameraComponentRes m_camera_res;

    CameraMode m_camera_mode {CameraMode::invalid};

    Vector3 m_position;

    float move_speed = 2.0f; // speed factor

    Vector3 m_forward {Vector3::NEGATIVE_UNIT_Y};
    Vector3 m_up {Vector3::UNIT_Z};
    Vector3 m_left {Vector3::UNIT_X};
};
} // namespace Piccolo
