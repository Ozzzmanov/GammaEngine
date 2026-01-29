#pragma once
#include "../Core/Prerequisites.h"

class VegetationManager {
public:
    VegetationManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~VegetationManager() = default;

    // Очистка данных перед загрузкой нового чанка/локации
    void Clear();

    // Создание буферов (вызывается после загрузки всех деревьев)
    void BuildDebugBuffers();

    // Основной рендер
    void Render(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);

    // Геттер для статистики
    int GetTotalTreeCount() const { return m_treeCount; }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    int m_treeCount = 0;
};