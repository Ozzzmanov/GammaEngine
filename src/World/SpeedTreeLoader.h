#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>

class SpeedTreeLoader {
public:
    SpeedTreeLoader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~SpeedTreeLoader() = default;

    // Загрузка дерева из файла .srt (SpeedTree Runtime)
    bool LoadTree(const std::string& filename);

    // Отрисовка загруженных деревьев
    void Render(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
};