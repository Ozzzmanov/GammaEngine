//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LightManager.h
// Управляет локальным освещением (Point / Spot Lights) для Deferred Renderer.
// Собирает статику и динамику в StructuredBuffer'ы.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "GpuTypes.h"
#include <vector>

/**
 * @class LightManager
 * @brief Глобальный менеджер локальных источников света.
 */
class LightManager {
public:
    static LightManager& Get() {
        static LightManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void ClearStaticLights();

    void AddStaticPointLight(const GPUPointLight& light);
    void AddStaticSpotLight(const GPUSpotLight& light);

    /// @brief Вызывается каждый кадр перед рендером! 
    /// Собирает статичный свет + динамический (из скриптов) и пушит в GPU
    void UpdateGpuBuffers();

    ID3D11ShaderResourceView* GetPointLightsSRV() const { return m_pointLightsSRV.Get(); }
    ID3D11ShaderResourceView* GetSpotLightsSRV()  const { return m_spotLightsSRV.Get(); }

    uint32_t GetPointLightCount() const { return m_pointLightCount; }
    uint32_t GetSpotLightCount()  const { return m_spotLightCount; }

private:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Статичный свет (Из .glevel)
    std::vector<GPUPointLight> m_staticPointLights;
    std::vector<GPUSpotLight>  m_staticSpotLights;

    // GPU Буферы
    ComPtr<ID3D11Buffer>             m_pointLightsBuffer;
    ComPtr<ID3D11ShaderResourceView> m_pointLightsSRV;

    ComPtr<ID3D11Buffer>             m_spotLightsBuffer;
    ComPtr<ID3D11ShaderResourceView> m_spotLightsSRV;

    uint32_t m_pointLightCount = 0;
    uint32_t m_spotLightCount = 0;

    // FIXME: Хардкод максимального числа источников (1024). 
    // Нужно реализовать автоматический ресайз буферов, если света на сцене больше.
    uint32_t m_maxPointLights = 1024;
    uint32_t m_maxSpotLights = 1024;
};