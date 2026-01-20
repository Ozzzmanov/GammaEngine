//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс BwPackedSection.
// Парсер бинарного формата BigWorld Packed Section (.cdata, .chunk).
// ================================================================================

#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

// Константы делаем доступными, так как Terrain.cpp использует их для детекции
static const uint32_t PACKED_SECTION_MAGIC = 0x62a14e45; // "EN¡b"
static const uint8_t BW_ENCRYPTION_KEY = 0x9c;

enum BwSectionType {
    TYPE_DATA_SECTION = 0,
    TYPE_STRING = 1,
    TYPE_INT = 2,
    TYPE_FLOAT = 3,
    TYPE_BOOL = 4,
    TYPE_BLOB = 5,
    TYPE_ENCRYPTED_BLOB = 6
};

class BwPackedSection {
public:
    // Структура записи потомка внутри секции
    struct ChildRecord {
        uint32_t endPosAndType;
        uint32_t keyPos;
    };

    // Фабричный метод создания секции из памяти
    static std::shared_ptr<BwPackedSection> Create(const char* data, size_t len);

    // Получить данные (Blob) этой секции
    std::vector<uint8_t> GetBlob() const { return m_data; }

    // Получить имя секции
    std::string GetName() const { return m_name; }

    // Получить список детей (полезно для отладки или сложной логики)
    const std::vector<std::shared_ptr<BwPackedSection>>& GetChildren() const { return m_children; }

private:
    BwPackedSection() = default;

    // Рекурсивная загрузка
    void Load(const char* dataBlockStart, const char* recordsStart, int numChildren, const std::vector<std::string>& stringTable);

    std::string m_name;
    std::vector<uint8_t> m_data;
    std::vector<std::shared_ptr<BwPackedSection>> m_children;
};