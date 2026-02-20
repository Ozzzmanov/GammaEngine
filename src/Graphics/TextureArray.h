//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TextureArray.h
// Создает Texture2DArray из списка файлов.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

class TextureArray {
public:
    TextureArray(ID3D11Device* device, ID3D11DeviceContext* context);
    ~TextureArray() = default;

    // Загружает массив
    bool Initialize(const std::vector<std::string>& filenames);

    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }

private:
    // Внутренний метод создания текстуры-заглушки
    ID3D11Texture2D* CreateSolidTexture(int width, int height, DXGI_FORMAT format, uint32_t color);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3D11Texture2D> m_arrayTexture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
};