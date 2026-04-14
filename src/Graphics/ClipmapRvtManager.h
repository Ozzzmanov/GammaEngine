//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ClipmapRvtManager.h
// Менеджер каскадного RVT (Clipmap) с поддержкой тороидального обновления.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <vector>
#include <DirectXMath.h>

// Задача на допекание "отрезанной" полоски при движении камеры
struct ClipmapUpdateTask {
    int CascadeIndex;
    int StartCellX, StartCellZ; // Координаты начальной ячейки
    int UpdateWidth, UpdateHeight; // Размер обновляемого прямоугольника
    float TexelSize; // Для вычисления мировых координат в Compute Shader
};

// Константный буфер для HLSL
__declspec(align(16)) struct CB_ClipmapData {
    DirectX::XMFLOAT4 CascadeScales[6];
    DirectX::XMFLOAT4 CascadeDistances[6];
    int NumCascades;
    int Resolution;

    float NearBlendStart;    // Начало растворения (в метрах)
    float NearBlendInvFade;  // 1.0f / Длина перехода (для оптимизации)
};

struct RvtCascade {
    float WorldCoverage;    // Сколько метров мира покрывает этот каскад
    float TexelSize;        // Размер одного пикселя в метрах (WorldCoverage / Resolution)

    int SnappedGridX;       // Текущая позиция сетки X
    int SnappedGridZ;       // Текущая позиция сетки Z
    bool IsFirstFrame;
};

class ClipmapRvtManager {
public:
    ClipmapRvtManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~ClipmapRvtManager();

    bool Initialize();

    // Вызывается каждый кадр. Проверяет, сдвинулась ли камера
    void Update(const DirectX::XMFLOAT3& cameraPos);

    // Забираем список кусков, которые нужно "допечь" в этом кадре
    std::vector<ClipmapUpdateTask> PopUpdateTasks();

    // Геттеры для биндинга в Terrain.hlsl
    ID3D11ShaderResourceView* GetAlbedoArraySRV() const { return m_albedoArraySRV.Get(); }
    ID3D11ShaderResourceView* GetNormalArraySRV() const { return m_normalArraySRV.Get(); }
    ID3D11Buffer* GetConstantsBuffer() const { return m_clipmapCB.Get(); }

    // Геттеры для RvtBaker.hlsl
    ID3D11UnorderedAccessView* GetAlbedoArrayUAV() const { return m_albedoArrayUAV.Get(); }
    ID3D11UnorderedAccessView* GetNormalArrayUAV() const { return m_normalArrayUAV.Get(); }
    int GetResolution() const { return m_resolution; }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    int m_resolution;
    int m_numCascades;

    std::vector<RvtCascade> m_cascades;
    std::vector<ClipmapUpdateTask> m_pendingTasks;

    ComPtr<ID3D11Texture2D> m_albedoArray;
    ComPtr<ID3D11ShaderResourceView> m_albedoArraySRV;
    ComPtr<ID3D11UnorderedAccessView> m_albedoArrayUAV;

    ComPtr<ID3D11Texture2D> m_normalArray;
    ComPtr<ID3D11ShaderResourceView> m_normalArraySRV;
    ComPtr<ID3D11UnorderedAccessView> m_normalArrayUAV;

    ComPtr<ID3D11Buffer> m_clipmapCB;
    CB_ClipmapData m_cbData;
};