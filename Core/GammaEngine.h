#pragma once
#include "Core/Window.h"
#include "Graphics/DX11Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/Terrain.h"
#include "Graphics/ConstantBuffer.h"
#include <vector>
#include <map> 
#include <filesystem>

class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();
    bool Initialize();
    void Run();
    void LoadLocation(const std::string& folderPath);
    void AutoAlignTerrainChunks();

private:
    void Update();
    void Render();
    bool ParseGridCoordinates(const std::string& filename, int& outX, int& outZ);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<DX11Renderer> m_renderer;
    std::unique_ptr<Shader> m_shader;

    std::vector<std::unique_ptr<Terrain>> m_chunks;

    // Карта для поиска соседей при сшивании
    std::map<std::pair<int, int>, Terrain*> m_chunkMap;

    std::unique_ptr<ConstantBuffer> m_transformBuffer;
    DirectX::XMFLOAT4X4 m_worldMatrix;
    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;

    const int WIDTH = 1280;
    const int HEIGHT = 720;
    const float CHUNK_SIZE = 100.0f;
    bool m_isWireframe = false;
};