#pragma once
#include "Core/Prerequisites.h"
#include "Graphics/Shader.h"
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <filesystem>

class Terrain {
public:
    Terrain(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Terrain() = default;

    bool Initialize(const std::string& cdataFile);
    void Render();

    void SetPosition(float x, float y, float z) { m_position = DirectX::XMFLOAT3(x, y, z); }
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }

    // Данные для сшивки краев
    float GetLeftEdgeHeight() const;
    float GetRightEdgeHeight() const;
    float GetTopEdgeHeight() const;
    float GetBottomEdgeHeight() const;

    // Данные для отладки
    float GetMinHeight() const { return m_minHeight; }
    float GetMaxHeight() const { return m_maxHeight; }

    // Включение цветовой отладки
    void SetDebugColor(float r, float g, float b) {
        m_debugColor = DirectX::XMFLOAT3(r, g, b);
        m_useDebugColor = true;
    }

private:
    void CreateDebugTexture();
    bool LoadTextureFromFile(const std::string& path);
    size_t FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_textureView;

    UINT m_indexCount = 0;
    DirectX::XMFLOAT3 m_position = { 0, 0, 0 };

    // Векторы высот по краям
    std::vector<float> m_edgeLeft;
    std::vector<float> m_edgeRight;
    std::vector<float> m_edgeTop;
    std::vector<float> m_edgeBottom;

    // Переменные отладки
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    bool m_useDebugColor = false;
    DirectX::XMFLOAT3 m_debugColor = { 1, 1, 1 };

    struct CB_PixelDebug { DirectX::XMFLOAT4 DebugColor; };
    ComPtr<ID3D11Buffer> m_pixelDebugBuffer;
};