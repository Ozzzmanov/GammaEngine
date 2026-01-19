#include "Core/GammaEngine.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <sstream> 
#include "Graphics/ConsoleColor.h"

namespace fs = std::filesystem;
using namespace DirectX;

GammaEngine::GammaEngine() {
    m_window = std::make_unique<Window>();
    m_renderer = std::make_unique<DX11Renderer>();
}
GammaEngine::~GammaEngine() {}

bool GammaEngine::Initialize() {
    if (!m_window->Initialize(WIDTH, HEIGHT, "Gamma Engine")) return false;
    if (!m_renderer->Initialize(m_window->GetHWND(), WIDTH, HEIGHT)) return false;

    m_shader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shader->Load(L"Assets/Shaders/Simple.hlsl")) return false;

    m_transformBuffer = std::make_unique<ConstantBuffer>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_transformBuffer->Initialize()) return false;

    XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)WIDTH / HEIGHT, 0.1f, 25000.0f);
    XMStoreFloat4x4(&m_projectionMatrix, P);

    // Путь к папке с уровнем
    LoadLocation("Assets/outlands2");
    return true;
}

bool GammaEngine::ParseGridCoordinates(const std::string& filename, int& outX, int& outZ) {
    if (filename.length() < 9) return false;
    try {
        std::string hexX = filename.substr(0, 4);
        std::string hexZ = filename.substr(4, 4);
        unsigned int uX, uZ;
        std::stringstream ssX, ssZ;
        ssX << std::hex << hexX; ssX >> uX;
        ssZ << std::hex << hexZ; ssZ >> uZ;
        outX = (short)uX;
        outZ = (short)uZ;
        return true;
    }
    catch (...) { return false; }
}

float FindYInChunk(const std::string& chunkPath, float expectedX, float expectedZ) {
    std::ifstream file(chunkPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return 0.0f;

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    size_t floatCount = fileSize / sizeof(float);
    const float* data = reinterpret_cast<const float*>(buffer.data());
    const float EPSILON = 1.0f;

    for (size_t i = 0; i < floatCount - 16; ++i) {
        float valX = data[i + 12];
        float valY = data[i + 13];
        float valZ = data[i + 14];

        if (std::abs(valX - expectedX) < EPSILON && std::abs(valZ - expectedZ) < EPSILON) {
            float scaleX = data[i + 0];
            float scaleY = data[i + 5];
            float scaleZ = data[i + 10];
            float wComp = data[i + 15];

            bool isMatrix = (std::abs(scaleX - 1.0f) < 0.1f) &&
                (std::abs(scaleY - 1.0f) < 0.1f) &&
                (std::abs(scaleZ - 1.0f) < 0.1f) &&
                (std::abs(wComp - 1.0f) < 0.1f);

            if (isMatrix) {
                return valY;
            }
        }
    }
    return 0.0f;
}

void GammaEngine::LoadLocation(const std::string& folderPath) {
    // Проверка существования папки
    if (!std::filesystem::exists(folderPath)) {
        std::cout << RED << "[FATAL] Folder not found: " << folderPath << WHITE << std::endl;
        return;
    }

    std::cout << WHITE << "--- LOADING TERRAIN CHUNKS ---" << std::endl;
    int successCount = 0;
    int failCount = 0;

    // Рекурсивный обход директории
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
        // Пропускаем подкаталоги
        if (entry.is_directory()) continue;

        std::string filename = entry.path().filename().string();

        // Обрабатываем только файлы .cdata, пропуская индексные файлы "i.cdata"
        if (entry.path().extension() == ".cdata" && filename.find("i.cdata") == std::string::npos) {

            int gridX = 0, gridZ = 0;
            // Парсим координаты чанка из имени файла (например, "0000ffff.cdata")
            if (ParseGridCoordinates(filename, gridX, gridZ)) {

                auto terrain = std::make_unique<Terrain>(
                    m_renderer->GetDevice(),
                    m_renderer->GetContext()
                );

                // Попытка инициализации чанка
                if (terrain->Initialize(entry.path().string())) {

                    // Валидация данных высот
                    float hMin = terrain->GetMinHeight();
                    float hMax = terrain->GetMaxHeight();
                    bool isFlat = std::abs(hMax - hMin) < 0.5f;

                    // Проверка на экстремальные значения (возможная порча данных)
                    bool isExtreme = (hMin < -2000.0f || hMax > 2000.0f);

                    // Установка позиции чанка в мировых координатах
                    terrain->SetPosition(gridX * 100.0f, 0.0f, gridZ * 100.0f);

                    if (isExtreme) {
                        // ОШИБКА: Экстремальные значения высот, вероятно повреждённые данные
                        std::cout << RED << "[BAD] " << filename
                            << " (Extreme Heights: " << hMin << " to " << hMax << ")"
                            << WHITE << std::endl;
                        failCount++;
                    }
                    else if (isFlat) {
                        // ПРЕДУПРЕЖДЕНИЕ: Плоский чанк (пол или вода)
                        std::cout << YELLOW << "[WARN] " << filename
                            << " (Flat/Empty)" << WHITE << std::endl;

                        m_chunks.push_back(std::move(terrain));
                        successCount++;
                    }
                    else {
                        // УСПЕШНО загружен
                        std::cout << GREEN << "[OK] " << filename << WHITE << std::endl;

                        m_chunks.push_back(std::move(terrain));
                        successCount++;
                    }
                }
                else {
                    // ОШИБКА: Не удалось прочитать или распарсить файл
                    std::cout << RED << "[FAIL] Failed to parse: "
                        << filename << WHITE << std::endl;
                    failCount++;
                }
            }
        }
    }

    std::cout << WHITE << "--------------------------------" << std::endl;
    std::cout << GREEN << " Loaded: " << successCount << WHITE
        << " | " << RED << " Failed/Bad: " << failCount << WHITE << std::endl;
}


void GammaEngine::Update() {
    static float t = 0.0f; t += 0.0025f;
    float radius = 1200.0f;
    float posX = cos(t) * radius;
    float posZ = sin(t) * radius;
    float posY = 600.0f;

    XMVECTOR eyePos = XMVectorSet(posX, posY, posZ, 0.0f);
    XMVECTOR focusPoint = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX V = XMMatrixLookAtLH(eyePos, focusPoint, upDir);
    XMStoreFloat4x4(&m_viewMatrix, V);

    if (GetAsyncKeyState(VK_TAB) & 0x8000) {
        m_isWireframe = !m_isWireframe;
        m_renderer->SetWireframe(m_isWireframe);
        Sleep(200);
    }
}

void GammaEngine::Render() {
    m_renderer->Clear(0.1f, 0.1f, 0.2f, 1.0f);
    m_shader->Bind();

    for (const auto& chunk : m_chunks) {
        XMFLOAT3 pos = chunk->GetPosition();
        XMMATRIX world = XMMatrixTranslation(pos.x, pos.y, pos.z);
        XMStoreFloat4x4(&m_worldMatrix, world);

        CB_VS_Transform cbData;
        cbData.World = XMMatrixTranspose(world);
        cbData.View = XMMatrixTranspose(XMLoadFloat4x4(&m_viewMatrix));
        cbData.Projection = XMMatrixTranspose(XMLoadFloat4x4(&m_projectionMatrix));

        m_transformBuffer->Update(cbData);
        m_transformBuffer->Bind(0);
        chunk->Render();
    }
    m_renderer->Present();
}

void GammaEngine::Run() {
    while (m_window->ProcessMessages()) {
        Update();
        Render();
    }
}