#include "DirectionalLight.h"

DirectionalLight::DirectionalLight() {
    // Дефолтные значения (Красивое утреннее солнце)
    m_pitch = 25.0f;
    m_yaw = 45.0f;
    m_color = Vector3(1.0f, 0.9f, 0.8f);
    m_intensity = 10.0f; // Мощный HDR
    m_angularSize = 1.0f; // Реальный размер солнца

    UpdateDirection();
}

void DirectionalLight::Update(float deltaTime) {
    // Здесь в будущем можно сделать: m_pitch -= deltaTime * speed; (Цикл дня и ночи)
    // UpdateDirection();
}

void DirectionalLight::SetPitchYaw(float pitch, float yaw) {
    m_pitch = pitch;
    m_yaw = yaw;
    UpdateDirection();
}

void DirectionalLight::UpdateDirection() {
    // Переводим градусы в радианы
    float pitchRad = m_pitch * (3.14159265f / 180.0f);
    float yawRad = m_yaw * (3.14159265f / 180.0f);

    // Сферические координаты в декартовы (Вектор указывает НА солнце)
    m_direction.x = cos(pitchRad) * cos(yawRad);
    m_direction.y = sin(pitchRad);
    m_direction.z = cos(pitchRad) * sin(yawRad);

    m_direction.Normalize();
}

void DirectionalLight::SetColor(const Vector3& color) { m_color = color; }
void DirectionalLight::SetIntensity(float intensity) { m_intensity = intensity; }
void DirectionalLight::SetAngularSize(float size) { m_angularSize = size; }