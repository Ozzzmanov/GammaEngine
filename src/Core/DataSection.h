//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DataSection.h
// Универсальный контейнер данных.
// ================================================================================

#pragma once
#include "Prerequisites.h"

class BwPackedSection;

class DataSection;
using DataSectionPtr = std::shared_ptr<DataSection>;

class DataSection {
public:
    DataSection(const std::string& name = "") : m_name(name) {}
    ~DataSection() = default;

    // Фабричный метод: определяет формат (Binary/XML) и создает иерархию
    static DataSectionPtr CreateFromXML(const std::string& filename);

    // --- Навигация ---

    // Открыть одиночную секцию (например: "water/position")
    DataSectionPtr openSection(const std::string& tagPath);

    // Получить список всех секций с таким именем
    std::vector<DataSectionPtr> openSections(const std::string& tagPath);

    // --- Чтение данных ---

    std::string readString(const std::string& tagPath, const std::string& defaultVal = "");
    int         readInt(const std::string& tagPath, int defaultVal = 0);
    bool        readBool(const std::string& tagPath, bool defaultVal = false);

    // Locale-Safe чтение (всегда ожидает точку как разделитель)
    float       readFloat(const std::string& tagPath, float defaultVal = 0.0f);
    Vector2     readVector2(const std::string& tagPath, const Vector2& defaultVal = Vector2::Zero);
    Vector3     readVector3(const std::string& tagPath, const Vector3& defaultVal = Vector3::Zero);
    Vector4     readVector4(const std::string& tagPath, const Vector4& defaultVal = Vector4::Zero);

    // --- Прямой доступ ---
    std::string asString() const { return m_value; }
    void setString(const std::string& val) { m_value = val; }
    std::string sectionName() const { return m_name; }

private:
    // Рекурсивный парсер чистого XML
    static void ParseXMLRecursive(const std::string& content, size_t& pos, DataSectionPtr parent);

    // Конвертер из бинарного формата BigWorld
    static void ConvertPackedRecursive(std::shared_ptr<BwPackedSection> packed, DataSectionPtr data);

    std::string m_name;
    std::string m_value;
    std::multimap<std::string, DataSectionPtr> m_children;
};