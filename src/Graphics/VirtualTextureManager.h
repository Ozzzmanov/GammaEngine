// ================================================================================
// VirtualTextureManager.h
// Менеджер Runtime Virtual Texturing (RVT).
// Управляет страницами, кэшем и обратной связью от GPU.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "TerrainGpuScene.h"
#include <vector>
#include <unordered_map>

class RvtBaker;
class TerrainArrayManager;
class LevelTextureManager;

struct RVTPageNode {
    uint32_t PhysicalX, PhysicalY; // Координаты в Физическом Кэше (VRAM)
    uint32_t VirtualX, VirtualY;   // Координаты в мире (Чанк / Тайл)
    uint32_t MipLevel;
    uint32_t LastAccessedFrame;    // Для алгоритма LRU (вытеснение старых)
    bool IsOccupied;
};

class VirtualTextureManager {
public:
    VirtualTextureManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~VirtualTextureManager();

    bool Initialize(int physicalSize, int pageSize);
    void Update(uint32_t currentFrame);

    void ProcessFeedback(
        uint32_t currentFrame,
        ID3D11Buffer* feedbackStagingBuffer,
        RvtBaker* baker,
        TerrainArrayManager* arrayMgr,
        LevelTextureManager* texManager,
        const std::vector<ChunkGpuData>& chunkData
    );

    // Геттеры для шейдеров
    ID3D11ShaderResourceView* GetPhysicalAlbedoSRV() const { return m_physicalAlbedoSRV.Get(); }
    ID3D11ShaderResourceView* GetPhysicalNormalSRV() const { return m_physicalNormalSRV.Get(); }
    ID3D11ShaderResourceView* GetPageTableSRV()      const { return m_pageTableSRV.Get(); }

    ID3D11UnorderedAccessView* GetPhysicalAlbedoUAV() const { return m_physicalAlbedoUAV.Get(); }
    ID3D11UnorderedAccessView* GetPhysicalNormalUAV() const { return m_physicalNormalUAV.Get(); }
    ID3D11UnorderedAccessView* GetPageTableUAV()      const { return m_pageTableUAV.Get(); }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::unordered_map<uint32_t, int> m_pageTableMap;

    void UpdatePageTableTexture(uint32_t virtX, uint32_t virtY, uint32_t physX, uint32_t physY);

    int m_physicalSize;
    int m_pageSize;
    int m_pagesPerRow;

    // --- ФИЗИЧЕСКИЙ КЭШ (VRAM) ---
    ComPtr<ID3D11Texture2D> m_physicalAlbedo;
    ComPtr<ID3D11Texture2D> m_physicalNormal;
    ComPtr<ID3D11ShaderResourceView> m_physicalAlbedoSRV;
    ComPtr<ID3D11ShaderResourceView> m_physicalNormalSRV;
    ComPtr<ID3D11UnorderedAccessView> m_physicalAlbedoUAV; // Для Compute Shader (запекание)
    ComPtr<ID3D11UnorderedAccessView> m_physicalNormalUAV;

    // --- ТАБЛИЦА СТРАНИЦ (Page Table) ---
    // Формат R16G16_UINT: X = PhysX, Y = PhysY
    ComPtr<ID3D11Texture2D> m_pageTable;
    ComPtr<ID3D11ShaderResourceView> m_pageTableSRV;
    ComPtr<ID3D11UnorderedAccessView> m_pageTableUAV;

    // --- CPU СТРУКТУРЫ ---
    std::vector<RVTPageNode> m_pagePool; // Все доступные слоты в физическом кэше
};