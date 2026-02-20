//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ModelManager.cpp
// ================================================================================
#include "ModelManager.h"
#include "../Graphics/StaticModel.h"
#include "../Core/Logger.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

void ModelManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;
    Logger::Info(LogCategory::General, "ModelManager Initialized.");
}

bool ModelManager::ResolvePath(const std::string& input, std::string& outPath) {
    if (input.empty()) return false;

    std::string clean = input;
    // Очистка пробелов
    size_t first = clean.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return false;
    size_t last = clean.find_last_not_of(" \t\n\r");
    clean = clean.substr(first, (last - first + 1));

    // Нормализация слешей
    std::replace(clean.begin(), clean.end(), '\\', '/');

    // Проверки существования
    if (fs::exists(clean)) { outPath = clean; return true; }
    if (fs::exists("Assets/" + clean)) { outPath = "Assets/" + clean; return true; }
    if (fs::exists("res/" + clean)) { outPath = "res/" + clean; return true; }

    // Фикс для путей вида "/models/..."
    if (!clean.empty() && clean[0] == '/') {
        std::string sub = clean.substr(1);
        if (fs::exists("Assets/" + sub)) { outPath = "Assets/" + sub; return true; }
    }

    return false;
}

std::shared_ptr<StaticModel> ModelManager::GetModel(const std::string& path) {
    if (!m_device) {
        Logger::Error(LogCategory::General, "ModelManager not initialized!");
        return nullptr;
    }

    // щем в кэше
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }

    // Если не нашли то загружаем
    auto model = std::make_shared<StaticModel>(m_device, m_context);

    // Если загрузка успешна сохраняем в кэш
    if (model->Load(path)) {
        m_cache[path] = model;
        return model;
    }

    // Если загрузка провалилась (файла нет) — возвращаем nullptr
    // (StaticModel сам напишет Warning)
    return nullptr;
}