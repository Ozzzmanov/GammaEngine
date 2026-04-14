#pragma once
#include "../Core/Prerequisites.h"

#ifdef GAMMA_EDITOR
#include "../Graphics/Camera.h"

class EditorCamera : public Camera {
public:
    EditorCamera();
    ~EditorCamera() override = default;

    // Полностью переопределяем управление
    void Update(float deltaTime, const InputSystem& input) override;

    // Фокус на объекте
    void FocusOn(const DirectX::XMFLOAT3& targetPosition, float distance = 10.0f);

private:
    void Pan(float deltaX, float deltaY);
    void Orbit(float deltaX, float deltaY);
    void Zoom(float delta);

    DirectX::XMFLOAT3 m_focalPoint;
    float m_distance;

    float m_moveSpeed = 20.0f;
    float m_orbitSpeed = 0.005f;
    float m_panSpeed = 0.02f;
    float m_zoomSpeed = 0.5f;
};
#endif // GAMMA_EDITOR