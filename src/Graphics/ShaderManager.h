//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// ShaderManager.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <map>
#include <d3d11.h>
#include <d3dcompiler.h>

class ShaderManager {
public:
    static ShaderManager& Get() { static ShaderManager s; return s; }

    void Initialize(ID3D11Device* device);

    // Главная функция возвращает скомпилированный PS с заданными макросами
    ID3D11PixelShader* GetPixelShader(const std::string& path, const std::vector<std::string>& defines);

    // Стандартный VS (он у нас один для всех)
    ID3D11VertexShader* GetVertexShader(const std::string& path, ID3DBlob** outBlob = nullptr);



private:
    ID3D11Device* m_device = nullptr;


    // Кэш: "Path|DEF1|DEF2" -> Pointer
    std::map<std::string, ComPtr<ID3D11PixelShader>> m_psCache;
    std::map<std::string, ComPtr<ID3D11VertexShader>> m_vsCache;
    std::map<std::string, ComPtr<ID3DBlob>> m_vsBlobCache; 
};