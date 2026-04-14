#pragma once
#include "../Core/Prerequisites.h"

class DirectionalLight {
public:
    DirectionalLight();
    ~DirectionalLight() = default;

    void Update(float deltaTime);

    // Управление через углы (Удобно для цикла День/Ночь)
    void SetPitchYaw(float pitch, float yaw);

    // Настройки "под Cycles"
    void SetColor(const Vector3& color);
    void SetIntensity(float intensity);
    void SetAngularSize(float sizeMultiplier); // 1.0 = реальное солнце, больше = мягче тени/блики

    // Геттеры для рендера
    Vector3 GetDirection() const { return m_direction; }
    Vector4 GetColorAndIntensity() const { return Vector4(m_color.x, m_color.y, m_color.z, m_intensity); }
    float GetAngularSize() const { return m_angularSize; }

private:
    void UpdateDirection();

    float m_pitch; // Высота над горизонтом (0 = горизонт, 90 = зенит)
    float m_yaw;   // Вращение по компасу

    Vector3 m_direction;
    Vector3 m_color;
    float m_intensity;
    float m_angularSize; // Физический размер диска на небе
};