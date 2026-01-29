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

struct SimpleVertex {
    Vector3 Pos;
    Vector3 Color;
    Vector3 Normal;
    Vector2 Tex;
};

__declspec(align(16)) struct CB_VS_Transform {
    Matrix World;
    Matrix View;
    Matrix Projection;
};