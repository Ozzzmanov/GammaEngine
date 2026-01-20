//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Реализация Chunk.
// ================================================================================

#include "Chunk.h"
#include <filesystem>

namespace fs = std::filesystem;

Chunk::Chunk(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

bool Chunk::Load(const std::string& chunkFilePath, int gridX, int gridZ, const SpaceParams& params) {
    m_chunkName = fs::path(chunkFilePath).filename().string();

    // Сохраняем координаты сетки
    m_gridX = gridX;
    m_gridZ = gridZ;

    // Рассчитываем мировую позицию (0,0 угол чанка)
    // Размер чанка всегда 100 метров (стандарт BigWorld)
    m_position = XMFLOAT3(gridX * 100.0f, 0.0f, gridZ * 100.0f);

    // Пытаемся найти .cdata (геометрия)
    fs::path cdataPath = fs::path(chunkFilePath).replace_extension(".cdata");
    if (fs::exists(cdataPath)) {
        m_terrain = std::make_unique<Terrain>(m_device, m_context);

        m_terrain->SetPosition(m_position.x, m_position.y, m_position.z);

        if (!m_terrain->Initialize(cdataPath.string(), params)) {
            m_terrain.reset();
        }
    }
    return true;
}

void Chunk::Render(ConstantBuffer<CB_VS_Transform>* cb, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj) {
    if (m_terrain) {
        // Формируем матрицу мира для ЭТОГО чанка
        XMMATRIX world = XMMatrixTranslation(m_position.x, m_position.y, m_position.z);

        // Заполняем структуру данных для шейдера
        CB_VS_Transform data;
        data.World = XMMatrixTranspose(world);
        data.View = XMMatrixTranspose(view);
        data.Projection = XMMatrixTranspose(proj);

        // Обновляем буфер на видеокарте
        cb->Update(data);

        // Активируем буфер (слот b0)
        cb->BindVS(0);

        // Рисуем ландшафт
        m_terrain->Render();
    }
}