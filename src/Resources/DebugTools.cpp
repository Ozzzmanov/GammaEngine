//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DebugTools.cpp
// ================================================================================

#include "DebugTools.h"
#include "BwPackedSection.h"
#include "BwBinSection.h"
#include "../Core/Logger.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cctype>

namespace fs = std::filesystem;

// ============================================================================
// INTERNAL HELPERS (Anonymous Namespace)
// ============================================================================
namespace {

    bool IsPrintableSafe(char c) {
        return std::isprint(static_cast<unsigned char>(c));
    }

    bool IsMatrix34(const std::vector<uint8_t>& blob) {
        if (blob.size() != 48) return false;
        const float* f = reinterpret_cast<const float*>(blob.data());
        // Простая эвристика: длины базисных векторов должны быть адекватными
        float len0 = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
        return (len0 > 0.001f && len0 < 1000.0f);
    }

    bool IsTreePath(const std::string& str) {
        return str.find(".spt") != std::string::npos || str.find("speedtree") != std::string::npos;
    }

    std::string InterpretData(const std::vector<uint8_t>& data) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);

        // Matrix 4x4 check
        if (data.size() == 64) {
            const float* m = reinterpret_cast<const float*>(data.data());
            if (std::abs(m[15] - 1.0f) < 0.01f) return "[Matrix 4x4]";
        }
        // Matrix 3x4 check
        if (data.size() == 48) {
            return "[Matrix 3x4]";
        }
        // String check
        bool isText = true;
        for (size_t i = 0; i < data.size(); ++i) {
            if (i == data.size() - 1 && data[i] == 0) continue;
            if (!IsPrintableSafe((char)data[i]) && data[i] != '\t') { isText = false; break; }
        }
        if (isText && data.size() > 1) {
            std::string s(data.begin(), data.end());
            if (!s.empty() && s.back() == '\0') s.pop_back();
            return "\"" + s + "\"";
        }

        ss << "[Binary: " << data.size() << "b]";
        return ss.str();
    }

    void DumpChunkNodeRecursive(std::ofstream& out, std::shared_ptr<BwPackedSection> node, int indent) {
        std::string tab(indent * 2, ' ');
        std::string name = node->GetName();

        // Значение (если есть)
        std::string val = InterpretData(node->GetBlob());

        out << tab << "<" << name;
        if (node->GetChildren().empty()) {
            out << ">" << val << "</" << name << ">\n";
        }
        else {
            out << ">\n";
            for (auto child : node->GetChildren()) {
                DumpChunkNodeRecursive(out, child, indent + 1);
            }
            out << tab << "</" << name << ">\n";
        }
    }
}

// ============================================================================
// IMPLEMENTATION
// ============================================================================

void DebugTools::DumpChunkStructure(const std::string& chunkPath) {
    Logger::Info(LogCategory::System, "Dumping Chunk: " + chunkPath);

    std::ifstream file(chunkPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    auto root = BwPackedSection::Create(buffer.data(), size);
    if (!root) return;

    std::ofstream out("ChunkDump.txt");
    DumpChunkNodeRecursive(out, root, 0);
    out.close();
    Logger::Info(LogCategory::System, "Saved to ChunkDump.txt");
}

void DebugTools::ScanChunkSchema(const std::string& filepath) {
    Logger::Info(LogCategory::System, "Scanning Schema: " + filepath);

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    auto root = BwPackedSection::Create(buffer.data(), size);
    if (!root) return;

    std::map<std::string, TagStats> stats;
    RecursiveScan(root, stats, 0);

    std::ofstream out("Schema_Report.txt");
    out << std::left << std::setw(25) << "TAG" << "| COUNT | TYPE HINT\n";
    out << "----------------------------------------------\n";

    for (const auto& [tag, stat] : stats) {
        std::string hint = "Container";
        if (stat.isMatrix34) hint = "Transform";
        else if (stat.isString) hint = "String";
        else if (stat.avgSize > 0) hint = "Data";

        out << std::setw(25) << tag << "| " << std::setw(5) << stat.count << " | " << hint;
        if (stat.containsTrees) out << " [TREE]";
        out << "\n";
    }
    out.close();
    Logger::Info(LogCategory::System, "Report saved.");
}

void DebugTools::RecursiveScan(std::shared_ptr<BwPackedSection> section, std::map<std::string, TagStats>& stats, int depth) {
    std::string name = section->GetName();
    TagStats& stat = stats[name];
    stat.count++;

    const auto& blob = section->GetBlob();
    if (!blob.empty()) {
        stat.avgSize = (stat.avgSize + blob.size()) / 2;
        if (IsMatrix34(blob)) stat.isMatrix34 = true;

        std::string val = section->GetValueAsString();
        if (!val.empty() && IsPrintableSafe(val[0])) {
            stat.isString = true;
            if (IsTreePath(val)) stat.containsTrees = true;
        }
    }

    for (auto child : section->GetChildren()) {
        RecursiveScan(child, stats, depth + 1);
    }
}

void DebugTools::AnalyzeVLO(const std::string& path) {
    Logger::Info(LogCategory::System, "Analyzing VLO: " + path);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    auto root = BwPackedSection::Create(buffer.data(), size);
    if (root) DumpNodeRecursive(root, 0);
}

void DebugTools::DumpNodeRecursive(std::shared_ptr<BwPackedSection> node, int depth) {
    if (!node) return;
    std::string indent(depth * 2, ' ');
    std::string val = InterpretData(node->GetBlob());

    Logger::Info(LogCategory::System, indent + node->GetName() + ": " + val);

    for (auto child : node->GetChildren()) {
        DumpNodeRecursive(child, depth + 1);
    }
}

void DebugTools::ScanForWaterKeywords(const std::string& folderPath) {
    Logger::Info(LogCategory::System, "Scanning for water keywords in: " + folderPath);
    int found = 0;

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".chunk" && ext != ".vlo") continue;

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) continue;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (content.find("water") != std::string::npos || content.find("Water") != std::string::npos) {
            Logger::Info(LogCategory::System, "Water found in: " + entry.path().filename().string());
            found++;
        }
        if (found > 20) break;
    }
}

void DebugTools::AnalyzeOData(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    auto bin = BwBinSection::Create(buffer.data(), size);
    if (bin) {
        std::vector<uint8_t> data;
        if (bin->GetSectionData("alpha", data)) {
            Logger::Info(LogCategory::System, path + " contains ALPHA (" + std::to_string(data.size()) + " bytes)");
        }
        if (bin->GetSectionData("rigid", data)) {
            Logger::Info(LogCategory::System, path + " contains RIGID (" + std::to_string(data.size()) + " bytes)");
        }
    }
}

void DebugTools::FullODataDecode(const std::string& folderPath) {
    Logger::Info(LogCategory::System, "Decoding OData in: " + folderPath);
    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.path().extension() == ".odata") {
            AnalyzeOData(entry.path().string());
        }
    }
}