//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ShadowManager.cpp
// ================================================================================
#include "ShadowManager.h"
#include "../Core/Logger.h"
#include <algorithm>
#include "../Config/EngineConfig.h"

using namespace DirectX;

ShadowManager::ShadowManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

ShadowManager::~ShadowManager() {}

bool ShadowManager::Initialize(int resolution, int cascadeCount) {
    m_resolution = resolution;
    m_cascadeCount = cascadeCount;
    m_cascades.resize(cascadeCount);

    // Очищаем старые ресурсы, если метод вызван повторно (смена настроек)
    m_cascadeDSVs.clear();
    m_shadowMap.Reset();
    m_shadowSRV.Reset();

    // Создаем Texture2DArray (Массив текстур)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = resolution;
    texDesc.Height = resolution;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = cascadeCount;
    texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_shadowMap.GetAddressOf());
    HR_CHECK(hr, "ShadowManager: Failed to create Shadow Map Array.");

    // Создаем DSV для КАЖДОГО каскада
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    for (int i = 0; i < cascadeCount; ++i) {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        ComPtr<ID3D11DepthStencilView> dsv;
        hr = m_device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, dsv.GetAddressOf());
        if (FAILED(hr)) return false;
        m_cascadeDSVs.push_back(dsv);
    }

    // Создаем глобальный SRV (для чтения всех теней в DeferredLight/Volumetric)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = cascadeCount;

    hr = m_device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, m_shadowSRV.GetAddressOf());
    if (FAILED(hr)) return false;

    GAMMA_LOG_INFO(LogCategory::Render, "ShadowManager Initialized (Resolution: " + std::to_string(resolution) + "x" + std::to_string(resolution) + ", Cascades: " + std::to_string(cascadeCount) + ")");
    return true;
}

// Хелпер для извлечения 6 плоскостей фрустума из матрицы View-Projection
static void ExtractFrustumPlanes(const DirectX::XMMATRIX& viewProj, DirectX::XMFLOAT4* outPlanes) {
    DirectX::XMFLOAT4X4 m;
    DirectX::XMStoreFloat4x4(&m, viewProj);

    // Нормали смотрят НАРУЖУ (Outward), чтобы работало условие: dot(center, N) + D > radius -> Cull
    outPlanes[0] = { -(m._14 + m._11), -(m._24 + m._21), -(m._34 + m._31), -(m._44 + m._41) }; // Left
    outPlanes[1] = { -(m._14 - m._11), -(m._24 - m._21), -(m._34 - m._31), -(m._44 - m._41) }; // Right
    outPlanes[2] = { -(m._14 + m._12), -(m._24 + m._22), -(m._34 + m._32), -(m._44 + m._42) }; // Bottom
    outPlanes[3] = { -(m._14 - m._12), -(m._24 - m._22), -(m._34 - m._32), -(m._44 - m._42) }; // Top
    outPlanes[4] = { -m._13, -m._23, -m._33, -m._43 };                                         // Near
    outPlanes[5] = { -(m._14 - m._13), -(m._24 - m._23), -(m._34 - m._33), -(m._44 - m._43) }; // Far

    // Строгая нормализация плоскостей
    for (int i = 0; i < 6; ++i) {
        float length = std::sqrt(outPlanes[i].x * outPlanes[i].x +
            outPlanes[i].y * outPlanes[i].y +
            outPlanes[i].z * outPlanes[i].z);
        if (length > 0.00001f) {
            float invLength = 1.0f / length;
            outPlanes[i].x *= invLength;
            outPlanes[i].y *= invLength;
            outPlanes[i].z *= invLength;
            outPlanes[i].w *= invLength;
        }
    }
}

void ShadowManager::Update(Camera* camera, const XMFLOAT3& lightDir, float maxShadowDist) {
    XMVECTOR lightDirVec = XMVector3Normalize(XMLoadFloat3(&lightDir));
    const auto& activeProfile = EngineConfig::Get().GetActiveProfile();

    // --- УМНОЕ РАСПРЕДЕЛЕНИЕ КАСКАДОВ ---
    std::vector<float> activeSplits(m_cascadeCount);

    // Безопасное чтение настроек с защитой от выхода за пределы массива (если каскадов > 3)
    for (int i = 0; i < m_cascadeCount; ++i) {
        if (i < 3) {
            activeSplits[i] = activeProfile.Shadows.Splits[i];
        }
        else {
            // Фолбэк, если кто-то задаст 4+ каскадов в будущем
            activeSplits[i] = activeProfile.Shadows.Splits[2] + (i - 2) * 200.0f;
        }
    }

    // Последний активный каскад ВСЕГДА заканчивается строго на MaxShadowDistance!
    activeSplits[m_cascadeCount - 1] = maxShadowDist;

    // Защита от наезда ближних каскадов на максимальную дистанцию
    for (int i = 0; i < m_cascadeCount - 1; ++i) {
        activeSplits[i] = std::min(activeSplits[i], maxShadowDist - 1.0f);
    }

    DirectX::XMFLOAT3 pos = camera->GetPosition();
    XMVECTOR camPos = XMLoadFloat3(&pos);

    for (int i = 0; i < m_cascadeCount; ++i) {
        float radius = activeSplits[i];

        // FIXME: Центрирование на камере тратит 50% Shadow Map на рендер того, что позади игрока.
        // Для качества центр должен быть смещен вперед по взгляду: 
        // center = camPos + camera->GetForwardVector() * (radius * 0.5f);
        // При этом радиус нужно пересчитать, чтобы он ровно обхватывал срез фрустума.
        XMVECTOR center = camPos;

        // Отодвигаем солнце назад на глобальную Дальность Прорисовки
        float pullBackDist = activeProfile.Graphics.RenderDistance;
        XMVECTOR lightPos = XMVectorAdd(center, XMVectorScale(lightDirVec, pullBackDist));

        // Защита от математического взрыва, когда солнце строго в зените (полдень)
        XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (std::abs(XMVectorGetY(lightDirVec)) > 0.999f) {
            upVec = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // Меняем вектор "Вверх" на ось Z
        }

        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, upVec);

        float nearZ = 0.1f;
        float farZ = pullBackDist + radius;
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-radius, radius, -radius, radius, nearZ, farZ);

   
        // TEXEL SNAPPING
        XMMATRIX shadowMatrix = XMMatrixMultiply(lightView, lightProj);
        XMVECTOR shadowOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin = XMVector4Transform(shadowOrigin, shadowMatrix);

        // Масштабируем в пиксели текстуры
        shadowOrigin = XMVectorScale(shadowOrigin, static_cast<float>(m_resolution) / 2.0f);

        XMVECTOR roundedOrigin = XMVectorRound(shadowOrigin);
        XMVECTOR roundOffset = XMVectorSubtract(roundedOrigin, shadowOrigin);

        // Возвращаем масштаб обратно
        roundOffset = XMVectorScale(roundOffset, 2.0f / static_cast<float>(m_resolution));
        roundOffset = XMVectorSetZ(roundOffset, 0.0f);
        roundOffset = XMVectorSetW(roundOffset, 0.0f);

        // Сдвигаем матрицу проекции (DirectXMath хранит Row-Major, поэтому r[3] - это translation)
        lightProj.r[3] = XMVectorAdd(lightProj.r[3], roundOffset);
    
        // Итоговая матрица для шейдеров
        XMMATRIX lightViewProj = XMMatrixMultiply(lightView, lightProj);

        m_cascades[i].ViewProj = XMMatrixTranspose(lightViewProj);
        m_cascades[i].SplitDepth = radius;

        ExtractFrustumPlanes(lightViewProj, m_cascades[i].FrustumPlanes);
    }
}