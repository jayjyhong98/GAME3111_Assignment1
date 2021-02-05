
#include "../Common/d3dApp.h"
#include <DirectXColors.h>
#include "../Common/GeometryGenerator.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class InitDirect3DApp : public D3DApp
{
public:
	InitDirect3DApp(HINSTANCE hInstance);
	~InitDirect3DApp();

private:
	GeometryGenerator::MeshData		mCone;
	ComPtr<ID3D12Resource> mConeVtxBuffer;
	ComPtr<ID3D12Resource> mConeIdxBuffer;
	D3D12_VERTEX_BUFFER_VIEW mConeVertexBufferView;
	D3D12_VERTEX_BUFFER_VIEW mConeIndexBufferView;

public:
	virtual bool Initialize()override;

private:
	bool CreateCone();

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

};



#pragma region step1
//WinMain --> every win32 application needs a WinMain() function, like console application needs main()
//HINSTANCE, PSTR.. are part of <Windows.h>
//Windows is about Windows (window class that you register and instance of the class) and Messages
//cmdLine receives the parameter that somebody passes
#pragma endregion


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		InitDirect3DApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

InitDirect3DApp::~InitDirect3DApp()
{
}

bool InitDirect3DApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	CreateCone();

	return true;
}

bool InitDirect3DApp::CreateCone()
{
	GeometryGenerator	geometryGen;

	mCone = geometryGen.CreateCone(5.f, 10.f, 10, 10);

	const UINT vbBufferSize = (UINT)mCone.Vertices.size() * sizeof(GeometryGenerator::Vertex);

	md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vbBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mConeVtxBuffer));

	// Copy the triangle data to the vertex buffer.
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	mConeVtxBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)); //allocates a CPU virtual address range for the resource. 
	memcpy(pVertexDataBegin, &mCone.Vertices[0], vbBufferSize); //Copies the values of num bytes from the location pointed to by vertices directly to the memory block pointed to by pVertexDataBegin.
	mConeVtxBuffer->Unmap(0, nullptr);

	//Initialize the vertex buffer view.
	mConeVertexBufferView.BufferLocation = mConeVtxBuffer->GetGPUVirtualAddress();
	mConeVertexBufferView.StrideInBytes = sizeof(GeometryGenerator::Vertex);
	mConeVertexBufferView.SizeInBytes = vbBufferSize;

	const UINT ibBufferSize = (UINT)mCone.Indices32.size() * sizeof(GeometryGenerator::uint32);

	md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(ibBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mConeIdxBuffer));

	// Copy the triangle data to the vertex buffer.
	UINT8* pIndexDataBegin;
	CD3DX12_RANGE readRangeIdx(0, 0);        // We do not intend to read from this resource on the CPU.
	mConeIdxBuffer->Map(0, &readRangeIdx, reinterpret_cast<void**>(&pIndexDataBegin)); //allocates a CPU virtual address range for the resource. 
	memcpy(pIndexDataBegin, &mCone.Indices32[0], ibBufferSize); //Copies the values of num bytes from the location pointed to by vertices directly to the memory block pointed to by pVertexDataBegin.
	mConeIdxBuffer->Unmap(0, nullptr);

	//Initialize the vertex buffer view.
	mConeIndexBufferView.BufferLocation = mConeIdxBuffer->GetGPUVirtualAddress();
	mConeIndexBufferView.StrideInBytes = sizeof(GeometryGenerator::uint32);
	mConeIndexBufferView.SizeInBytes = ibBufferSize;

	return true;
}

void InitDirect3DApp::OnResize()
{
	D3DApp::OnResize();
}

void InitDirect3DApp::Update(const GameTimer& gt)
{

}



void InitDirect3DApp::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());


	// Render Shape





	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
}
