//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Camera.cpp
// Реализация камеры.
// ================================================================================

#include "Camera.h"
#include "../Core/InputSystem.h"
#include "../Config/EngineConfig.h"
#include <algorithm>
#undef max
#undef min

using namespace DirectX;

Camera::Camera()
    : m_position({ 0.0f, 50.0f, 0.0f }),
    m_pitch(0.0f), m_yaw(0.0f),
    m_isDebugMode(false)
{
    XMStoreFloat4x4(&m_viewMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_projectionMatrix, XMMatrixIdentity());
    m_cullOrigin = m_position;
}

Camera::~Camera() {}

void Camera::Initialize(float fov, float aspectRatio, float nearZ, float farZ) {
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
    XMStoreFloat4x4(&m_projectionMatrix, P);

    // Создаем начальный фрустум
    BoundingFrustum::CreateFromMatrix(m_frustum, P);
}

void Camera::Update(float deltaTime, const InputSystem& input) {
    // Вращение
    XMFLOAT2 mouseDelta = input.GetMouseDelta();
    float sensitivity = EngineConfig::Get().MouseSensitivity;

    float rotX = mouseDelta.x * (sensitivity / 0.003f);
    float rotY = mouseDelta.y * (sensitivity / 0.003f);

    if (EngineConfig::Get().InvertY) rotY = -rotY;

    if (input.IsKeyDown(VK_RBUTTON)) {
        m_yaw += rotX;
        m_pitch += rotY;
        float limit = XM_PIDIV2 - 0.01f;
        m_pitch = std::max(-limit, std::min(m_pitch, limit));
    }

    // Перемещение
    float speed = 100.0f * deltaTime;

    // Спринт
    if (input.IsActionActive("Sprint")) speed *= 5.0f;

    XMVECTOR pos = XMLoadFloat3(&m_position);

    XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    if (input.IsActionActive("MoveForward"))  pos += forward * speed;
    if (input.IsActionActive("MoveBackward")) pos -= forward * speed;
    if (input.IsActionActive("MoveLeft"))     pos -= right * speed;
    if (input.IsActionActive("MoveRight"))    pos += right * speed;

    // Q / E (Fly Down / Fly Up)
    if (input.IsActionActive("FlyUp"))        pos += up * speed; // E
    if (input.IsActionActive("FlyDown"))      pos -= up * speed; // Q

    XMStoreFloat3(&m_position, pos);

    UpdateMatrices();
}

void Camera::UpdateMatrices() {
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0);
    XMVECTOR lookDir = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotation);
    XMVECTOR target = pos + lookDir;
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMMATRIX V = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&m_viewMatrix, V);

    if (!m_isDebugMode) {
        BoundingFrustum viewFrustum;
        BoundingFrustum::CreateFromMatrix(viewFrustum, XMLoadFloat4x4(&m_projectionMatrix));
        XMMATRIX invView = XMMatrixInverse(nullptr, V);
        viewFrustum.Transform(m_frustum, invView);
        m_cullOrigin = m_position;
    }
}

void Camera::ToggleDebugMode() {
    m_isDebugMode = !m_isDebugMode;
}

DirectX::XMMATRIX Camera::GetViewProjectionMatrix() const {
    XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&m_projectionMatrix);
    return XMMatrixMultiply(view, proj);
}
