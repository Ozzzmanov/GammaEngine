//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TextureArray.h
// Обертка над ID3D11Texture2DArray. 
// Позволяет загружать готовые запеченные массивы (быстро) или 
// динамически собирать их из списка файлов (медленно, только для Editor/Baker).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <vector>
#include <string>

/**
 * @class TextureArray
 * @brief Управляет аппаратным массивом текстур в видеопамяти.
 */
class TextureArray {
public:
    static constexpr int DEFAULT_RESOLUTION = 1024;

public:
    TextureArray(ID3D11Device* device, ID3D11DeviceContext* context);
    ~TextureArray() = default;

    /// @brief Динамически собирает Texture2DArray из списка файлов.
    /// FIXME: Использовать ТОЛЬКО в редакторе или бейкере. В рантайме вызывает фризы.
    bool Initialize(const std::vector<std::string>& filenames, int targetW, int targetH);

    /// @brief Мгновенно загружает готовый массив из .dds файла напрямую в VRAM.
    bool LoadFromBakedFile(const std::string& filePath);

    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3D11Texture2D> m_arrayTexture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
};