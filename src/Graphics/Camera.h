//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Camera.h
// Управление камерой, матрицами вида/проекции и Frustum Culling.
// ================================================================================

#pragma once

#include <DirectXMath.h>
#include <DirectXCollision.h>

class InputSystem;

class Camera {
public:
    Camera();
    ~Camera();

    void Initialize(float fov, float aspectRatio, float nearZ, float farZ);

    void Update(float deltaTime, const InputSystem& input);

    // --- Getters ---
    const DirectX::XMFLOAT4X4& GetViewMatrix() const { return m_viewMatrix; }
    const DirectX::XMFLOAT4X4& GetProjectionMatrix() const { return m_projectionMatrix; }

    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    DirectX::XMFLOAT3 GetRotation() const { return { m_pitch, m_yaw, 0.0f }; }

    // Для Frustum Culling
    const DirectX::BoundingFrustum& GetFrustum() const { return m_frustum; }
    DirectX::XMFLOAT3 GetCullOrigin() const { return m_cullOrigin; } // Позиция для расчета дистанции

    // Управление режимом отладки (заморозка Frustum)
    void ToggleDebugMode();
    bool IsDebugMode() const { return m_isDebugMode; }

private:
    void UpdateMatrices();

private:
    // Transform
    DirectX::XMFLOAT3 m_position;
    float m_pitch;
    float m_yaw;

    // Matrices
    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;

    // Culling & Debug
    DirectX::BoundingFrustum m_frustum;    // Текущий (или замороженный) фрустум
    DirectX::XMFLOAT3 m_cullOrigin;        // Позиция для проверки дистанции
    bool m_isDebugMode;                    // Если true - фрустум не обновляется
};