//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// DebugOverlay.h
// Отрисовка отладочной информации.
// ================================================================================

#pragma once

#include <d3d11.h>
#include <memory>
#include <string>
#include <DirectXMath.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>

class DebugOverlay {
public:
    DebugOverlay(ID3D11Device* device, ID3D11DeviceContext* context);
    ~DebugOverlay();

    bool LoadContent(const std::wstring& fontPath);

    struct Stats {
        DirectX::XMFLOAT3 camPos;
        float renderDist;
        bool isCulling;
        bool isDebugCam;
        int chunksDrawn;
        int totalChunks;
        int waterCount;
        int fps;
    };

    void Render(const Stats& stats);

private:
    ID3D11Device* m_device;
    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont>  m_font;
};