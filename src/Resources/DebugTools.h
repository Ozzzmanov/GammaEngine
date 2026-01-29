//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DebugTools.h
// Инструменты для анализа форматов файлов BigWorld (Reverse Engineering).
// Позволяют просматривать структуру PackedSection и искать скрытые данные.
// ================================================================================

#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

class BwPackedSection;

class DebugTools {
public:
    // --- VLO / Water Analysis ---
    // Сканирует папку на наличие текстовых упоминаний воды (полезно, если форматы меняются)
    static void ScanForWaterKeywords(const std::string& folderPath);

    // Детальный дамп .vlo файла (позиции, настройки)
    static void AnalyzeVLO(const std::string& filepath);

    // --- Chunk / World Analysis ---
    // Выводит дерево тегов чанка в файл ChunkDump.txt
    static void DumpChunkStructure(const std::string& chunkPath);

    // Создает отчет Schema_Report.txt с частотой использования тегов и их типами
    static void ScanChunkSchema(const std::string& filepath);

    // --- Binary Data Analysis ---
    // Анализ заголовков .odata (BinSection)
    static void AnalyzeOData(const std::string& path);
    static void FullODataDecode(const std::string& folderPath);

private:
    struct TagStats {
        int count = 0;
        bool isMatrix34 = false;
        bool isString = false;
        bool containsTrees = false;
        size_t avgSize = 0;
    };

    static void RecursiveScan(std::shared_ptr<BwPackedSection> section, std::map<std::string, TagStats>& stats, int depth);
    static void DumpNodeRecursive(std::shared_ptr<BwPackedSection> node, int depth);
};