#include "EditorCamera.h"

#ifdef GAMMA_EDITOR
#include "../Core/InputSystem.h"
#include "Vendor/ImGui/imgui.h"
#include <algorithm>

using namespace DirectX;

EditorCamera::EditorCamera() : Camera() {
    m_focalPoint = { 0.0f, 0.0f, 0.0f };
    m_distance = 150.0f;                

    m_pitch = 0.8f;  // Наклоняем камеру вниз (около 45 градусов)
    m_yaw = -0.5f;   // Немного поворачиваем вбок для изометрического вида

    // ФИКС СКОРОСТЕЙ
    m_moveSpeed = 50.0f;
    m_orbitSpeed = 2.5f; // Увеличили вращение (было 0.005f)
    m_panSpeed = 1.5f;   // Увеличили скорость панорамирования
    m_zoomSpeed = 0.5f;
}

void EditorCamera::FocusOn(const XMFLOAT3& targetPosition, float distance) {
    m_focalPoint = targetPosition;
    m_distance = distance;
}

void EditorCamera::Pan(float deltaX, float deltaY) {
    XMVECTOR fPoint = XMLoadFloat3(&m_focalPoint);
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0);

    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rotation);
    XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), rotation);

    float speedMulti = m_distance * m_panSpeed * 0.01f;
    fPoint += -right * deltaX * speedMulti;
    fPoint += up * deltaY * speedMulti;

    XMStoreFloat3(&m_focalPoint, fPoint);
}

void EditorCamera::Orbit(float deltaX, float deltaY) {
    m_yaw += deltaX * m_orbitSpeed;
    m_pitch += deltaY * m_orbitSpeed;

    float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

void EditorCamera::Zoom(float delta) {
    float zoomStep = m_distance * 0.1f;

    if (zoomStep < 0.2f) zoomStep = 0.2f;

    m_distance -= delta * m_zoomSpeed * zoomStep;

    // Не даем камере улететь сквозь фокусную точку
    if (m_distance < 0.1f) m_distance = 0.1f;
}

void EditorCamera::Update(float deltaTime, const InputSystem& input) {
    XMFLOAT2 mouseDelta = input.GetMouseDelta();

    float scrollDelta = input.GetMouseScrollDelta();
    if (scrollDelta != 0.0f) {
        Zoom(scrollDelta);
    }

    if (input.IsKeyDown(VK_MBUTTON)) {
        if (input.IsKeyDown(VK_SHIFT)) {
            // Shift + MMB = Панорамирование
            Pan(mouseDelta.x, mouseDelta.y);
        }
        else {
            // Просто MMB = Вращение
            Orbit(mouseDelta.x, mouseDelta.y);
        }
    }
    // СВОБОДНЫЙ ПОЛЕТ
    else if (input.IsKeyDown(VK_RBUTTON)) {
        Orbit(mouseDelta.x, mouseDelta.y);

        XMVECTOR fPoint = XMLoadFloat3(&m_focalPoint);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0);
        XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotation);
        XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rotation);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);

        XMVECTOR moveDir = XMVectorZero();
        if (input.IsActionActive("EditorMoveForward"))  moveDir += forward;
        if (input.IsActionActive("EditorMoveBackward")) moveDir -= forward;
        if (input.IsActionActive("EditorMoveRight"))    moveDir += right;
        if (input.IsActionActive("EditorMoveLeft"))     moveDir -= right;
        if (input.IsActionActive("EditorFlyUp"))        moveDir += up;
        if (input.IsActionActive("EditorFlyDown"))      moveDir -= up;

        float currentSpeed = m_moveSpeed;
        if (input.IsKeyDown(VK_SHIFT)) currentSpeed *= 3.0f;

        fPoint += XMVector3Normalize(moveDir) * currentSpeed * deltaTime;
        XMStoreFloat3(&m_focalPoint, fPoint);
    }

    // Вычисляем позицию камеры из фокусной точки
    XMVECTOR fPoint = XMLoadFloat3(&m_focalPoint);
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0);
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotation);

    XMVECTOR pos = fPoint - (forward * m_distance);
    XMStoreFloat3(&m_position, pos);

    UpdateMatrices();
}
#endif // GAMMA_EDITOR