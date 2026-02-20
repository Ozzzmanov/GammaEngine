//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// HzbManager.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <vector>
#include <memory>
#include <DirectXMath.h>

class ComputeShader;
class DX11Renderer;

class HzbManager {
public:
    HzbManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~HzbManager();

    // Инициализация текстур под размер экрана
    void Initialize(int width, int height);

    // Вызывать при изменении размера окна
    void Resize(int width, int height);

    // Главный метод: запускает генерацию HZB
    void Generate(DX11Renderer* renderer);

    // Получить SRV всей пирамиды (нужно для Culling Shader)
    ID3D11ShaderResourceView* GetHzbSRV() const { return m_hzbSRV.Get(); }

    // Получить размеры HZB (Mip 0)
    DirectX::XMFLOAT2 GetHzbSize() const { return m_hzbSize; }

private:
    void Cleanup();
    void CreateResources(int width, int height);

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    std::unique_ptr<ComputeShader> m_csDownscale;

    // Текстура HZB
    ComPtr<ID3D11Texture2D> m_hzbTexture;
    ComPtr<ID3D11ShaderResourceView> m_hzbSRV; // View на ВСЮ текстуру

    // Views для каждого уровня (чтобы читать i-1 и писать в i)
    std::vector<ComPtr<ID3D11ShaderResourceView>> m_hzbMipSRVs;
    std::vector<ComPtr<ID3D11UnorderedAccessView>> m_hzbMipUAVs;

    int m_width = 0;
    int m_height = 0;
    int m_mipCount = 0;
    DirectX::XMFLOAT2 m_hzbSize = { 0,0 };
};