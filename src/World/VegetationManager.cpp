#include "VegetationManager.h"
#include "../Core/Logger.h"

VegetationManager::VegetationManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

void VegetationManager::Clear() {
    m_treeCount = 0;
    // Здесь будет очистка буферов инстансинга
}

void VegetationManager::BuildDebugBuffers() {
    // Здесь будет создание VertexBuffer для инстансинга деревьев
    // Пока заглушка
}

void VegetationManager::Render(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj) {
    // Здесь будет отрисовка.
    // Если деревьев нет, просто выходим
    if (m_treeCount == 0) return;
}