//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Camera.h
// Управление камерой, матрицами вида/проекции. Поддержка инерции.
// ================================================================================
#pragma once
#include <DirectXMath.h>
#include <DirectXCollision.h>

class InputSystem;

class Camera {
public:
    Camera();
    virtual ~Camera();

    void Initialize(float fov, float aspectRatio, float nearZ, float farZ);

    virtual void Update(float deltaTime, const InputSystem& input);

    const DirectX::XMFLOAT4X4& GetViewMatrix() const { return m_viewMatrix; }
    const DirectX::XMFLOAT4X4& GetProjectionMatrix() const { return m_projectionMatrix; }

    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    DirectX::XMFLOAT3 GetRotation() const { return { m_pitch, m_yaw, 0.0f }; }
    const DirectX::BoundingFrustum& GetFrustum() const { return m_frustum; }
    DirectX::XMFLOAT3 GetCullOrigin() const { return m_cullOrigin; }

    void ToggleDebugMode() { m_isDebugMode = !m_isDebugMode; }
    bool IsDebugMode() const { return m_isDebugMode; }

    DirectX::XMMATRIX GetViewProjectionMatrix() const;

    // Настройки камеры
    float MovementSpeed = 15.0f;
    float SprintMultiplier = 5.0f;
    float MouseSensitivity = 0.002f;
    float Damping = 12.0f; // Сила трения/инерции (чем выше, тем быстрее остановка)

protected:
    void UpdateMatrices();

    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_velocity; // Вектор текущей скорости для плавного полета

    float m_pitch;
    float m_yaw;

    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;

    DirectX::BoundingFrustum m_frustum;
    DirectX::XMFLOAT3 m_cullOrigin;

    bool m_isDebugMode;
    bool m_isDirty; // Флаг нужно ли пересчитывать матрицы в этом кадре
};