//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс Chunk.
// Представляет собой единицу мира (обычно 100x100 метров).
// Связывает логическую позицию в сетке с графическим представлением (Terrain).
// ================================================================================

#pragma once
#include "Terrain.h"
#include "../Graphics/ConstantBuffer.h"
#include <memory>

class Chunk {
public:
    Chunk(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Chunk() = default;

    // Загрузка данных чанка
    bool Load(const std::string& chunkFilePath, int gridX, int gridZ, const SpaceParams& params);

    // Отрисовка чанка.
    // Принимает буфер констант и матрицы камеры, чтобы рассчитать свою позицию.
    void Render(ConstantBuffer<CB_VS_Transform>* cb, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);

    // Геттеры
    std::string GetName() const { return m_chunkName; }
    int GetGridX() const { return m_gridX; }
    int GetGridZ() const { return m_gridZ; }
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }

    // Получение списка текстур для отладки
    std::vector<std::string> GetTerrainTextures() const {
        if (m_terrain) return m_terrain->GetTextureNames();
        return {};
    }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string m_chunkName;
    int m_gridX = 0;
    int m_gridZ = 0;

    // Позиция угла чанка в мире
    DirectX::XMFLOAT3 m_position = { 0, 0, 0 };

    std::unique_ptr<Terrain> m_terrain;
};