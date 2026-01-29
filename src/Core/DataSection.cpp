//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DataSection.cpp
// ================================================================================

#include "DataSection.h"
#include "Logger.h"
#include "../Resources/BwPackedSection.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void DataSection::ConvertPackedRecursive(std::shared_ptr<BwPackedSection> packed, DataSectionPtr data) {
    if (!packed || !data) return;

    // Получаем значение из бинарного блока
    data->setString(packed->GetValueAsString());

    for (const auto& childPacked : packed->GetChildren()) {
        DataSectionPtr childData = std::make_shared<DataSection>(childPacked->GetName());
        data->m_children.insert({ childPacked->GetName(), childData });

        // Рекурсия
        ConvertPackedRecursive(childPacked, childData);
    }
}

// ============================================================================
// FACTORY & PARSING
// ============================================================================

DataSectionPtr DataSection::CreateFromXML(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    // Попытка загрузить как бинарный PackedSection (BigWorld Binary XML)
    if (fileSize >= 4) {
        uint32_t magic = *reinterpret_cast<uint32_t*>(buffer.data());
        if (magic == PACKED_SECTION_MAGIC) {
            auto packedRoot = BwPackedSection::Create(buffer.data(), fileSize);
            if (!packedRoot) return nullptr;

            DataSectionPtr root = std::make_shared<DataSection>("root");
            ConvertPackedRecursive(packedRoot, root);

            // Если корень содержит только один узел, возвращаем его
            if (root->m_children.size() == 1) {
                return root->m_children.begin()->second;
            }
            return root;
        }
    }

    // Fallback: Загрузка как текстовый XML
    std::string content(buffer.begin(), buffer.end());
    DataSectionPtr root = std::make_shared<DataSection>("root");
    size_t pos = 0;
    ParseXMLRecursive(content, pos, root);

    if (root->m_children.size() == 1) {
        return root->m_children.begin()->second;
    }
    return root;
}

void DataSection::ParseXMLRecursive(const std::string& content, size_t& pos, DataSectionPtr parent) {
    size_t len = content.length();

    while (pos < len) {
        // Ищем начало тега <
        size_t startTag = content.find('<', pos);
        if (startTag == std::string::npos) {
            pos = len;
            return;
        }

        if (startTag + 4 <= len && content[startTag + 1] == '!' && content[startTag + 2] == '-' && content[startTag + 3] == '-') {
            size_t endComment = content.find("-->", startTag);
            if (endComment != std::string::npos) {
                pos = endComment + 3;
                continue; // Теперь continue внутри цикла while
            }
        }

        // Ищем конец тега >
        size_t endTag = content.find('>', startTag);
        if (endTag == std::string::npos) {
            pos = len;
            return;
        }

        // Имя тега
        std::string tagName = content.substr(startTag + 1, endTag - startTag - 1);

        // Убираем атрибуты
        size_t spacePos = tagName.find(' ');
        if (spacePos != std::string::npos) tagName = tagName.substr(0, spacePos);

        // Если это закрывающий тег </...>, выходим на уровень вверх
        if (!tagName.empty() && tagName[0] == '/') {
            pos = endTag + 1;
            return;
        }

        // Проверка на самозакрывающийся тег <tag />
        bool selfClosing = (!tagName.empty() && tagName.back() == '/');
        if (selfClosing) tagName.pop_back();

        DataSectionPtr current = std::make_shared<DataSection>(tagName);
        parent->m_children.insert({ tagName, current });
        pos = endTag + 1;

        if (selfClosing) continue;

        // Ищем контент или вложенные теги
        size_t nextLeft = content.find('<', pos);

        if (nextLeft != std::string::npos) {
            // Если между тегами есть текст, это значение
            if (nextLeft > pos) {
                std::string val = content.substr(pos, nextLeft - pos);
                // Trim пробелов
                size_t first = val.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) {
                    size_t last = val.find_last_not_of(" \t\r\n");
                    current->m_value = val.substr(first, (last - first + 1));
                }
            }

            // Если сразу закрывающий тег </tag>
            if (nextLeft + 1 < len && content[nextLeft + 1] == '/') {
                size_t closeRight = content.find('>', nextLeft);
                if (closeRight != std::string::npos) {
                    pos = closeRight + 1;
                }
                else {
                    pos = len;
                }
            }
            else {
                // Иначе вложенные теги -> рекурсия
                ParseXMLRecursive(content, pos, current);
            }
        }
        else {
            pos = len;
        }
    }
}

// ============================================================================
// DATA ACCESS
// ============================================================================

DataSectionPtr DataSection::openSection(const std::string& tagPath) {
    // Поддержка путей вида "grandparent/parent/child"
    size_t slash = tagPath.find('/');
    std::string currentTag = (slash == std::string::npos) ? tagPath : tagPath.substr(0, slash);

    auto it = m_children.find(currentTag);
    if (it != m_children.end()) {
        if (slash == std::string::npos) return it->second;
        return it->second->openSection(tagPath.substr(slash + 1));
    }
    return nullptr;
}

std::vector<DataSectionPtr> DataSection::openSections(const std::string& tagPath) {
    std::vector<DataSectionPtr> result;
    auto range = m_children.equal_range(tagPath);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

std::string DataSection::readString(const std::string& tagPath, const std::string& defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    return sect ? sect->asString() : defaultVal;
}

int DataSection::readInt(const std::string& tagPath, int defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;
    try { return std::stoi(sect->asString()); }
    catch (...) { return defaultVal; }
}

bool DataSection::readBool(const std::string& tagPath, bool defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;
    std::string s = sect->asString();
    return (s == "true" || s == "TRUE" || s == "1");
}

// ============================================================================
// LOCALE-SAFE FLOAT PARSING
// ============================================================================

float DataSection::readFloat(const std::string& tagPath, float defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;

    std::stringstream ss(sect->asString());
    ss.imbue(std::locale::classic()); // Force '.' as decimal separator
    float val;
    ss >> val;
    return ss.fail() ? defaultVal : val;
}

Vector2 DataSection::readVector2(const std::string& tagPath, const Vector2& defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;

    std::stringstream ss(sect->asString());
    ss.imbue(std::locale::classic());

    float x, y;
    ss >> x >> y;
    if (ss.fail()) return defaultVal;
    return Vector2(x, y);
}

Vector3 DataSection::readVector3(const std::string& tagPath, const Vector3& defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;

    std::stringstream ss(sect->asString());
    ss.imbue(std::locale::classic());

    float x, y, z;
    ss >> x >> y >> z;
    if (ss.fail()) return defaultVal;
    return Vector3(x, y, z);
}

Vector4 DataSection::readVector4(const std::string& tagPath, const Vector4& defaultVal) {
    DataSectionPtr sect = openSection(tagPath);
    if (!sect) return defaultVal;

    std::stringstream ss(sect->asString());
    ss.imbue(std::locale::classic());

    float x, y, z, w;
    ss >> x >> y >> z >> w;
    if (ss.fail()) return defaultVal;
    return Vector4(x, y, z, w);
}