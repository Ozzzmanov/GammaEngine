#include "Graphics/Terrain.h"
#include "Resources/HeightMap.h"
#include "Core/stb_image.h"
#include "Core/DDSTextureLoader.h" 

#include <fstream>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <filesystem>
#include <windows.h> 

using namespace DirectX;

void SetRed()    { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY); }
void SetGreen()  { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
void SetYellow() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
void SetBlue()   { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_GREEN); }
void SetWhite()  { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); }

static const unsigned char PNG_SIG[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

Terrain::Terrain(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
    CreateDebugTexture();

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(XMFLOAT4);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&bd, nullptr, m_pixelDebugBuffer.GetAddressOf());
}

size_t Terrain::FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset) {
    if (startOffset >= data.size()) return std::string::npos;
    auto it = std::search(data.begin() + startOffset, data.end(), pattern.begin(), pattern.end());
    return (it != data.end()) ? std::distance(data.begin(), it) : std::string::npos;
}

const unsigned char* GetPngData(const std::vector<char>& fileData, size_t nameOffset, size_t fileSize, int& outMaxLen) {
    size_t searchEnd = (std::min)(fileSize, nameOffset + 512);
    for (size_t i = nameOffset; i <= searchEnd - 8; ++i) {
        bool match = true;
        for (int j = 0; j < 8; ++j) if ((unsigned char)fileData[i + j] != PNG_SIG[j]) { match = false; break; }
        if (match) {
            outMaxLen = (int)(fileSize - i);
            return reinterpret_cast<const unsigned char*>(&fileData[i]);
        }
    }
    return nullptr;
}

// фильтр текстур
std::vector<std::string> ExtractTexturePaths(const std::vector<char>& data) {
    std::vector<std::string> foundPaths;
    const std::string extensions[] = { ".dds", ".tga", ".png", ".jpg" };
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '.') {
            for (const auto& ext : extensions) {
                if (i + ext.length() <= data.size()) {
                    bool match = true;
                    for (size_t k = 0; k < ext.length(); ++k) if (std::tolower(data[i + k]) != ext[k]) match = false;
                    if (match) {
                        size_t start = i;
                        while (start > 0 && data[start - 1] >= 32 && data[start - 1] <= 126 && data[start - 1] != '\\') start--;
                        std::string path;
                        for (size_t k = start; k < i + ext.length(); ++k) path += data[k];
                        
                        // Игнорируем внутренние ресурсы
                        if (path.find("lodTexture") != std::string::npos) continue;
                        if (path.length() < 5) continue;

                        bool exists = false;
                        for (const auto& p : foundPaths) if (p == path) exists = true;
                        if (!exists) foundPaths.push_back(path);
                    }
                }
            }
        }
    }
    return foundPaths;
}

bool Terrain::LoadTextureFromFile(const std::string& relativePath) {
    // Если путь пустой (например, для плоских чанков), просто выходим
    if (relativePath.empty()) return false;

    std::string fullPath = "Assets/" + relativePath;
    std::filesystem::path p(fullPath);
    p.replace_extension(".dds");

    HRESULT hr = E_FAIL;
    std::wstring widePath = p.wstring();

    hr = CreateDDSTextureFromFile(m_device.Get(), m_context.Get(), widePath.c_str(), 
        (ID3D11Resource**)m_texture.ReleaseAndGetAddressOf(), m_textureView.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        // Fallback: ищем в корне Assets
        std::wstring wideName = L"Assets/" + p.filename().wstring();
        hr = CreateDDSTextureFromFile(m_device.Get(), m_context.Get(), wideName.c_str(),
            (ID3D11Resource**)m_texture.ReleaseAndGetAddressOf(), m_textureView.ReleaseAndGetAddressOf());
    }

    if (SUCCEEDED(hr)) {
        SetGreen(); std::cout << "   [Texture] Loaded: " << p.filename().string() << std::endl; SetWhite();
        return true;
    } 
    return false;
}

void Terrain::CreateDebugTexture() {
    //FIX ME Серый квадрат 1x1 для заглушки (нужна дефолтная текстура для space)
    const int w = 2, h = 2;
    uint32_t pixels[w * h] = { 0xFF808080, 0xFFAAAAAA, 0xFFAAAAAA, 0xFF808080 };
    
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels; initData.SysMemPitch = w * 4;
    m_device->CreateTexture2D(&desc, &initData, m_texture.GetAddressOf());
    m_device->CreateShaderResourceView(m_texture.Get(), nullptr, m_textureView.GetAddressOf());
}

bool Terrain::Initialize(const std::string& cdataFile) {
    std::vector<float> heights;
    std::vector<ColorRGB> normalsRaw;
    int wH = 32, dH = 32; // Дефолтный размер для плоских чанков
    int wN = 0, dN = 0;

    std::ifstream file(cdataFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    // Текстуры
    std::vector<std::string> textures = ExtractTexturePaths(fileData);
    if (!textures.empty()) LoadTextureFromFile(textures[0]);

    // Высоты
    std::string hNames[] = { "terrain2/heightshmp", "terrain2/heights1", "terrain2/heights" };
    size_t hOffset = std::string::npos;
    for (const auto& n : hNames) { hOffset = FindPattern(fileData, n, 0); if (hOffset != std::string::npos) break; }

    const float HEIGHT_SCALE = 0.001f; // Стандартный scale (игнорируется внутри LoadPackedHeight для QPNG)

    if (hOffset != std::string::npos) {
        int maxLen;
        const unsigned char* pngData = GetPngData(fileData, hOffset, fileSize, maxLen);

        if (pngData) {
            const unsigned char* chunkStart = pngData;
            int chunkLen = maxLen;

            // Вычисляем смещение PNG относительно начала файла
            size_t pngFileOffset = pngData - reinterpret_cast<const unsigned char*>(fileData.data());

            // Проверяем, есть ли перед PNG "магия" qpng и заголовок (нужно минимум 32 байта места сзади)
            // 28 байт Header + 4 байта 'qpng' = 32 байта
            if (pngFileOffset >= 32) {
                // Читаем 4 байта прямо перед PNG
                const uint32_t* potentialMagic = reinterpret_cast<const uint32_t*>(pngData - 4);

                // Проверяем на 0x71706e67 ('qpng')
                if (*potentialMagic == 0x71706e67) {
                    // Сдвигаем указатель назад к началу настоящего Заголовка
                    chunkStart = pngData - 32;
                    chunkLen = maxLen + 32;
                    // Debug
                    // std::cout << "Detected QPNG chunk!" << std::endl; 
                }
            }

            HeightMap::LoadPackedHeight(chunkStart, chunkLen, heights, wH, dH, HEIGHT_SCALE);
        }
        else {
            // Плоский чанк (читаем 1 float)
            wH = 32; dH = 32;
            heights.resize(wH * dH, 0.0f);
        }
    }

    // Отладка
    if (!heights.empty()) {
        m_minHeight = heights[0]; m_maxHeight = heights[0];
        for (float h : heights) {
            if (h < m_minHeight) m_minHeight = h;
            if (h > m_maxHeight) m_maxHeight = h;
        }
        if (m_maxHeight > 500.0f || m_minHeight < -500.0f) {
            SetYellow();
            std::cout << "[INFO] Heights: " << m_minHeight << " .. " << m_maxHeight << std::endl;
            SetWhite();
        }
    }

    // НОРМАЛИ
    size_t nOffset = FindPattern(fileData, "terrain2/normals", 0);
    if (nOffset != std::string::npos) {
        int maxLen;
        const unsigned char* pngData = GetPngData(fileData, nOffset, fileSize, maxLen);
        if (pngData) HeightMap::LoadNormalsFromMemory(pngData, maxLen, normalsRaw, wN, dN);
    }

    // Сшивка
    m_edgeLeft.clear(); m_edgeRight.clear(); m_edgeTop.clear(); m_edgeBottom.clear();
    for (int z = 0; z < dH; ++z) {
        m_edgeLeft.push_back(heights[z * wH]);
        m_edgeRight.push_back(heights[z * wH + (wH - 1)]);
    }
    for (int x = 0; x < wH; ++x) {
        m_edgeBottom.push_back(heights[x]);
        m_edgeTop.push_back(heights[(dH - 1) * wH + x]);
    }

    // Геометрия
    bool hasNormals = !normalsRaw.empty() && (wN == wH) && (dN == dH);
    std::vector<SimpleVertex> vertices;
    std::vector<unsigned long> indices;
    const float CHUNK_SIZE = 100.0f;
    float dx = CHUNK_SIZE / (float)(wH > 1 ? wH - 1 : 1);
    float dz = CHUNK_SIZE / (float)(dH > 1 ? dH - 1 : 1);
    float widthOffset = (wH - 1) * dx * 0.5f;
    float depthOffset = (dH - 1) * dz * 0.5f;

    vertices.resize(wH * dH);
    for (int z = 0; z < dH; ++z) {
        for (int x = 0; x < wH; ++x) {
            SimpleVertex& v = vertices[z * wH + x];
            v.x = x * dx - widthOffset;
            v.y = heights[z * wH + x];
            v.z = z * dz - depthOffset;
            v.r = 1.0f; v.g = 1.0f; v.b = 1.0f;
            v.u = ((float)x / (float)(wH - 1)) * 4.0f; 
            v.v = ((float)z / (float)(dH - 1)) * 4.0f;
            if (hasNormals) {
                ColorRGB rgb = normalsRaw[z * wN + x];
                v.nx = (rgb.r / 255.0f) * 2 - 1; v.ny = (rgb.b / 255.0f) * 2 - 1; v.nz = (rgb.g / 255.0f) * 2 - 1;
            } else { v.nx = 0; v.ny = 1; v.nz = 0; }
        }
    }

    // Генерация нормалей, если их нет
    if (!hasNormals) {
        for (int z = 0; z < dH; ++z) {
            for (int x = 0; x < wH; ++x) {
                float hL = heights[z * wH + (x > 0 ? x - 1 : x)];
                float hR = heights[z * wH + (x < wH - 1 ? x + 1 : x)];
                float hD = heights[(z > 0 ? z - 1 : z) * wH + x];
                float hU = heights[(z < dH - 1 ? z + 1 : z) * wH + x];
                XMVECTOR tX = XMVectorSet(2.0f * dx, hR - hL, 0, 0);
                XMVECTOR tZ = XMVectorSet(0, hU - hD, 2.0f * dz, 0);
                XMVECTOR n = XMVector3Normalize(XMVector3Cross(tZ, tX));
                XMStoreFloat3((XMFLOAT3*)&vertices[z * wH + x].nx, n);
            }
        }
    }

    for (int z = 0; z < dH - 1; ++z) {
        for (int x = 0; x < wH - 1; ++x) {
            int bl = z * wH + x; int br = z * wH + (x + 1);
            int tl = (z + 1) * wH + x; int tr = (z + 1) * wH + (x + 1);
            indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
            indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
        }
    }
    m_indexCount = (UINT)indices.size();

    D3D11_BUFFER_DESC vbd = { 0 }; vbd.Usage = D3D11_USAGE_DEFAULT; vbd.ByteWidth = sizeof(SimpleVertex) * (UINT)vertices.size(); vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vInitData = { 0 }; vInitData.pSysMem = vertices.data();
    m_device->CreateBuffer(&vbd, &vInitData, m_vertexBuffer.GetAddressOf());

    D3D11_BUFFER_DESC ibd = { 0 }; ibd.Usage = D3D11_USAGE_DEFAULT; ibd.ByteWidth = sizeof(unsigned long) * m_indexCount; ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iInitData = { 0 }; iInitData.pSysMem = indices.data();
    m_device->CreateBuffer(&ibd, &iInitData, m_indexBuffer.GetAddressOf());

    return true;
}

void Terrain::Render() {
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(m_context->Map(m_pixelDebugBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        XMFLOAT4* dataPtr = (XMFLOAT4*)mappedResource.pData;
        dataPtr->x = m_debugColor.x;
        dataPtr->y = m_debugColor.y;
        dataPtr->z = m_debugColor.z;
        dataPtr->w = m_useDebugColor ? 1.0f : 0.0f;
        m_context->Unmap(m_pixelDebugBuffer.Get(), 0);
    }
    m_context->PSSetConstantBuffers(1, 1, m_pixelDebugBuffer.GetAddressOf());

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->PSSetShaderResources(0, 1, m_textureView.GetAddressOf());
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(m_indexCount, 0, 0);
}

float GetAverage(const std::vector<float>& vec) { if (vec.empty()) return 0.0f; float sum = std::accumulate(vec.begin(), vec.end(), 0.0f); return sum / vec.size(); }
float Terrain::GetLeftEdgeHeight() const { return GetAverage(m_edgeLeft); }
float Terrain::GetRightEdgeHeight() const { return GetAverage(m_edgeRight); }
float Terrain::GetTopEdgeHeight() const { return GetAverage(m_edgeTop); }
float Terrain::GetBottomEdgeHeight() const { return GetAverage(m_edgeBottom); }