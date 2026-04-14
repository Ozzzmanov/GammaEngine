// ================================================================================
// WorldEnvironment.cpp
// ================================================================================
#include "WorldEnvironment.h"
#include "../Config/EngineConfig.h" // Добавили конфиг
#include <algorithm>
#include <cmath>    

static float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

WorldEnvironment::WorldEnvironment() {
    m_timeOfDay = 16.0f;
    m_timeSpeed = 0.0f;

    m_sun.AngularSize = 1.0f;
    m_sun.EnableGodRays = true;
    m_sun.GodRaysDensity = 0.8f;

    SetTimeOfDay(m_timeOfDay);
}

void WorldEnvironment::Update(float deltaTime) {
    if (m_timeSpeed > 0.0f) {
        m_timeOfDay += deltaTime * m_timeSpeed;
        if (m_timeOfDay >= 24.0f) m_timeOfDay -= 24.0f;
        SetTimeOfDay(m_timeOfDay);
    }
}

void WorldEnvironment::SetTimeOfDay(float time) {
    m_timeOfDay = time;

    float compassOffset = 45.0f;
    float timeOffset = time - 6.0f;
    float pitch = sin(timeOffset * (3.14159f / 12.0f)) * 90.0f;
    float yaw = (time / 24.0f) * 360.0f - 90.0f + compassOffset;

    m_sun.Pitch = pitch;
    m_sun.Yaw = yaw;

    // --- ЛОГИКА ЦВЕТА НЕБА ---
    // 1. Цвета для разных времен суток
    Vector3 zenithDay(0.015f, 0.03f, 0.08f);
    Vector3 horizonDay(0.05f, 0.15f, 0.3f);

    Vector3 zenithSunset(0.02f, 0.04f, 0.1f);
    Vector3 horizonSunset(0.8f, 0.25f, 0.02f);

    Vector3 zenithNight(0.001f, 0.002f, 0.005f);  // Очень темный синий (почти черный)
    Vector3 horizonNight(0.002f, 0.005f, 0.015f); // Чуть светлее на горизонте

    // 2. Факторы смешивания
    // День -> Закат (когда солнце опускается с 20 до 0 градусов)
    float sunsetBlend = std::clamp((20.0f - pitch) / 20.0f, 0.0f, 1.0f);

    // Закат -> Ночь (когда солнце опускается с 0 до -15 градусов под землю)
    float nightBlend = std::clamp((0.0f - pitch) / 15.0f, 0.0f, 1.0f);

    // 3. Плавно смешиваем цвета
    // Сначала смешиваем День и Закат
    Vector3 curZenith = Vector3(
        std::lerp(zenithDay.x, zenithSunset.x, sunsetBlend),
        std::lerp(zenithDay.y, zenithSunset.y, sunsetBlend),
        std::lerp(zenithDay.z, zenithSunset.z, sunsetBlend)
    );
    Vector3 curHorizon = Vector3(
        std::lerp(horizonDay.x, horizonSunset.x, sunsetBlend),
        std::lerp(horizonDay.y, horizonSunset.y, sunsetBlend),
        std::lerp(horizonDay.z, horizonSunset.z, sunsetBlend)
    );

    // Затем получившийся результат плавно уводим в Ночь
    m_zenithColor = Vector3(
        std::lerp(curZenith.x, zenithNight.x, nightBlend),
        std::lerp(curZenith.y, zenithNight.y, nightBlend),
        std::lerp(curZenith.z, zenithNight.z, nightBlend)
    );
    m_horizonColor = Vector3(
        std::lerp(curHorizon.x, horizonNight.x, nightBlend),
        std::lerp(curHorizon.y, horizonNight.y, nightBlend),
        std::lerp(curHorizon.z, horizonNight.z, nightBlend)
    );
    // -------------------------

    if (pitch <= -5.0f) {
        m_sun.Intensity = 0.0f;
    }
    else {
        float tempK = 2000.0f + std::clamp(pitch / 60.0f, 0.0f, 1.0f) * 4500.0f;
        Vector3 rawColor = KelvinToRGB(tempK);
        float maxC = std::max({ rawColor.x, rawColor.y, rawColor.z });
        if (maxC > 0.0f) m_sun.Color = Vector3(rawColor.x / maxC, rawColor.y / maxC, rawColor.z / maxC);

        // Плавное затухание на горизонте
        float horizonFade = smoothstep(-2.0f, 8.0f, pitch);
        // Теперь на закате интенсивность упадет почти до 0, и шейдер это учтет!
        m_sun.Intensity = horizonFade * 1.2f;
    }

    if (EngineConfig::Get().VolumetricLighting.Enabled) {
        float timeMultiplier = 1.0f - smoothstep(20.0f, 80.0f, pitch);

        // --- ПРАВИЛЬНАЯ ВЛАЖНОСТЬ ---
        // Влажность должна добавлять максимум 50% к плотности тумана, а не умножать его в разы
        float weatherMultiplier = 1.0f + (m_humidity * 0.5f);

        m_sun.GodRaysDensity = EngineConfig::Get().VolumetricLighting.Density * timeMultiplier * weatherMultiplier;
    }
    else {
        m_sun.GodRaysDensity = 0.0f;
    }

    RecalculateSunDirection();
}

void WorldEnvironment::SetTimeSpeed(float speed) { m_timeSpeed = speed; }
void WorldEnvironment::SetGodRays(bool enabled, float density) {
    m_sun.EnableGodRays = enabled;
    m_sun.GodRaysDensity = density;
}

void WorldEnvironment::RecalculateSunDirection() {
    float pitchRad = m_sun.Pitch * (3.14159265f / 180.0f);
    float yawRad = m_sun.Yaw * (3.14159265f / 180.0f);

    m_sunDirection.x = cos(pitchRad) * cos(yawRad);
    m_sunDirection.y = sin(pitchRad);
    m_sunDirection.z = cos(pitchRad) * sin(yawRad);

    m_sunDirection.Normalize();
}

Vector3 WorldEnvironment::KelvinToRGB(float temp) {
    temp /= 100.0f;
    float r, g, b;

    if (temp <= 66.0f) {
        r = 255.0f;
        g = std::clamp(99.47f * log(temp) - 161.1f, 0.0f, 255.0f);
        if (temp <= 19.0f) b = 0.0f;
        else b = std::clamp(138.5f * log(temp - 10.0f) - 305.0f, 0.0f, 255.0f);
    }
    else {
        r = std::clamp(329.7f * pow(temp - 60.0f, -0.133f), 0.0f, 255.0f);
        g = std::clamp(288.1f * pow(temp - 60.0f, -0.0755f), 0.0f, 255.0f);
        b = 255.0f;
    }
    return Vector3(r / 255.0f, g / 255.0f, b / 255.0f);
}