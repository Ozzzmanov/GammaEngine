#include "MaterialSystem.h"
#include "../Core/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// ... (namespace SimpleJson оставляем как был) ...
namespace SimpleJson {
    void SkipWhitespace(const std::string& json, size_t& pos) {
        while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    }
    std::string ParseString(const std::string& json, size_t& pos) {
        SkipWhitespace(json, pos);
        if (pos >= json.length() || json[pos] != '"') return "";
        pos++; size_t start = pos;
        while (pos < json.length() && json[pos] != '"') pos++;
        std::string result = json.substr(start, pos - start);
        if (pos < json.length()) pos++;
        return result;
    }
    std::map<std::string, std::string> ParseStringMap(const std::string& content) {
        std::map<std::string, std::string> result;
        size_t pos = 0;
        SkipWhitespace(content, pos); if (pos >= content.length() || content[pos] != '{') return result; pos++;
        while (pos < content.length()) {
            SkipWhitespace(content, pos); if (content[pos] == '}') break;
            std::string key = ParseString(content, pos);
            SkipWhitespace(content, pos); if (content[pos] == ':') pos++;
            std::string value = ParseString(content, pos);
            if (!key.empty()) result[key] = value;
            SkipWhitespace(content, pos); if (content[pos] == ',') pos++;
        }
        return result;
    }
    std::map<std::string, std::vector<std::string>> ParseArrayMap(const std::string& content) {
        std::map<std::string, std::vector<std::string>> result;
        size_t pos = 0;
        SkipWhitespace(content, pos); if (pos >= content.length() || content[pos] != '{') return result; pos++;
        while (pos < content.length()) {
            SkipWhitespace(content, pos); if (content[pos] == '}') break;
            std::string key = ParseString(content, pos);
            SkipWhitespace(content, pos); if (content[pos] == ':') pos++;
            SkipWhitespace(content, pos);
            if (content[pos] == '[') {
                pos++;
                std::vector<std::string> arr;
                while (pos < content.length()) {
                    SkipWhitespace(content, pos); if (content[pos] == ']') { pos++; break; }
                    std::string val = ParseString(content, pos);
                    arr.push_back(val);
                    SkipWhitespace(content, pos); if (content[pos] == ',') pos++;
                }
                result[key] = arr;
            }
            SkipWhitespace(content, pos); if (content[pos] == ',') pos++;
        }
        return result;
    }
}

// ============================================================================
// IMPLEMENTATION
// ============================================================================

void MaterialSystem::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    // Создаем буфер констант (Один раз при старте движка)
    m_cbMaterial = std::make_unique<ConstantBuffer<CB_Material>>(dev, ctx);
    m_cbMaterial->Initialize(true);
    Logger::Info(LogCategory::General, "MaterialSystem: Initialized buffers.");
}

void MaterialSystem::LoadLevelData(const std::string& levelPath) {
    // Очищаем старые данные
    m_variantsDB.clear();
    m_manifestDB.clear();

    Logger::Info(LogCategory::General, "MaterialSystem: Loading data from " + levelPath);

    // 1. Materials.json
    std::string matPath = levelPath + "/materials.json";
    std::ifstream fMat(matPath);
    if (fMat.is_open()) {
        std::stringstream buffer; buffer << fMat.rdbuf();
        m_variantsDB = SimpleJson::ParseArrayMap(buffer.str());
        Logger::Info(LogCategory::General, "MaterialSystem: Loaded " + std::to_string(m_variantsDB.size()) + " variants.");
    }
    else {
        Logger::Error(LogCategory::General, "MaterialSystem: FAILED to load " + matPath);
    }

    // 2. Models.json
    std::string modPath = levelPath + "/models.json";
    std::ifstream fMod(modPath);
    if (fMod.is_open()) {
        std::stringstream buffer; buffer << fMod.rdbuf();
        m_manifestDB = SimpleJson::ParseStringMap(buffer.str());
        Logger::Info(LogCategory::General, "MaterialSystem: Loaded " + std::to_string(m_manifestDB.size()) + " models mapping.");
    }
    else {
        Logger::Error(LogCategory::General, "MaterialSystem: FAILED to load " + modPath);
    }
}

MaterialConfig MaterialSystem::GetConfig(const std::string& modelPath, size_t loadedTextureCount) {
    MaterialConfig config;
    if (m_manifestDB.empty()) return config;

    // Нормализуем путь для поиска в БД
    std::string cleanPath = modelPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // Убираем префиксы Assets/ или res/, чтобы совпадало с ключами в JSON
    if (cleanPath.find("Assets/") == 0) cleanPath = cleanPath.substr(7);
    if (cleanPath.find("res/") == 0) cleanPath = cleanPath.substr(4);

    std::string variantName = "";
    if (m_manifestDB.count(cleanPath)) {
        variantName = m_manifestDB[cleanPath];
    }
    else {
        // Fallback: если модель не найдена, пробуем угадать по количеству текстур
        if (loadedTextureCount >= 2) {
            config.macros.push_back("HAS_DIFFUSE"); config.textureSlots[0] = 1; // Предполагаем Diffuse на 2 месте
            config.macros.push_back("HAS_NORMAL");  config.textureSlots[1] = 0; // Предполагаем Normal на 1 месте
            return config;
        }
        config.macros.push_back("HAS_DIFFUSE");
        config.textureSlots[0] = 0;
        return config;
    }

    if (m_variantsDB.count(variantName) == 0) return config;
    const auto& layout = m_variantsDB[variantName];

    for (size_t i = 0; i < layout.size(); ++i) {
        // ВАЖНО: Если мы нашли в XML меньше текстур, чем требует JSON, прерываемся,
        // чтобы не вылезти за пределы массива texPaths в StaticModel
        if (i >= loadedTextureCount) break;

        std::string type = layout[i];

        // Маппинг типов на слоты
        // slot 0 = Diffuse, 1 = Normal, 2 = Specular, 3 = Lightmap, 4 = Mask

        if (type == "Diffuse") {
            config.macros.push_back("HAS_DIFFUSE");
            config.textureSlots[0] = (int)i; // Текстура #i из XML идет в слот 0
        }
        else if (type == "Normal") {
            config.macros.push_back("HAS_NORMAL");
            config.textureSlots[1] = (int)i;
        }
        else if (type == "Specular") {
            config.macros.push_back("HAS_SPECULAR");
            config.textureSlots[2] = (int)i;
        }
        else if (type == "Lightmap") {
            config.macros.push_back("HAS_LIGHTMAP");
            config.textureSlots[3] = (int)i;
        }
        else if (type == "Mask") {
            config.macros.push_back("HAS_MASK");
            config.textureSlots[4] = (int)i;
        }
    }
    return config;
}

void MaterialSystem::BindMaterialBuffer() {
    if (!m_cbMaterial) return; // Защита от краша, если Initialize не был вызван

    CB_Material data;
    data.BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    data.AlphaCutoff = 0.5f;
    data.SpecularPower = 16.0f;

    m_cbMaterial->UpdateDynamic(data);
    m_cbMaterial->BindPS(1);
}