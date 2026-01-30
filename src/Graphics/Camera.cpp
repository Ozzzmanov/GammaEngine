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
    // Вращение (Мышь)
    XMFLOAT2 mouseDelta = input.GetMouseDelta();

    // Если зажат ПКМ - вращаем камеру
    if (input.IsKeyDown(VK_RBUTTON)) {
        m_yaw += mouseDelta.x;
        m_pitch += mouseDelta.y;

        // Clamp pitch
        m_pitch = std::max(-1.5f, std::min(m_pitch, 1.5f));
    }

    // Перемещение
    float speed = 100.0f * deltaTime;
    if (input.IsKeyDown(VK_SHIFT)) speed *= 5.0f;

    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    if (input.IsKeyDown('W')) pos += forward * speed;
    if (input.IsKeyDown('S')) pos -= forward * speed;
    if (input.IsKeyDown('A')) pos -= right * speed;
    if (input.IsKeyDown('D')) pos += right * speed;
    if (input.IsKeyDown('Q')) pos -= up * speed;
    if (input.IsKeyDown('E')) pos += up * speed;

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

    // Если дебаг режим НЕ активен - обновляем фрустум для отсечения
    if (!m_isDebugMode) {
        BoundingFrustum viewFrustum;
        BoundingFrustum::CreateFromMatrix(viewFrustum, XMLoadFloat4x4(&m_projectionMatrix));

        XMMATRIX invView = XMMatrixInverse(nullptr, V);
        viewFrustum.Transform(m_frustum, invView);

        m_cullOrigin = m_position; // Точка отсчета дистанции тоже обновляется
    }
}

void Camera::ToggleDebugMode() {
    m_isDebugMode = !m_isDebugMode;
}