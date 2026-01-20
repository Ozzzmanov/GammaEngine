//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Реализация BwPackedSection.cpp
// ================================================================================

#include "BwPackedSection.h"
#include <cstring>

std::shared_ptr<BwPackedSection> BwPackedSection::Create(const char* data, size_t len) {
    if (len < 8) return nullptr;

    const uint32_t* magic = reinterpret_cast<const uint32_t*>(data);
    if (*magic != PACKED_SECTION_MAGIC) return nullptr;

    std::vector<std::string> stringTable;
    size_t offset = 8;

    // Чтение таблицы строк
    while (offset < len) {
        if (data[offset] == 0) { offset++; break; }
        std::string s(data + offset);
        stringTable.push_back(s);
        offset += s.length() + 1;
    }

    auto root = std::shared_ptr<BwPackedSection>(new BwPackedSection());
    root->m_name = "root";

    if (len - offset < 4) return nullptr;
    uint32_t numChildren = *reinterpret_cast<const uint32_t*>(data + offset);

    size_t headerSize = 4 + (numChildren * sizeof(ChildRecord)) + 4;
    const char* recordsStart = data + offset + 4;
    const char* dataBlockStart = data + offset + headerSize;

    if (offset + headerSize > len) return nullptr;

    root->Load(dataBlockStart, recordsStart, numChildren, stringTable);
    return root;
}

void BwPackedSection::Load(const char* dataBlockStart, const char* recordsStart, int numChildren, const std::vector<std::string>& stringTable) {
    const ChildRecord* records = reinterpret_cast<const ChildRecord*>(recordsStart);
    int currentStart = 0;

    for (int i = 0; i < numChildren; ++i) {
        uint32_t endPosAndType = records[i].endPosAndType;
        uint32_t keyIndex = records[i].keyPos;

        int type = endPosAndType >> 28;
        int endPos = endPosAndType & 0x0FFFFFFF;

        std::string name = (keyIndex < stringTable.size()) ? stringTable[keyIndex] : "unknown";
        int length = endPos - currentStart;

        auto child = std::shared_ptr<BwPackedSection>(new BwPackedSection());
        child->m_name = name;

        if (type == TYPE_DATA_SECTION && length >= 4) {
            const char* childDataPtr = dataBlockStart + currentStart;
            uint32_t grandChildren = *reinterpret_cast<const uint32_t*>(childDataPtr);
            size_t childHeaderSize = 4 + (grandChildren * sizeof(ChildRecord)) + 4;
            child->Load(childDataPtr + childHeaderSize, childDataPtr + 4, grandChildren, stringTable);
        }
        else {
            if (length > 0) {
                child->m_data.resize(length);
                memcpy(child->m_data.data(), dataBlockStart + currentStart, length);

                // Расшифровка XOR
                if (type == TYPE_ENCRYPTED_BLOB && child->m_data.size() > 0) {
                    for (size_t k = 1; k < child->m_data.size(); ++k) {
                        child->m_data[k] ^= BW_ENCRYPTION_KEY;
                    }
                    child->m_data.erase(child->m_data.begin());
                }
            }
        }
        m_children.push_back(child);
        currentStart = endPos;
    }
}