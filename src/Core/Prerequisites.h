//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Prerequisites.h
// Базовые настройки, макросы и библиотеки для всего движка.
// Подключается везде (Precompiled Header).
// ================================================================================
#pragma once

// --- System Definitions ---
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// --- Standard Library ---
#include <Windows.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <mutex>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <cmath>
#include <atomic>
#include <thread>

// --- DirectX 11 & Tools ---
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

// --- DirectXTK ---
#include <CommonStates.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <SimpleMath.h>

// --- Namespaces ---
namespace fs = std::filesystem;
using namespace Microsoft::WRL;
using namespace DirectX;
using namespace DirectX::SimpleMath;

// --- Build Configurations ---
// Настраиваем флаг Hot Reloading на основе макросов сборки.
// В Release сборке Hot Reloading должен быть СТРОГО отключен.
#if defined(_DEBUG) || defined(GAMMA_EDITOR)
#define GAMMA_USE_HOT_RELOAD
#endif

// --- Safety Macros ---
#define HR_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        Logger::Error(LogCategory::Render, std::string(msg) + " [Line: " + std::to_string(__LINE__) + "]"); \
        return false; \
    }

#define HR_CHECK_VOID(hr, msg) \
    if (FAILED(hr)) { \
        Logger::Error(LogCategory::Render, std::string(msg)); \
        return; \
    }

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }
#define UNUSED(x) (void)(x)

// --- Common Structures ---
// FIXME: В будущем вынести SimpleVertex в отдельный файл VertexTypes.h
struct SimpleVertex {
    Vector3 Pos;
    Vector3 Color;
    Vector3 Normal;
    Vector2 Tex;
    Vector4 Tangent; // Вектор Тангенса (XYZ) + знак битангенса (W)
};

// FIXME: В будущем вынести все CB_ (Constant Buffers) в отдельный файл ShaderConstants.h, 
// чтобы не засорять глобальный PCH (Precompiled Header) структурами рендера.
__declspec(align(16)) struct CB_VS_Transform {
    Matrix World;
    Matrix View;
    Matrix Projection;
    Vector4 TimeAndParams; // x = GameTime
};

// b2 (Погода)
__declspec(align(16)) struct CB_GlobalWeather {
    Vector4 WindParams1;
    Vector4 WindParams2;
    Vector4 SunDirection;
    Vector4 SunColor;        // RGB = цвет, W = Intensity
    Vector4 SunParams;
    Vector4 SunPositionNDC;

    // --- ПАРАМЕТРЫ НЕБА ---
    Vector4 SkyZenithColor;  // Цвет неба в зените (наверху)
    Vector4 SkyHorizonColor; // Цвет неба на горизонте
};