//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// BwPackedSection.cpp
// Оптимизированный поиск GUID без лишних аллокаций.
// ================================================================================

#include "BwPackedSection.h"
#include "../Core/Logger.h"
#include <iomanip>

// --- Хелпер для проверки валидности символов GUID ---
static inline bool IsGuidChar(uint8_t c) {
    return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') ||
        c == '.';
}

// --------------------------------------------------------------------------------------
// ОПТИМИЗИРОВАННЫЙ ПОИСК GUID
// --------------------------------------------------------------------------------------
std::string BwPackedSection::ExtractWaterGUID(const std::vector<uint8_t>& blob) {
    if (blob.size() < 40) return "";

    const uint8_t* data = blob.data();
    size_t size = blob.size();

    // Ищем паттерн "water"
    // "water" is 5 chars. We need at least 35 chars before it.
    // So loop starts at 35 and goes up to size - 5.
    for (size_t i = 35; i <= size - 5; ++i) {
        if (data[i] == 'w' && data[i + 1] == 'a' && data[i + 2] == 't' && data[i + 3] == 'e' && data[i + 4] == 'r') {

            // Нашли "water". Проверяем 35 байт ПЕРЕД ним на соответствие структуре GUID
            // GUID формат BigWorld: XXXXXXXX.XXXXXXXX.XXXXXXXX.XXXXXXXX (35 символов)

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

std::string BwPackedSection::ExtractGUID(const std::vector<uint8_t>& blob) {
    if (blob.size() < 35) return "";

    const uint8_t* data = blob.data();
    size_t size = blob.size();

    // Скользящее окно 35 байт
    for (size_t i = 0; i <= size - 35; ++i) {
        // Быстрая проверка: GUID обычно начинается с цифры или буквы a-f
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
            // Маппинг типов BigWorld на наши типы
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

// --------------------------------------------------------------------------------------
// DATA ACCESS
// --------------------------------------------------------------------------------------

std::string BwPackedSection::GetValueAsString() const {
    if (m_data.empty()) return "";

    std::stringstream ss;
    ss.imbue(std::locale::classic()); // Важно для точек в float
    ss << std::fixed << std::setprecision(6); // Достаточная точность для координат

    size_t len = m_data.size();

    // Специальная обработка для векторов/float
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
    // Удаляем нуль-терминатор, если есть
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    return s;
}

DirectX::XMFLOAT3 BwPackedSection::AsVector3() const {
    if (m_type == TYPE_FLOAT && m_data.size() >= 12) {
        const float* f = reinterpret_cast<const float*>(m_data.data());
        return DirectX::XMFLOAT3(f[0], f[1], f[2]);
    }

    // Fallback: парсинг из строки (медленно, но надежно для странных форматов)
    std::string s = GetValueAsString();
    if (s.empty()) return DirectX::XMFLOAT3(0, 0, 0);

    std::stringstream ss(s);
    ss.imbue(std::locale::classic());
    float x = 0, y = 0, z = 0;
    ss >> x >> y >> z;
    return DirectX::XMFLOAT3(x, y, z);
}