//***************************************************************************************
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 5;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Transparent,
    AlphaTested,
    Count
};

class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateWaves(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildWavesGeometry();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    RenderItem* mWavesRitem = nullptr;

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    std::unique_ptr<Waves> mWaves;

    // Render items divided by PSO.
    std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.7f * XM_PI;
    float mPhi = 0.35f * XM_PI;
    float mRadius = 130.0f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        ShapesApp theApp(hInstance);
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

ShapesApp::ShapesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
    // so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildWavesGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void ShapesApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    //step1: 
    //mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);

    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    //step 2
    mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
    // Convert Spherical to Cartesian coordinates.
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    // Build the view matrix.
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void ShapesApp::AnimateMaterials(const GameTimer& gt)
{
    // Scroll the water material texture coordinates.
    auto waterMat = mMaterials["water"].get();

    float& tu = waterMat->MatTransform(3, 0);
    float& tv = waterMat->MatTransform(3, 1);

    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    if (tu >= 1.0f)
        tu -= 1.0f;

    if (tv >= 1.0f)
        tv -= 1.0f;

    waterMat->MatTransform(3, 0) = tu;
    waterMat->MatTransform(3, 1) = tv;

    // Material has changed, so need to update cbuffer.
    waterMat->NumFramesDirty = gNumFrameResources;
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();

    // LIGHT
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

    // Lantern Lights
    float z = 20.0f;
    for (int i = 3; i < 10; i++)
    {
        mMainPassCB.Lights[i].Position = { 19.0f, 31.0f, z };
        mMainPassCB.Lights[i].Direction = { -5.0f, 0.0f, 0.0f };
        mMainPassCB.Lights[i].Strength = { 0.3f, 0.3f, 0.3f };
        mMainPassCB.Lights[i].SpotPower = 1.0;

        i++;

        mMainPassCB.Lights[i].Position = { -19.0f, 31.0f, z };
        mMainPassCB.Lights[i].Direction = { 5.0f, 0.0f, 0.0f };
        mMainPassCB.Lights[i].Strength = { 0.3f, 0.3f, 0.3f };
        mMainPassCB.Lights[i].SpotPower = 1.0;

        z += 4.0f; // increment position to next two set of lights further back
    }

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::UpdateWaves(const GameTimer& gt)
{
    // Every quarter second, generate a random wave.
    static float t_base = 0.0f;
    if ((mTimer.TotalTime() - t_base) >= 0.25f)
    {
        t_base += 0.25f;

        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

        float r = MathHelper::RandF(0.2f, 0.5f);

        mWaves->Disturb(i, j, r);
    }

    // Update the wave simulation.
    mWaves->Update(gt.DeltaTime());

    // Update the wave vertex buffer with the new solution.
    auto currWavesVB = mCurrFrameResource->WavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = mWaves->Position(i);
        v.Normal = mWaves->Normal(i);

        // Derive tex-coords from position by 
        // mapping [-w/2,w/2] --> [0,1]
        v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
        v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

        currWavesVB->CopyData(i, v);
    }

    // Set the dynamic VB of the wave renderitem to the current frame VB.
    mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void ShapesApp::LoadTextures()
{
    // Brick texture for the walls
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"../Textures/bricks.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));

    // Roof texture for the wedges
    auto roofTex = std::make_unique<Texture>();
    roofTex->Name = "roofTex";
    roofTex->Filename = L"../Textures/rooftile.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), roofTex->Filename.c_str(),
        roofTex->Resource, roofTex->UploadHeap));

    // Texture for lantern
    auto lanternTex = std::make_unique<Texture>();
    lanternTex->Name = "lanternTex";
    lanternTex->Filename = L"../Textures/lantern.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), lanternTex->Filename.c_str(),
        lanternTex->Resource, lanternTex->UploadHeap));

    // Dirt texture for the ground
    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"../Textures/tile.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));

    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"../Textures/water1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), waterTex->Filename.c_str(),
        waterTex->Resource, waterTex->UploadHeap));

    auto greenTex = std::make_unique<Texture>();
    greenTex->Name = "greenTex";
    greenTex->Filename = L"../Textures/grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), greenTex->Filename.c_str(),
        greenTex->Resource, greenTex->UploadHeap));

    auto woodTex = std::make_unique<Texture>();
    woodTex->Name = "woodTex";
    woodTex->Filename = L"../Textures/wood.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), woodTex->Filename.c_str(),
        woodTex->Resource, woodTex->UploadHeap));

    auto yellowTex = std::make_unique<Texture>();
    yellowTex->Name = "yellowTex";
    yellowTex->Filename = L"../Textures/yellow.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), yellowTex->Filename.c_str(),
        yellowTex->Resource, yellowTex->UploadHeap));

    mTextures[bricksTex->Name] = std::move(bricksTex);
    mTextures[roofTex->Name] = std::move(roofTex);
    mTextures[lanternTex->Name] = std::move(lanternTex);
    mTextures[tileTex->Name] = std::move(tileTex);
    mTextures[waterTex->Name] = std::move(waterTex);
    mTextures[greenTex->Name] = std::move(greenTex);
    mTextures[woodTex->Name] = std::move(woodTex);
    mTextures[yellowTex->Name] = std::move(yellowTex);
}

void ShapesApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        1,  // number of descriptors
        0); // register t0

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0); // register b0
    slotRootParameter[2].InitAsConstantBufferView(1); // register b1
    slotRootParameter[3].InitAsConstantBufferView(2); // register b2

    auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 8; //  Change when adding more descripotrsaf
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    //
    // Fill out the heap with actual descriptors.
    //
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto roofTex = mTextures["roofTex"]->Resource;
    auto lanternTex = mTextures["lanternTex"]->Resource;
    auto tileTex = mTextures["tileTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto greenTex = mTextures["greenTex"]->Resource;
    auto woodTex = mTextures["woodTex"]->Resource;
    auto yellowTex = mTextures["yellowTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = bricksTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = roofTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = roofTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(roofTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = lanternTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = lanternTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(lanternTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = tileTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = waterTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = greenTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(greenTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = woodTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);

    srvDesc.Format = yellowTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(yellowTex.Get(), &srvDesc, hDescriptor);
}

void ShapesApp::BuildShadersAndInputLayout()
{
    //step3
    const D3D_SHADER_MACRO defines[] =
    {
        "FOG", "1",
        NULL, NULL
    };

    const D3D_SHADER_MACRO alphaTestDefines[] =
    {
        "FOG", "1",
        "ALPHA_TEST",
        NULL, NULL
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ShapesApp::BuildWavesGeometry()
{
    std::vector<std::uint32_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
    assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for (int i = 0; i < m - 1; ++i)
    {
        for (int j = 0; j < n - 1; ++j)
        {
            indices[k] = i * n + j;
            indices[k + 1] = i * n + j + 1;
            indices[k + 2] = (i + 1) * n + j;

            indices[k + 3] = (i + 1) * n + j;
            indices[k + 4] = i * n + j + 1;
            indices[k + 5] = (i + 1) * n + j + 1;

            k += 6; // next quad
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";

    // Set dynamically.
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["water"] = submesh;

    mGeometries["waterGeo"] = std::move(geo);
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(60.0f, 60.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 1.0f, 1.0f, 20, 20);
    GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
    GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData triangularprism = geoGen.CreateTriangularPrism(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData pentagonalprism = geoGen.CreatePentagonalPrism(1.0f, 1.0f, 1.0f, 3);

    //
    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.
    //

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
    UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
    UINT wedgeVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
    UINT pyramidVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
    UINT diamondVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
    UINT triangularprismVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
    UINT pentagonalprismVertexOffset = triangularprismVertexOffset + (UINT)triangularprism.Vertices.size();

    // Cache the starting index for each object in the concatenated index buffer.
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
    UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
    UINT wedgeIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
    UINT pyramidIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
    UINT diamondIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
    UINT triangularprismIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
    UINT pentagonalprismIndexOffset = triangularprismIndexOffset + (UINT)triangularprism.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    SubmeshGeometry coneSubmesh;
    coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
    coneSubmesh.StartIndexLocation = coneIndexOffset;
    coneSubmesh.BaseVertexLocation = coneVertexOffset;

    SubmeshGeometry wedgeSubmesh;
    wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
    wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
    wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

    SubmeshGeometry pyramidSubmesh;
    pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
    pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
    pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

    SubmeshGeometry diamondSubmesh;
    diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
    diamondSubmesh.StartIndexLocation = diamondIndexOffset;
    diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

    SubmeshGeometry triangularprismSubmesh;
    triangularprismSubmesh.IndexCount = (UINT)triangularprism.Indices32.size();
    triangularprismSubmesh.StartIndexLocation = triangularprismIndexOffset;
    triangularprismSubmesh.BaseVertexLocation = triangularprismVertexOffset;

    SubmeshGeometry pentagonalprismSubmesh;
    pentagonalprismSubmesh.IndexCount = (UINT)pentagonalprism.Indices32.size();
    pentagonalprismSubmesh.StartIndexLocation = pentagonalprismIndexOffset;
    pentagonalprismSubmesh.BaseVertexLocation = pentagonalprismVertexOffset;

    //
    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.
    //

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size() +
        cone.Vertices.size() +
        wedge.Vertices.size() +
        pyramid.Vertices.size() +
        diamond.Vertices.size() +
        triangularprism.Vertices.size() +
        pentagonalprism.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cone.Vertices[i].Position;
        vertices[k].Normal = cone.Vertices[i].Normal;
        vertices[k].TexC = cone.Vertices[i].TexC;
    }

    for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = wedge.Vertices[i].Position;
        vertices[k].Normal = wedge.Vertices[i].Normal;
        vertices[k].TexC = wedge.Vertices[i].TexC;
    }

    for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pyramid.Vertices[i].Position;
        vertices[k].Normal = pyramid.Vertices[i].Normal;
        vertices[k].TexC = pyramid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = diamond.Vertices[i].Position;
        vertices[k].Normal = diamond.Vertices[i].Normal;
        vertices[k].TexC = diamond.Vertices[i].TexC;
    }

    for (size_t i = 0; i < triangularprism.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = triangularprism.Vertices[i].Position;
        vertices[k].Normal = triangularprism.Vertices[i].Normal;
        vertices[k].TexC = triangularprism.Vertices[i].TexC;
    }

    for (size_t i = 0; i < pentagonalprism.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pentagonalprism.Vertices[i].Position;
        vertices[k].Normal = pentagonalprism.Vertices[i].Normal;
        vertices[k].TexC = pentagonalprism.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
    indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
    indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
    indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
    indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
    indices.insert(indices.end(), std::begin(triangularprism.GetIndices16()), std::end(triangularprism.GetIndices16()));
    indices.insert(indices.end(), std::begin(pentagonalprism.GetIndices16()), std::end(pentagonalprism.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;
    geo->DrawArgs["cone"] = coneSubmesh;
    geo->DrawArgs["wedge"] = wedgeSubmesh;
    geo->DrawArgs["pyramid"] = pyramidSubmesh;
    geo->DrawArgs["diamond"] = diamondSubmesh;
    geo->DrawArgs["triangularprism"] = triangularprismSubmesh;
    geo->DrawArgs["pentagonalprism"] = pentagonalprismSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    //
    // PSO for opaque objects.
    //
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // step1:
    // PSO for transparent objects
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;

    //suppose that we want to blend the source and destination pixels based on the opacity of the source pixel :
    //source blend factor : D3D12_BLEND_SRC_ALPHA
    //destination blend factor : D3D12_BLEND_INV_SRC_ALPHA
    //blend operator : D3D12_BLEND_OP_ADD


    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

    //you could  specify the blend factor or not..
    //F = (r, g, b) and F = a, where the color (r, g, b,a) is supplied to the  parameter of the ID3D12GraphicsCommandList::OMSetBlendFactor method.
    //transparencyBlendDesc.SrcBlend = D3D12_BLEND_BLEND_FACTOR;
    //transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_BLEND_FACTOR;

    //Hooman: try different blend operators to see the blending effects
    //D3D12_BLEND_OP_ADD,
    //D3D12_BLEND_OP_SUBTRACT,
    //D3D12_BLEND_OP_REV_SUBTRACT,
    //D3D12_BLEND_OP_MIN,
    //D3D12_BLEND_OP_MAX

    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD,

        transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    //transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_BLUE;
    //Direct3D supports rendering to up to eight render targets simultaneously.
    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    //
    // PSO for alpha tested objects
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
    alphaTestedPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
        mShaders["alphaTestedPS"]->GetBufferSize()
    };
    alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));
}

void ShapesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void ShapesApp::BuildMaterials()
{
    int Index = 0;

    auto bricks0 = std::make_unique<Material>();
    bricks0->Name = "bricks0";
    bricks0->MatCBIndex = Index;
    bricks0->DiffuseSrvHeapIndex = Index;
    bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks0->Roughness = 0.1f;

    Index++;

    auto roof0 = std::make_unique<Material>();
    roof0->Name = "roof0";
    roof0->MatCBIndex = Index;
    roof0->DiffuseSrvHeapIndex = Index;
    roof0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    roof0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    roof0->Roughness = 0.1f;

    Index++;

    auto lantern0 = std::make_unique<Material>();
    lantern0->Name = "lantern0";
    lantern0->MatCBIndex = Index;
    lantern0->DiffuseSrvHeapIndex = Index;
    lantern0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    lantern0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    lantern0->Roughness = 0.3f;

    Index++;

    auto tile0 = std::make_unique<Material>();
    tile0->Name = "tile0";
    tile0->MatCBIndex = Index;
    tile0->DiffuseSrvHeapIndex = Index;
    tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;

    Index++;

    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = Index;
    water->DiffuseSrvHeapIndex = Index;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
    water->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    water->Roughness = 0.0f;

    Index++;

    auto green = std::make_unique<Material>();
    green->Name = "green";
    green->MatCBIndex = Index;
    green->DiffuseSrvHeapIndex = Index;
    green->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    green->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    green->Roughness = 0.0f;

    Index++;

    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = Index;
    wood->DiffuseSrvHeapIndex = Index;
    wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wood->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    wood->Roughness = 0.0f;

    Index++;

    auto yellow = std::make_unique<Material>();
    yellow->Name = "yellow";
    yellow->MatCBIndex = Index;
    yellow->DiffuseSrvHeapIndex = Index;
    yellow->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    yellow->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    yellow->Roughness = 0.0f;

    mMaterials["bricks0"] = std::move(bricks0);
    mMaterials["roof0"] = std::move(roof0);
    mMaterials["lantern0"] = std::move(lantern0);
    mMaterials["tile0"] = std::move(tile0);
    mMaterials["water"] = std::move(water);
    mMaterials["green"] = std::move(green);
    mMaterials["wood"] = std::move(wood);
    mMaterials["yellow"] = std::move(yellow);
}

void ShapesApp::BuildRenderItems()
{
    // World = Scale * Rotation * Translation
    // Rotation = RotX * RotY * RotZ;

    UINT Index = 0;

    auto wavesRitem = std::make_unique<RenderItem>();
    //wavesRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(3.0f, 1.0f, 3.0f) * XMMatrixTranslation(0.0f, -3.0f, 0.0f));
    XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.f, 5.f, 5.f));
    wavesRitem->ObjCBIndex = Index++;
    wavesRitem->Mat = mMaterials["water"].get();
    wavesRitem->Geo = mGeometries["waterGeo"].get();
    wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["water"].IndexCount;
    wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["water"].StartIndexLocation;
    wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["water"].BaseVertexLocation;

    // we use mVavesRitem in updatewaves() to set the dynamic VB of the wave renderitem to the current frame VB.
    mWavesRitem = wavesRitem.get();

    mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
    mAllRitems.push_back(std::move(wavesRitem));

    // front box
    auto boxRItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRItem->World, XMMatrixScaling(14.0f, 10.0f, 14.0f) * XMMatrixTranslation(0.0f, 3.0f, 0.0f));
    boxRItem->ObjCBIndex = Index++;
    boxRItem->Mat = mMaterials["bricks0"].get();
    boxRItem->Geo = mGeometries["shapeGeo"].get();
    boxRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRItem->IndexCount = boxRItem->Geo->DrawArgs["box"].IndexCount;
    boxRItem->StartIndexLocation = boxRItem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRItem->BaseVertexLocation = boxRItem->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRItem.get());
    mAllRitems.push_back(std::move(boxRItem));

    // front pyramid on front box
    auto pyramidRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(16.0f, 5.0f, 16.0f) * XMMatrixRotationY(3.14f) * XMMatrixTranslation(0.0f, 10.5f, 0.0f));
    XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(2.f, 8.f, 2.f));
    pyramidRitem->ObjCBIndex = Index++;
    pyramidRitem->Mat = mMaterials["roof0"].get();
    pyramidRitem->Geo = mGeometries["shapeGeo"].get();
    pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
    pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
    pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
    mAllRitems.push_back(std::move(pyramidRitem));

    auto pentagonalprismRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&pentagonalprismRitem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f) * XMMatrixRotationX(-1.57f) * XMMatrixTranslation(0.0f, 15.5f, 4.0f));
    pentagonalprismRitem->ObjCBIndex = Index++; // need to be changed
    pentagonalprismRitem->Mat = mMaterials["lantern0"].get();
    pentagonalprismRitem->Geo = mGeometries["shapeGeo"].get();
    pentagonalprismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    pentagonalprismRitem->IndexCount = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].IndexCount;
    pentagonalprismRitem->StartIndexLocation = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].StartIndexLocation;
    pentagonalprismRitem->BaseVertexLocation = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(pentagonalprismRitem.get());
    mAllRitems.push_back(std::move(pentagonalprismRitem));

    // tower with wedges and hexagon window
    for (unsigned int i = 0; i < 4; i++)
    {
        float sizeZ = 40.0f;

        boxRItem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&boxRItem->World, XMMatrixScaling(sizeZ - (5.0f * i), 12.0f, sizeZ - (5.0f * i)) * XMMatrixTranslation(0.0f, 4.0f + (8.0f * i), 25.0f));
        boxRItem->ObjCBIndex = Index++;
        boxRItem->Mat = mMaterials["bricks0"].get();
        boxRItem->Geo = mGeometries["shapeGeo"].get();
        boxRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        boxRItem->IndexCount = boxRItem->Geo->DrawArgs["box"].IndexCount;
        boxRItem->StartIndexLocation = boxRItem->Geo->DrawArgs["box"].StartIndexLocation;
        boxRItem->BaseVertexLocation = boxRItem->Geo->DrawArgs["box"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRItem.get());
        mAllRitems.push_back(std::move(boxRItem));

        auto wedgeRitem = std::make_unique<RenderItem>();

        XMMATRIX frontWedgeWorld = XMMatrixScaling(3.0f, 2.0f, sizeZ - (5.0 * i)) * XMMatrixRotationY(-1.57f) * XMMatrixTranslation(0.0f, 10.0f + (8.0 * i), 25.0f - 1.5 - (sizeZ / 2) + (2.5 * i));
        XMMATRIX backWedgeWorld = XMMatrixScaling(3.0f, 2.0f, sizeZ - (5.0 * i)) * XMMatrixRotationY(1.57f) * XMMatrixTranslation(0.0f, 10.0f + (8.0 * i), 25.0f + 1.5 + (sizeZ / 2) - (2.5 * i));

        XMStoreFloat4x4(&wedgeRitem->World, frontWedgeWorld);
        XMStoreFloat4x4(&wedgeRitem->TexTransform, XMMatrixScaling(1.f, 0.5f, 0.5f));
        wedgeRitem->ObjCBIndex = Index++;
        wedgeRitem->Mat = mMaterials["roof0"].get();
        wedgeRitem->Geo = mGeometries["shapeGeo"].get();
        wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
        wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
        wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wedgeRitem.get());
        mAllRitems.push_back(std::move(wedgeRitem));

        wedgeRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wedgeRitem->World, backWedgeWorld);
        XMStoreFloat4x4(&wedgeRitem->TexTransform, XMMatrixScaling(1.f, 0.5f, 0.5f));
        wedgeRitem->ObjCBIndex = Index++;
        wedgeRitem->Mat = mMaterials["roof0"].get();
        wedgeRitem->Geo = mGeometries["shapeGeo"].get();
        wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
        wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
        wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wedgeRitem.get());
        mAllRitems.push_back(std::move(wedgeRitem));

        XMMATRIX leftWedgeWorld = XMMatrixScaling(3.0f, 2.0f, sizeZ - (5.0 * i)) * XMMatrixTranslation((-sizeZ / 2) - 1.5 + (2.5 * i), 10.0f + (8.0 * i), 25.0f);
        XMMATRIX rightWedgeWorld = XMMatrixScaling(3.0f, 2.0f, sizeZ - (5.0 * i)) * XMMatrixRotationY(3.14f) * XMMatrixTranslation((sizeZ / 2) + 1.5 - (2.5 * i), 10.0f + (8.0 * i), 25.0f);

        wedgeRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wedgeRitem->World, leftWedgeWorld);
        XMStoreFloat4x4(&wedgeRitem->TexTransform, XMMatrixScaling(1.f, 0.5f, 0.5f));
        wedgeRitem->ObjCBIndex = Index++;
        wedgeRitem->Mat = mMaterials["roof0"].get();
        wedgeRitem->Geo = mGeometries["shapeGeo"].get();
        wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
        wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
        wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wedgeRitem.get());
        mAllRitems.push_back(std::move(wedgeRitem));

        wedgeRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wedgeRitem->World, rightWedgeWorld);
        XMStoreFloat4x4(&wedgeRitem->TexTransform, XMMatrixScaling(1.f, 0.5f, 0.5f));
        wedgeRitem->ObjCBIndex = Index++;
        wedgeRitem->Mat = mMaterials["roof0"].get();
        wedgeRitem->Geo = mGeometries["shapeGeo"].get();
        wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
        wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
        wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wedgeRitem.get());
        mAllRitems.push_back(std::move(wedgeRitem));

        // continue using prism from earlier
        for (int u = -1; u <= 1; u = u + 2) // one for each side
        {
            XMMATRIX lrPrismWorld = XMMatrixScaling(3.0f, 1.0f, 3.0f) * XMMatrixRotationX(-1.57f) * XMMatrixTranslation((14.0f - (i * 2.0f)) * u, 5.0f + (i * 8.0f), 5.0f + (i * 2.5f));

            pentagonalprismRitem = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&pentagonalprismRitem->World, lrPrismWorld);
            pentagonalprismRitem->ObjCBIndex = Index++; // need to be changed
            pentagonalprismRitem->Mat = mMaterials["lantern0"].get();
            pentagonalprismRitem->Geo = mGeometries["shapeGeo"].get();
            pentagonalprismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            pentagonalprismRitem->IndexCount = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].IndexCount;
            pentagonalprismRitem->StartIndexLocation = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].StartIndexLocation;
            pentagonalprismRitem->BaseVertexLocation = pentagonalprismRitem->Geo->DrawArgs["pentagonalprism"].BaseVertexLocation;
            mRitemLayer[(int)RenderLayer::Opaque].push_back(pentagonalprismRitem.get());
            mAllRitems.push_back(std::move(pentagonalprismRitem));
        }
    }

    // trees
    for (int i = 0; i < 6; i++)
    {

        for (int u = -1; u <= 1; u = u + 2) // one for each side
        {
            auto coneRitem = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(3.0f, 10.0f, 3.0f) * XMMatrixTranslation(25.0f * u, 7.5f, 10.0f * i));
            coneRitem->ObjCBIndex = Index++;
            coneRitem->Mat = mMaterials["green"].get();
            coneRitem->Geo = mGeometries["shapeGeo"].get();
            coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
            coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
            coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
            mRitemLayer[(int)RenderLayer::Opaque].push_back(coneRitem.get());
            mAllRitems.push_back(std::move(coneRitem));

            auto cylinderRitem = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&cylinderRitem->World, XMMatrixScaling(1.0f, 3.0f, 1.0f) * XMMatrixTranslation(25.0f * u, 1.0f, 10.0f * i));
            cylinderRitem->ObjCBIndex = Index++;
            cylinderRitem->Mat = mMaterials["wood"].get();
            cylinderRitem->Geo = mGeometries["shapeGeo"].get();
            cylinderRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
            cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
            cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
            mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinderRitem.get());
            mAllRitems.push_back(std::move(cylinderRitem));
        }
    }
    //--------------------------------------------------------------------------------------------------------------------------------

    // front triangular structure
    auto triangularprismRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&triangularprismRitem->World, XMMatrixScaling(20.0f, 5.0f, 10.0f) * XMMatrixRotationX(-1.57f) * XMMatrixTranslation(0.0f, 15.5f, 7.0f));
    XMStoreFloat4x4(&triangularprismRitem->TexTransform, XMMatrixScaling(8.f, 8.f, 8.f));
    triangularprismRitem->ObjCBIndex = Index++; 
    triangularprismRitem->Mat = mMaterials["roof0"].get();
    triangularprismRitem->Geo = mGeometries["shapeGeo"].get();
    triangularprismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    triangularprismRitem->IndexCount = triangularprismRitem->Geo->DrawArgs["triangularprism"].IndexCount;
    triangularprismRitem->StartIndexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].StartIndexLocation;
    triangularprismRitem->BaseVertexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(triangularprismRitem.get());
    mAllRitems.push_back(std::move(triangularprismRitem));

    // back triangular structure
    triangularprismRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&triangularprismRitem->World, XMMatrixScaling(8.0f, 32.0f, 10.0f) * XMMatrixRotationX(-1.57f) * XMMatrixRotationY(-1.57f) * XMMatrixTranslation(0.0f, 23.0f, 40.0f));
    XMStoreFloat4x4(&triangularprismRitem->TexTransform, XMMatrixScaling(8.f, 8.f, 8.f));
    triangularprismRitem->ObjCBIndex = Index++; 
    triangularprismRitem->Mat = mMaterials["roof0"].get();
    triangularprismRitem->Geo = mGeometries["shapeGeo"].get();
    triangularprismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    triangularprismRitem->IndexCount = triangularprismRitem->Geo->DrawArgs["triangularprism"].IndexCount;
    triangularprismRitem->StartIndexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].StartIndexLocation;
    triangularprismRitem->BaseVertexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(triangularprismRitem.get());
    mAllRitems.push_back(std::move(triangularprismRitem));

    // back pryamid structures
    for (int i = -1; i <= 1; i = i + 2) // one for each side
    {
        pyramidRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(15.0f, 7.0f, 6.0f) * XMMatrixRotationY(3.14f) * XMMatrixTranslation(9.0f * i, 13.5f, 43.0f));
        XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(2.f, 8.f, 2.f));
        pyramidRitem->ObjCBIndex = Index++;
        pyramidRitem->Mat = mMaterials["roof0"].get();
        pyramidRitem->Geo = mGeometries["shapeGeo"].get();
        pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
        pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
        pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
        mAllRitems.push_back(std::move(pyramidRitem));
    }


    // window tops
    for (int i = 0; i < 4; i++)
    {
        for (int u = -1; u <= 1; u = u + 2) // one for each side
        {
            pyramidRitem = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(6.0f, 2.0f, 6.0f) * XMMatrixRotationY(3.14f) * XMMatrixTranslation(18.0f * u, 16.0f, 13.0f + (i * 8.0f)));
            XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(2.f, 8.f, 2.f));
            pyramidRitem->ObjCBIndex = Index++;
            pyramidRitem->Mat = mMaterials["roof0"].get();
            pyramidRitem->Geo = mGeometries["shapeGeo"].get();
            pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
            pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
            pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
            mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
            mAllRitems.push_back(std::move(pyramidRitem));
        }
    }

    // pyramid roofs
    pyramidRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(25.0f, 6.0f, 25.0f) * XMMatrixRotationY(3.14f) * XMMatrixTranslation(0.0f, 37.0f, 25.0f));
    XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(4.f, 8.f, 4.f));
    pyramidRitem->ObjCBIndex = Index++;
    pyramidRitem->Mat = mMaterials["roof0"].get();
    pyramidRitem->Geo = mGeometries["shapeGeo"].get();
    pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
    pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
    pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
    mAllRitems.push_back(std::move(pyramidRitem));

    // top triangle prism
    triangularprismRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&triangularprismRitem->World, XMMatrixScaling(10.0f, 25.0f, 8.0f) * XMMatrixRotationX(-1.57f) * XMMatrixRotationY(-1.57f) * XMMatrixTranslation(0.0f, 38.0f, 25.0f));
    XMStoreFloat4x4(&triangularprismRitem->TexTransform, XMMatrixScaling(8.f, 8.f, 8.f));
    triangularprismRitem->ObjCBIndex = Index++;
    triangularprismRitem->Mat = mMaterials["roof0"].get();
    triangularprismRitem->Geo = mGeometries["shapeGeo"].get();
    triangularprismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    triangularprismRitem->IndexCount = triangularprismRitem->Geo->DrawArgs["triangularprism"].IndexCount;
    triangularprismRitem->StartIndexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].StartIndexLocation;
    triangularprismRitem->BaseVertexLocation = triangularprismRitem->Geo->DrawArgs["triangularprism"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(triangularprismRitem.get());
    mAllRitems.push_back(std::move(triangularprismRitem));

    // lower diamond lanterns
    for (int u = -1; u <= 1; u = u + 2) // one for each side
    {
        auto diamondRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(2.0f, 4.0f, 2.0f) * XMMatrixTranslation(7.5f * u, 6.0f, -8.0f));
        diamondRitem->ObjCBIndex = Index++; // need to be changed
        diamondRitem->Mat = mMaterials["yellow"].get();
        diamondRitem->Geo = mGeometries["shapeGeo"].get();
        diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
        diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
        diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondRitem.get());
        mAllRitems.push_back(std::move(diamondRitem));
    }

    // top diamond lanterns
    for (int i = -2; i < 3; i++) // left and right side
    {
        auto diamondRitem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(2.0f, 4.0f, 2.0f) * XMMatrixTranslation(0.0f + (i * 5.0), 31.0f, 10.0f));
        diamondRitem->ObjCBIndex = Index++; // need to be changed
        diamondRitem->Mat = mMaterials["yellow"].get();
        diamondRitem->Geo = mGeometries["shapeGeo"].get();
        diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
        diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
        diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondRitem.get());
        mAllRitems.push_back(std::move(diamondRitem));
    }

    // sphere lanterns
    for (int i = 0; i < 4; i++)
    {
        for (int u = -1; u <= 1; u = u + 2) // one for each side
        {
            auto sphereRitem = std::make_unique<RenderItem>();
            XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixTranslation(15.0f * u, 31.0f, 20.0f + (4.0f * i)));
            sphereRitem->ObjCBIndex = Index++; // need to be changed
            sphereRitem->Mat = mMaterials["lantern0"].get();
            sphereRitem->Geo = mGeometries["shapeGeo"].get();
            sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
            sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
            sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
            mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem.get());
            mAllRitems.push_back(std::move(sphereRitem));
        }
    }

    auto gridRitem = std::make_unique<RenderItem>();
    //gridRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixTranslation(0.0f, 0.0f, 20.0f));
    gridRitem->ObjCBIndex = Index++;
    gridRitem->Mat = mMaterials["tile0"].get();
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
    mAllRitems.push_back(std::move(gridRitem));

    // All the render items are opaque.
    /*for (auto& e : mAllRitems)
        mOpaqueRitems.push_back(e.get());*/
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}

