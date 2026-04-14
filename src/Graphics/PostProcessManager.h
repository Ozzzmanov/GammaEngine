//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// PostProcessManager.h
// Управляет финальными эффектами экрана: Автоэкспозиция (Eye Adaptation), 
// Bloom (Dual Filtering), Lens Flares, Uber-Tonemap (Vignette, ACES, Gamma).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "ConstantBuffer.h"
#include <vector>
#include <memory>
#include "ComputeShader.h"

__declspec(align(16)) struct CB_Bloom {
    Vector4 Params; // X = Threshold, Y = Mode, Z = TexelWidth, W = TexelHeight
};

__declspec(align(16)) struct CB_UberTonemap {
    Vector4 BloomParams;    // X = Intensity
    Vector4 VignetteParams; // X = Intensity, Y = Power
    Vector4 GrainParams;    // X = Intensity, Y = Time 
    Vector4 TonemapParams;  // X = Exposure Compensation
};

__declspec(align(16)) struct CB_AutoExposure {
    Vector4 LogLuminanceParams; // X=MinLog, Y=MaxLog, Z=MinExposure, W=MaxExposure
    Vector4 TimeParams;         // X=DeltaTime, Y=SpeedUp, Z=SpeedDown, W=TotalPixels
};

class DX11Renderer;
class WorldEnvironment;

class PostProcessManager {
public:
    PostProcessManager(DX11Renderer* renderer);
    ~PostProcessManager();

    bool Initialize(int width, int height);

    void Render(ID3D11ShaderResourceView* depthSRV, ID3D11ShaderResourceView* sceneHdrSRV,
        RenderTarget* sceneHdrTarget, RenderTarget* finalOutRT, ConstantBuffer<CB_GlobalWeather>* weatherCB);

private:
    DX11Renderer* m_renderer;

    int m_width;
    int m_height;

    // Шейдер
    std::unique_ptr<Shader> m_sunEffectsShader;
    std::unique_ptr<Shader> m_uberTonemapShader;
    std::unique_ptr<Shader> m_bloomShader;

    // Константные буферы
    std::unique_ptr<ConstantBuffer<CB_Bloom>> m_bloomCB;
    std::unique_ptr<ConstantBuffer<CB_UberTonemap>> m_uberTonemapCB;

    // Цепочка мип-мапов для Dual Kawase Bloom
    std::vector<std::unique_ptr<RenderTarget>> m_bloomChain;

    // Стейты
    ComPtr<ID3D11BlendState> m_blendStateAdditive;
    ComPtr<ID3D11DepthStencilState> m_depthStateNone;
    ComPtr<ID3D11SamplerState> m_samplerLinear;

    // Автоэкспозиция (Compute Shaders)
    std::unique_ptr<ComputeShader> m_csHistogramBuild;
    std::unique_ptr<ComputeShader> m_csHistogramAdapt;
    std::unique_ptr<ConstantBuffer<CB_AutoExposure>> m_autoExposureCB;

    ComPtr<ID3D11Buffer> m_histogramBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_histogramUAV;
    ComPtr<ID3D11ShaderResourceView> m_histogramSRV;

    ComPtr<ID3D11Texture2D> m_exposureTex[2];
    ComPtr<ID3D11UnorderedAccessView> m_exposureUAV[2];
    ComPtr<ID3D11ShaderResourceView> m_exposureSRV[2];
    int m_exposureIndex = 0;
};