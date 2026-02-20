//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// BwPackedSection.cpp
// ================================================================================

#include "BwPackedSection.h"
#include "../Core/Logger.h"
#include <iomanip>
#include <DirectXMath.h>

// --- Хелпер для проверки валидности символов GUID ---
static inline bool IsGuidChar(uint8_t c) {
    return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') ||
        c == '.';
}


// ПОИСК GUID
std::string BwPackedSection::ExtractWaterGUID(const std::vector<uint8_t>& blob) {
    if (blob.size() < 40) return "";

    const uint8_t* data = blob.data();
    size_t size = blob.size();

    for (size_t i = 35; i <= size - 5; ++i) {
        if (data[i] == 'w' && data[i + 1] == 'a' && data[i + 2] == 't' && data[i + 3] == 'e' && data[i + 4] == 'r') {

 
            size_t start = i - 35;
            int dots = 0;
            bool valid = true;

            for (size_t k = 0; k < 35; ++k) {
                uint8_t c = data[start + k];
                if (c == '.') {
                    dots++;
                }
                else if (!IsGuidChar(c)) {
                    valid = false;
                    break;
                }
            }

            if (valid && dots == 3) {
                return std::string((const char*)&data[start], 35);
            }
        }
    }
    return "";
}

DirectX::XMFLOAT4X4 BwPackedSection::AsMatrix() {
    DirectX::XMFLOAT4X4 result;
    // Дефолтное значение Identity
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixIdentity());

    // Если данных недостаточно, возвращаем Identity и не падаем
    if (m_data.size() < 48) {
        return result;
    }

    const float* f = reinterpret_cast<const float*>(m_data.data());

    if (m_data.size() == 48) { // 12 floats (Row Major без последней колонки)
        result._11 = f[0]; result._12 = f[1]; result._13 = f[2]; result._14 = 0.0f;
        result._21 = f[3]; result._22 = f[4]; result._23 = f[5]; result._24 = 0.0f;
        result._31 = f[6]; result._32 = f[7]; result._33 = f[8]; result._34 = 0.0f;
        result._41 = f[9]; result._42 = f[10]; result._43 = f[11]; result._44 = 1.0f;
    }
    else if (m_data.size() >= 64) { // 16 floats
        memcpy(&result, f, 64);
    }

    return result;
}

std::string BwPackedSection::ExtractGUID(const std::vector<uint8_t>& blob) {
    if (blob.size() < 35) return "";

    const uint8_t* data = blob.data();
    size_t size = blob.size();

    // Скользящее окно 35 байт
    for (size_t i = 0; i <= size - 35; ++i) {
        // GUID обычно начинается с цифры или буквы a-f
        if (!IsGuidChar(data[i])) continue;

        // Проверяем структуру: точки должны быть на позициях 8, 17, 26
        if (data[i + 8] == '.' && data[i + 17] == '.' && data[i + 26] == '.') {
            bool valid = true;
            for (size_t k = 0; k < 35; ++k) {
                if (!IsGuidChar(data[i + k])) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                return std::string((const char*)&data[i], 35);
            }
        }
    }
    return "";
}

// --------------------------------------------------------------------------------------
// FACTORY & LOAD
// --------------------------------------------------------------------------------------

std::shared_ptr<BwPackedSection> BwPackedSection::Create(const char* data, size_t len) {
    if (len < 10) return nullptr;
    if (*reinterpret_cast<const uint32_t*>(data) != PACKED_SECTION_MAGIC) return nullptr;

    std::vector<std::string> stringTable;
    size_t offset = 5; // Magic(4) + Version(1)

    // Чтение таблицы строк
    while (offset < len) {
        if (data[offset] == 0) { offset++; break; } // Конец таблицы строк
        stringTable.push_back(data + offset);
        offset += stringTable.back().length() + 1;
    }

    auto root = std::shared_ptr<BwPackedSection>(new BwPackedSection());
    root->m_name = "root";

    if (offset + 6 > len) return nullptr;

    uint16_t numChildren = *reinterpret_cast<const uint16_t*>(data + offset);
    const char* recordsStart = data + offset + 6;

    root->Load(nullptr, recordsStart, numChildren, stringTable, data, data + len);
    return root;
}

bool BwPackedSection::Load(const char* unused, const char* recordsStart, int numChildren,
    const std::vector<std::string>& stringTable, const char* fileStart, const char* fileEnd)
{
    if (!recordsStart || recordsStart >= fileEnd) return false;

    const char* currRecordPtr = recordsStart;
    const char* baseDataPtr = recordsStart + (numChildren * 6);
    uint32_t currentDataOffset = 0;

    for (int i = 0; i < numChildren; ++i) {
        if (currRecordPtr + 6 > fileEnd) break;

        uint16_t keyIndex = *reinterpret_cast<const uint16_t*>(currRecordPtr);
        uint32_t res = *reinterpret_cast<const uint32_t*>(currRecordPtr + 2);
        currRecordPtr += 6;

        // BigWorld packed format:
        // Старшие 4 бита 'res' - тип данных
        // Младшие 28 бит 'res' - смещение конца данных
        uint32_t bwType = (res >> 28) & 0x0F;
        uint32_t endPos = res & 0x0FFFFFFF;

        std::string name = (keyIndex < stringTable.size()) ? stringTable[keyIndex] : "unk";

        if (baseDataPtr + endPos > fileEnd) {
            endPos = (uint32_t)(fileEnd - baseDataPtr);
        }

        const char* childDataPtr = baseDataPtr + currentDataOffset;
        int length = endPos - currentDataOffset;

        auto child = std::shared_ptr<BwPackedSection>(new BwPackedSection());
        child->m_name = name;

        // Type 0 = Nested Section
        if (bwType == 0x0) {
            child->m_type = TYPE_DATA_SECTION;
            if (length >= 6) {
                uint16_t count = *reinterpret_cast<const uint16_t*>(childDataPtr);
                // Рекурсивная загрузка
                child->Load(nullptr, childDataPtr + 6, count, stringTable, fileStart, fileEnd);
            }
        }
        else {
            // Листовой узел (данные)
            if (length > 0 && (childDataPtr + length <= fileEnd)) {
                child->m_data.assign(childDataPtr, childDataPtr + length);
            }
            // Маппинг типов BigWorld
            if (bwType == 0x1) child->m_type = TYPE_STRING;
            else if (bwType == 0x2) child->m_type = TYPE_INT;
            else if (bwType == 0x3) child->m_type = TYPE_FLOAT;
            else if (bwType == 0x4) child->m_type = TYPE_BOOL;
            else child->m_type = TYPE_BLOB;
        }

        m_children.push_back(child);
        currentDataOffset = endPos;
    }
    return true;
}

std::string BwPackedSection::GetValueAsString() const {
    if (m_data.empty()) return "";

    std::stringstream ss;
    ss.imbue(std::locale::classic()); 
    ss << std::fixed << std::setprecision(6);

    size_t len = m_data.size();

    if (m_type == TYPE_FLOAT) {
        const float* f = reinterpret_cast<const float*>(m_data.data());
        if (len == 12) { // Vector3
            ss << f[0] << " " << f[1] << " " << f[2];
            return ss.str();
        }
        if (len == 16) { // Vector4
            ss << f[0] << " " << f[1] << " " << f[2] << " " << f[3];
            return ss.str();
        }
        if (len == 4) { // Float
            ss << *f;
            return ss.str();
        }
    }

    // Обработка строк и прочих типов
    std::string s(m_data.begin(), m_data.end());
    if (m_type == TYPE_STRING) {
        while (!s.empty() && s.back() == '\0') {
            s.pop_back();
        }
    }

    return s;
}

DirectX::XMFLOAT3 BwPackedSection::AsVector3() const {
    // Проверка размера
    if (m_data.size() < 12) return DirectX::XMFLOAT3(0, 0, 0);

    const float* f = reinterpret_cast<const float*>(m_data.data());
    return DirectX::XMFLOAT3(f[0], f[1], f[2]);
}