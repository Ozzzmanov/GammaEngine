//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// BwBinSection.h
// Парсер бинарного формата BigWorld (*.odata).
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

class BwBinSection {
public:
    struct SectionEntry {
        uint32_t offset;
        uint32_t length;
    };

    // Создает экземпляр и копирует данные внутрь для безопасности
    static std::shared_ptr<BwBinSection> Create(const char* data, size_t len);

    // Получить данные секции по имени (например "alpha" или "rigid")
    bool GetSectionData(const std::string& name, std::vector<uint8_t>& outData);

    const std::unordered_map<std::string, SectionEntry>& GetSections() const { return m_sections; }
    const std::vector<uint8_t>& GetRawData() const { return m_storage; }

private:
    BwBinSection() = default;

    // Внутреннее хранилище данных (чтобы избежать Use-After-Free)
    std::vector<uint8_t> m_storage;

    // Таблица смещений
    std::unordered_map<std::string, SectionEntry> m_sections;
};