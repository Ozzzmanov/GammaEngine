//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// BwBinSection.cpp
// ================================================================================

#include "BwBinSection.h"
#include "../Core/Logger.h"
#include <cstring>

static const uint32_t BIN_SECTION_MAGIC = 0x42a14e65; // eNЎB

std::shared_ptr<BwBinSection> BwBinSection::Create(const char* data, size_t len) {
    if (len < 12) return nullptr;

    // Проверка Magic Number
    if (*reinterpret_cast<const uint32_t*>(data) != BIN_SECTION_MAGIC) {
        return nullptr;
    }

    auto instance = std::shared_ptr<BwBinSection>(new BwBinSection());

    // Копируем данные в свой буфер, чтобы не зависеть от жизни внешнего указателя
    instance->m_storage.assign(data, data + len);
    const char* safePtr = reinterpret_cast<const char*>(instance->m_storage.data());

    // Читаем длину таблицы индексов из последних 4 байт
    uint32_t indexTableLen = *reinterpret_cast<const uint32_t*>(safePtr + len - 4);
    if (indexTableLen > len) {
        Logger::Error(LogCategory::System, "BwBinSection: Corrupted Index Table Length.");
        return nullptr;
    }

    size_t indexStart = len - 4 - indexTableLen;
    if (indexStart < 4) return nullptr;

    const char* ptr = safePtr + indexStart;
    const char* endPtr = safePtr + len - 4;
    uint32_t currentDataOffset = 4; // Пропускаем Magic (4 байта) в начале

    // Парсинг таблицы секций
    while (ptr + 24 <= endPtr) {
        // Формат записи: [DataLength(4)] [Reserved(16)] [NameLength(4)] [Name...]
        uint32_t dataLen = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 20; // Skip DataLen + Reserved

        uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;

        if (ptr + nameLen > endPtr) break;

        std::string name(ptr, nameLen);

        // Выравнивание указателя имени на 4 байта
        ptr += (nameLen + 3) & ~3;

        BwBinSection::SectionEntry entry;
        entry.offset = currentDataOffset;
        entry.length = dataLen;

        instance->m_sections[name] = entry;

        // Выравнивание смещения данных на 4 байта
        currentDataOffset += (dataLen + 3) & ~3;
    }

    return instance;
}

bool BwBinSection::GetSectionData(const std::string& name, std::vector<uint8_t>& outData) {
    auto it = m_sections.find(name);
    if (it == m_sections.end()) return false;

    const auto& entry = it->second;

    // Проверка границ (на всякий случай)
    if (entry.offset + entry.length > m_storage.size()) {
        Logger::Error(LogCategory::System, "BwBinSection: Section read out of bounds.");
        return false;
    }

    try {
        outData.resize(entry.length);
        if (entry.length > 0) {
            memcpy(outData.data(), m_storage.data() + entry.offset, entry.length);
        }
        return true;
    }
    catch (...) {
        return false;
    }
}