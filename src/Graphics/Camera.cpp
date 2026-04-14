//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Camera.cpp
// ================================================================================
#include "Camera.h"
#include "../Core/InputSystem.h"
#include "../Config/EngineConfig.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera()
    : m_position({ 0.0f, 50.0f, 0.0f }),
    m_velocity({ 0.0f, 0.0f, 0.0f }),
    m_pitch(0.0f), m_yaw(0.0f),
    m_isDebugMode(false),
    m_isDirty(true)
{
    XMStoreFloat4x4(&m_viewMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_projectionMatrix, XMMatrixIdentity());
    m_cullOrigin = m_position;
}

Camera::~Camera() {}

void Camera::Initialize(float fov, float aspectRatio, float nearZ, float farZ) {
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
    XMStoreFloat4x4(&m_projectionMatrix, P);

    BoundingFrustum::CreateFromMatrix(m_frustum, P);
    m_isDirty = true;
}

void Camera::Update(float deltaTime, const InputSystem& input) {
    XMFLOAT2 mouseDelta = input.GetMouseDelta();

    // Вращение
    // FIXME: Утечка Windows API (VK_RBUTTON) в логику камеры.
    // Использовать лучше input.IsActionActive("CameraLook").
    if (input.IsKeyDown(0x02)) {
        m_yaw += mouseDelta.x * MouseSensitivity;
        m_pitch += mouseDelta.y * MouseSensitivity;

        m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
        m_isDirty = true;
    }

    // Вычисление векторов направления
    XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);

    // Вектор взгляда (для свободного полета)
    XMVECTOR flyForward = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);
    // Горизонтальный стрейф (без крена)
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0.0f, -sinf(m_yaw), 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // Вычисление желаемого ускорения (Input)
    XMVECTOR accel = XMVectorZero();

    if (input.IsActionActive("MoveForward"))  accel += flyForward;
    if (input.IsActionActive("MoveBackward")) accel -= flyForward;
    if (input.IsActionActive("MoveLeft"))     accel -= right;
    if (input.IsActionActive("MoveRight"))    accel += right;
    if (input.IsActionActive("FlyUp"))        accel += up;
    if (input.IsActionActive("FlyDown"))      accel -= up;

    // Нормализация, чтобы движение по диагонали не было быстрее
    if (XMVectorGetX(XMVector3LengthSq(accel)) > 0.0f) {
        accel = XMVector3Normalize(accel);
    }

    float currentSpeed = MovementSpeed;
    if (input.IsActionActive("Sprint")) currentSpeed *= SprintMultiplier;

    accel *= currentSpeed;

    // Инерция (Frame-Rate Independent Damping)
    XMVECTOR currentVel = XMLoadFloat3(&m_velocity);
    currentVel = XMVectorLerp(currentVel, accel, 1.0f - expf(-Damping * deltaTime));
    XMStoreFloat3(&m_velocity, currentVel);

    // Применение перемещения
    if (XMVectorGetX(XMVector3LengthSq(currentVel)) > 0.0001f) {
        XMVECTOR pos = XMLoadFloat3(&m_position);
        pos += currentVel * deltaTime;
        XMStoreFloat3(&m_position, pos);
        m_isDirty = true;
    }

    // Обновляем тяжелую математику только при движении
    if (m_isDirty) {
        UpdateMatrices();
        m_isDirty = false;
    }
}

void Camera::UpdateMatrices() {
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
    XMVECTOR lookDir = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
    XMVECTOR target = pos + lookDir;
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

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

DirectX::XMMATRIX Camera::GetViewProjectionMatrix() const {
    XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&m_projectionMatrix);
    return XMMatrixMultiply(view, proj);
}