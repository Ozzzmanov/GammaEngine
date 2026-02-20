//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ModelManager.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <unordered_map>
#include <string>
#include <memory>

class StaticModel;

class ModelManager {
public:
    static ModelManager& Get() {
        static ModelManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // получить модель (или загрузить, если её нет)
    std::shared_ptr<StaticModel> GetModel(const std::string& path);

    // Хелпер для поиска файлов
    bool ResolvePath(const std::string& input, std::string& outPath);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Кэш загруженных моделей
    std::unordered_map<std::string, std::shared_ptr<StaticModel>> m_cache;
};