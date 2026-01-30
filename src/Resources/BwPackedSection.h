//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// BwPackedSection.h
// Парсер упакованного XML формата BigWorld (*.o, *.chunk, .vlo).
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

static const uint32_t PACKED_SECTION_MAGIC = 0x62a14e45;

enum BwSectionType {
    TYPE_DATA_SECTION = 0x0,
    TYPE_STRING = 0x1,
    TYPE_INT = 0x2,
    TYPE_FLOAT = 0x3,
    TYPE_BOOL = 0x4,
    TYPE_BLOB = 0x5,
    TYPE_UNKNOWN = 0x7
};

class BwPackedSection {
public:
    static std::shared_ptr<BwPackedSection> Create(const char* data, size_t len);

    std::string GetName() const { return m_name; }

    // Возвращает строковое представление значения
    std::string GetValueAsString() const;

    // Прямой доступ к вектору (если тип FLOAT, size=12)
    DirectX::XMFLOAT3 AsVector3() const;

    // Хелперы для поиска GUID в сырых данных
    static std::string ExtractGUID(const std::vector<uint8_t>& blob);
    static std::string ExtractWaterGUID(const std::vector<uint8_t>& blob);

    const std::vector<uint8_t>& GetBlob() const { return m_data; }
    const std::vector<std::shared_ptr<BwPackedSection>>& GetChildren() const { return m_children; }

    DirectX::XMFLOAT4X4 AsMatrix();

private:
    BwPackedSection() = default;

    bool Load(const char* dataBlockStart, const char* recordsStart, int numChildren,
        const std::vector<std::string>& stringTable, const char* fileStart, const char* fileEnd);

    std::string m_name;
    std::vector<uint8_t> m_data;
    std::vector<std::shared_ptr<BwPackedSection>> m_children;
    BwSectionType m_type = TYPE_DATA_SECTION;
};