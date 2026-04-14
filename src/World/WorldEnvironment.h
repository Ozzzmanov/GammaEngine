#pragma once
#include "../Core/Prerequisites.h"

// Структура, которую легко биндить в скрипты
struct SunConfig {
    float Pitch;           // Высота над горизонтом
    float Yaw;             // Вращение по компасу
    Vector3 Color;         // RGB цвет солнца
    float Intensity;       // Яркость (HDR)
    float AngularSize;     // Физический размер диска (0.53 по умолчанию)

    // Эффекты
    bool EnableGodRays;    // Те самые "солнечные лучи / колодец"
    float GodRaysDensity;  // Насколько плотный воздух (пыль/туман)
};

class WorldEnvironment {
public:
    WorldEnvironment();
    ~WorldEnvironment() = default;

    void Update(float deltaTime);

    // --- Скриптовый API (Высокий уровень) ---
    void SetTimeOfDay(float timeInHours); // От 0.0 до 24.0
    void SetTimeSpeed(float multiplier);  // Скорость течения времени

    // --- Скриптовый API (Тонкая настройка) ---
    void SetSunAngles(float pitch, float yaw);
    void SetSunColorTemperature(float kelvin); // Реалистичный цвет по Кельвину
    void SetGodRays(bool enabled, float density = 1.0f);

    void SetHumidity(float humidity) { m_humidity = humidity; }

    Vector3 GetZenithColor() const { return m_zenithColor; }
    Vector3 GetHorizonColor() const { return m_horizonColor; }

    // --- Внутренний API для Рендера ---
    Vector3 GetSunDirection() const { return m_sunDirection; }
    const SunConfig& GetSunConfig() const { return m_sun; }

private:
    void RecalculateSunDirection();
    Vector3 KelvinToRGB(float temperature); // Физически корректный цвет

    float m_humidity = 0.2f; // По умолчанию 50% влажности

    SunConfig m_sun;
    Vector3 m_sunDirection;

    Vector3 m_zenithColor;
    Vector3 m_horizonColor;

    float m_timeOfDay;
    float m_timeSpeed;
};