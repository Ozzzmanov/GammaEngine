//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DebugOverlay.cpp
// Реализация GUI отладки.
// ================================================================================

#include "DebugOverlay.h"
#include "../Core/Logger.h"
#include <cmath>

using namespace DirectX;

DebugOverlay::DebugOverlay(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device)
{
    m_spriteBatch = std::make_unique<SpriteBatch>(context);
}

DebugOverlay::~DebugOverlay() {}

bool DebugOverlay::LoadContent(const std::wstring& fontPath) {
    try {
        m_font = std::make_unique<SpriteFont>(m_device, fontPath.c_str());
        return true;
    }
    catch (...) {
        Logger::Error(LogCategory::General, "GUI: Failed to load font.");
        return false;
    }
}

void DebugOverlay::Render(const Stats& stats) {
    if (!m_font || !m_spriteBatch) return;

    m_spriteBatch->Begin();

    int curX = (int)std::floor(stats.camPos.x / 100.0f);
    int curZ = (int)std::floor(stats.camPos.z / 100.0f);

    std::wstring cullStatus = stats.isCulling ? L"ON" : L"OFF";
    std::wstring debugCamStatus = stats.isDebugCam ? L" [FROZEN]" : L" [FOLLOW]";

    std::wstring debugInfo =
        L"Pos: " + std::to_wstring((int)stats.camPos.x) + L", " +
        std::to_wstring((int)stats.camPos.y) + L", " + std::to_wstring((int)stats.camPos.z) +
        L"\nChunk Grid: [" + std::to_wstring(curX) + L", " + std::to_wstring(curZ) + L"]" +
        L"\nRender Dist (Z/X): " + std::to_wstring((int)stats.renderDist) + L"m" +
        L"\nCulling (F1): " + cullStatus +
        L"\nDebug Cam (F2): " + debugCamStatus +
        L"\nChunks Visible: " + std::to_wstring(stats.chunksDrawn) + L" / " + std::to_wstring(stats.totalChunks) +
        L"\nWater Objects: " + std::to_wstring(stats.waterCount);

    // Тень (Black) + Текст (Yellow)
    m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(11, 11), Colors::Black);
    m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(10, 10), Colors::Yellow);

    if (stats.isDebugCam) {
        m_font->DrawString(m_spriteBatch.get(), L"DEBUG CAMERA ACTIVE - FRUSTUM FROZEN",
            XMFLOAT2(1920 / 2 - 150, 50), Colors::Red);
    }

    m_spriteBatch->End();
}