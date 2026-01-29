#include "SpeedTreeLoader.h"
#include "../Core/Logger.h"

SpeedTreeLoader::SpeedTreeLoader(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

bool SpeedTreeLoader::LoadTree(const std::string& filename) {
    // Заглушка: просто логируем, что попытались загрузить
    // В реальности здесь должна быть интеграция SpeedTree SDK
    // Logger::Info(LogCategory::General, "SpeedTreeLoader [STUB]: Loading " + filename);
    return true;
}

void SpeedTreeLoader::Render(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj) {
    // Заглушка отрисовки
}