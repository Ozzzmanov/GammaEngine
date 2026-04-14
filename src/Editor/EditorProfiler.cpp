//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// EditorProfiler.cpp
// ================================================================================
#include "EditorProfiler.h"

#ifdef GAMMA_EDITOR
#include "Vendor/ImGui/imgui.h"
#include "../Core/TaskScheduler.h" 
#include <numeric>
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>

#pragma comment(lib, "dxgi.lib")

void EditorProfiler::Initialize(ID3D11Device* device) {
    m_device = device;
}

void EditorProfiler::CollectSystemMetrics() {
    // RAM (Использование процессом)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        m_ramUsageMb = (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
    }

    // RAM (Общий объем системы)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        m_ramTotalMb = (float)memInfo.ullTotalPhys / (1024.0f * 1024.0f);
    }

    // VRAM
    if (!m_device) return;
    ComPtr<IDXGIDevice> dxgiDevice;
    if (SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
        ComPtr<IDXGIAdapter> dxgiAdapter;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
            ComPtr<IDXGIAdapter3> dxgiAdapter3;
            if (SUCCEEDED(dxgiAdapter.As(&dxgiAdapter3))) {
                DXGI_QUERY_VIDEO_MEMORY_INFO info;
                if (SUCCEEDED(dxgiAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                    m_vramUsageMb = (float)info.CurrentUsage / (1024.0f * 1024.0f);
                    m_vramBudgetMb = (float)info.Budget / (1024.0f * 1024.0f);
                }
            }
        }
    }
}

void EditorProfiler::UpdateAndRender(const FrameProfilerStats& stats) {
    float totalMs = stats.UpdateTimeMs + stats.RenderPipelineTimeMs + stats.EditorTimeMs + stats.PresentTimeMs;

    m_frameTimeHistory.push_back(totalMs);
    if (m_frameTimeHistory.size() > MAX_HISTORY) m_frameTimeHistory.erase(m_frameTimeHistory.begin());

    m_fpsHistory.push_back((float)stats.FPS);
    if (m_fpsHistory.size() > MAX_HISTORY) m_fpsHistory.erase(m_fpsHistory.begin());

    // Обновляем системные метрики раз в секунду
    m_sysInfoTimer += totalMs * 0.001f;
    if (m_sysInfoTimer >= 1.0f) {
        CollectSystemMetrics();
        m_sysInfoTimer = 0.0f;
    }

    ImGui::Begin("Profiler & Performance");

    if (m_frameTimeHistory.empty()) {
        ImGui::End();
        return;
    }

    float avgMs = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0f) / m_frameTimeHistory.size();

    // ОБЩИЙ ГРАФИК FPS
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "FPS: %d", stats.FPS);
    ImGui::SameLine();
    ImGui::Text("| Frame: %.2f ms (Avg: %.2f ms)", totalMs, avgMs);
    ImGui::PlotLines("##FrameTime", m_frameTimeHistory.data(), (int)m_frameTimeHistory.size(), 0, nullptr, 0.0f, 33.3f, ImVec2(0, 60));

    ImGui::Separator();

    //FLAME GRAPH (Визуальная хронология кадра)
    ImGui::Text("Frame Breakdown (Visual):");

    ImVec2 p = ImGui::GetCursorScreenPos();
    float barWidth = ImGui::GetContentRegionAvail().x;
    float barHeight = 20.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (totalMs > 0.0f) {
        float x = p.x;

        // Синий - Game Logic
        float wLogic = (stats.UpdateTimeMs / totalMs) * barWidth;
        drawList->AddRectFilled(ImVec2(x, p.y), ImVec2(x + wLogic, p.y + barHeight), IM_COL32(50, 150, 250, 255));
        x += wLogic;

        // Зеленый - Render
        float wRender = (stats.RenderPipelineTimeMs / totalMs) * barWidth;
        drawList->AddRectFilled(ImVec2(x, p.y), ImVec2(x + wRender, p.y + barHeight), IM_COL32(50, 200, 50, 255));
        x += wRender;

        // Фиолетовый - Editor UI
        float wEditor = (stats.EditorTimeMs / totalMs) * barWidth;
        drawList->AddRectFilled(ImVec2(x, p.y), ImVec2(x + wEditor, p.y + barHeight), IM_COL32(200, 50, 200, 255));
        x += wEditor;

        // Оранжевый - GPU Wait
        float wPresent = (stats.PresentTimeMs / totalMs) * barWidth;
        drawList->AddRectFilled(ImVec2(x, p.y), ImVec2(x + wPresent, p.y + barHeight), IM_COL32(250, 150, 50, 255));
    }
    ImGui::Dummy(ImVec2(barWidth, barHeight));

    // Текстовая расшифровка
    ImGui::Columns(2, "Breakdown", false);
    ImGui::SetColumnWidth(0, 160.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
    ImGui::Text("Logic (Update):"); ImGui::PopStyleColor(); ImGui::NextColumn(); ImGui::Text("%.2f ms", stats.UpdateTimeMs); ImGui::NextColumn();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    ImGui::Text("Render Pipeline:"); ImGui::PopStyleColor(); ImGui::NextColumn(); ImGui::Text("%.2f ms", stats.RenderPipelineTimeMs); ImGui::NextColumn();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.8f, 1.0f));
    ImGui::Text("ImGui Build:"); ImGui::PopStyleColor(); ImGui::NextColumn(); ImGui::Text("%.2f ms", stats.EditorTimeMs); ImGui::NextColumn();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    ImGui::Text("GPU Wait (Present):"); ImGui::PopStyleColor(); ImGui::NextColumn(); ImGui::Text("%.2f ms", stats.PresentTimeMs); ImGui::NextColumn();

    ImGui::Columns(1);

    ImGui::Separator();


    // МУЛЬТИПОТОЧНОСТЬ (Task Scheduler)
    ImGui::Text("Multithreading (TaskScheduler):");
    ImGui::Text("Worker Threads:         %d", (int)TaskScheduler::Get().GetWorkerCount());
    ImGui::Text("Pending BG Tasks:       %d", (int)TaskScheduler::Get().GetPendingBackgroundTasks());
    ImGui::Text("Pending Main Callbacks: %d", (int)TaskScheduler::Get().GetPendingMainCallbacks());

    ImGui::Separator();

    // СИСТЕМНЫЕ РЕСУРСЫ (RAM & VRAM Progress Bars)
    ImGui::Text("Hardware Resources:");

    // RAM Прогресс-бар
    std::string ramStr = std::to_string((int)m_ramUsageMb) + " / " + std::to_string((int)m_ramTotalMb) + " MB";
    float ramFrac = (m_ramTotalMb > 0) ? (m_ramUsageMb / m_ramTotalMb) : 0.0f;
    ImGui::ProgressBar(ramFrac, ImVec2(-1, 0), ramStr.c_str());
    ImGui::Text("System RAM");

    // VRAM Прогресс-бар
    std::string vramStr = std::to_string((int)m_vramUsageMb) + " / " + std::to_string((int)m_vramBudgetMb) + " MB";
    float vramFrac = (m_vramBudgetMb > 0) ? (m_vramUsageMb / m_vramBudgetMb) : 0.0f;
    ImGui::ProgressBar(vramFrac, ImVec2(-1, 0), vramStr.c_str());
    ImGui::Text("GPU VRAM (Local)");

    ImGui::End();
}
#endif // GAMMA_EDITOR