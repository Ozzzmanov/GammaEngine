//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// Texture.h
// ================================================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

class Texture {
public:
    Texture(const std::string& path) : m_path(path) {}
    ~Texture() = default;

    // Установить реальный ресурс (вызывается после загрузки)
    void SetSRV(ID3D11ShaderResourceView* srv) {
        m_srv = srv; // ComPtr сам увеличит счетчик ссылок
    }

    // Получить текущий ресурс (может быть заглушкой или реальной текстурой)
    ID3D11ShaderResourceView* Get() const { return m_srv.Get(); }
    ID3D11ShaderResourceView** GetAddressOf() { return m_srv.GetAddressOf(); }

    bool IsLoaded() const { return m_isLoaded; }
    void SetLoaded(bool loaded) { m_isLoaded = loaded; }

    const std::string& GetPath() const { return m_path; }

private:
    std::string m_path;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    bool m_isLoaded = false;
};