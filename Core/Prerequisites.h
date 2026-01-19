#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h> 

#include <memory>
#include <string>
#include <vector>
#include <iostream>

using namespace Microsoft::WRL; 
using namespace DirectX;

#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }