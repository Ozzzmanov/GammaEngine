//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ShadowManager.h
// Управляет каскадными картами теней (CSM) и расчетом ортографических матриц 
// направленного света (Солнца).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "Camera.h"
#include <vector>

/**
 * @struct CascadeData
 * @brief Хранит матрицы и плоскости отсечения для одного каскада теней.
 * Строго выровнено по 16 байт для безопасной передачи в Constant Buffer.
 */
__declspec(align(16)) struct CascadeData {
    DirectX::XMMATRIX ViewProj;         // 64 bytes: Матрица для рендера в этот каскад
    float SplitDepth;                   // 4 bytes: Дистанция окончания каскада
    float Padding[3];                   // 12 bytes
    DirectX::XMFLOAT4 FrustumPlanes[6]; // 96 bytes: Плоскости для Light-Space куллинга
};

/**
 * @class ShadowManager
 * @brief Менеджер CSM (Cascaded Shadow Maps).
 */
class ShadowManager {
public:
    ShadowManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~ShadowManager();

    /// @brief Создает массив текстур теней. Можно вызывать повторно при смене настроек графики.
    bool Initialize(int resolution, int cascadeCount);

    /// @brief Рассчитывает матрицы для каждого каскада каждый кадр, сглаживая движение (Texel Snapping).
    void Update(Camera* camera, const DirectX::XMFLOAT3& lightDir, float maxShadowDist);

    ID3D11ShaderResourceView* GetShadowMapSRV() const { return m_shadowSRV.Get(); }
    ID3D11DepthStencilView* GetCascadeDSV(int index) const { return m_cascadeDSVs[index].Get(); }

    const std::vector<CascadeData>& GetCascades() const { return m_cascades; }
    int GetResolution() const { return m_resolution; }
    int GetCascadeCount() const { return m_cascadeCount; }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    int m_resolution = 2048;
    int m_cascadeCount = 3;

    ComPtr<ID3D11Texture2D> m_shadowMap;
    ComPtr<ID3D11ShaderResourceView> m_shadowSRV;
    std::vector<ComPtr<ID3D11DepthStencilView>> m_cascadeDSVs;

    std::vector<CascadeData> m_cascades;
};