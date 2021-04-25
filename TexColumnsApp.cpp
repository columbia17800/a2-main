//***************************************************************************************
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"
#include <unordered_set>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

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
	//step1
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class TexColumnsApp : public D3DApp
{
public:
    TexColumnsApp(HINSTANCE hInstance);
    TexColumnsApp(const TexColumnsApp& rhs) = delete;
    TexColumnsApp& operator=(const TexColumnsApp& rhs) = delete;
    ~TexColumnsApp();

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
	void BuildLandGeometry();
	void BuildLabyrinthGeometry();
	void BuildWavesGeometry();
    void BuildShapeGeometry();
	void BuildTreeSpritesGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z) const;

	XMFLOAT3 GetHillsNormal(float x, float z) const;

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
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;
 
	RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 50.0f;

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
        TexColumnsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TexColumnsApp::TexColumnsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TexColumnsApp::~TexColumnsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TexColumnsApp::Initialize()
{
    if(!D3DApp::Initialize())
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
	BuildLandGeometry();
	BuildLabyrinthGeometry();
	BuildWavesGeometry();
    BuildShapeGeometry();
	BuildTreeSpritesGeometry();
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
 
void TexColumnsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void TexColumnsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
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

void TexColumnsApp::Draw(const GameTimer& gt)
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
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	//when you draw, you can set the blend factor that modulate values for a pixel shader, render target, or both.
	//You could also use the following blend factor when you set your blend to D3D12_BLEND_BLEND_FACTOR in PSO like following:	
	//transparencyBlendDesc.SrcBlend = D3D12_BLEND_BLEND_FACTOR;
	//transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_BLEND_FACTOR;
	//and then we set the blend factor here!

	//float blendFactor[4] = { 0.9f, 0.9f, 0.9f, 1.f };  //change that water to high opacity
	//float blendFactor[4] = { 0.3f, 0.3f, 0.3f, 1.f };  //change the water to high transparency
	//mCommandList->OMSetBlendFactor(blendFactor);

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

void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void TexColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TexColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void TexColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void TexColumnsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void TexColumnsApp::AnimateMaterials(const GameTimer& gt)
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

void TexColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
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

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
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

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
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
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TexColumnsApp::UpdateWaves(const GameTimer& gt)
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

void TexColumnsApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto brickTex = std::make_unique<Texture>();
	brickTex->Name = "brickTex";
	brickTex->Filename = L"Textures/bricks3.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), brickTex->Filename.c_str(),
		brickTex->Resource, brickTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"Textures/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[brickTex->Name] = std::move(brickTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void TexColumnsApp::BuildRootSignature()
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

    if(errorBlob != nullptr)
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

void TexColumnsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 7;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto brickTex = mTextures["brickTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

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

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = brickTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = brickTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(brickTex.Get(), &srvDesc, hDescriptor);


	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

void TexColumnsApp::BuildShadersAndInputLayout()
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
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");
    
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void TexColumnsApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TexColumnsApp::BuildLabyrinthGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wallh = geoGen.CreateBox(2.0f, 2.0f, 0.5f, 1);
	GeometryGenerator::MeshData wallv = geoGen.CreateBox(0.5f, 2.0f, 2.0f, 1);
	GeometryGenerator::MeshData walld = geoGen.CreateBox(0.5f, 2.0f, 0.5f, 1);

	std::vector<Vertex> vertices;
	std::vector<std::uint16_t> indices;

	std::vector<std::unordered_set<size_t>> emptyHorizontal{
		{0},
		{3, 4, 6, 8, 12, 13, 15, 17},
		{0, 1, 3, 6, 9, 13, 15, 16, 17},
		{0, 2, 3, 5, 6, 8, 10, 12, 14, 17},
		{1, 5, 6, 8, 10, 11, 13, 14, 15, 17},
		{0, 1, 2, 5, 7, 8, 10, 11, 13, 15},
		{0, 2, 4, 5, 8, 9, 11, 12, 15, 16},
		{2, 3, 5, 6, 8, 11, 12, 14, 16},
		{2, 5, 8, 11, 12, 14, 16},
		{0, 2, 3, 4, 5, 7, 8, 10, 13, 14, 16, 17},
		{1, 4, 5, 8, 10, 11, 15, 16, 17},
		{1, 2, 5, 8, 9, 11, 14, 16},
		{0, 1, 3, 5, 8, 9, 11, 12, 13, 16},
		{0, 4, 5, 6, 8, 11, 14, 16},
		{3, 4, 9, 10, 11, 12, 13, 15, 16},
		{0, 2, 5, 7, 10, 11, 13, 14, 15, 16},
		{1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16},
		{2, 3, 5, 6, 7, 8, 11, 12, 14, 16},
		{17}
	};

	std::vector<std::vector<size_t>> drawVertical{
		{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 ,11, 12, 13, 14, 15, 16, 17},
		{1, 2, 4, 6, 9, 11, 15},
		{2, 4, 11, 13, 15},
		{3, 4, 6, 9, 11, 15, 17},
		{0, 1, 5, 6, 8, 9, 13, 17},
		{2, 3, 5, 7 , 8, 9, 10, 11, 13, 14, 15},
		{1, 2, 4, 6, 8, 9, 11, 12, 14, 15, 17},
		{1, 4, 7, 10, 15, 16},
		{2, 3, 4, 5, 6, 8, 12, 14, 15, 16},
		{1, 2, 5, 9, 10, 12, 15, 16},
		{0, 1, 3, 5, 6, 8, 10, 14, 15},
		{3, 4, 5, 7, 9, 11, 12, 13, 14, 16},
		{2, 3, 4, 5, 6, 7, 11, 14, 15, 16},
		{1, 3, 5, 8, 9, 12, 13, 14, 15, 16},
		{3, 4, 6, 7, 8, 10, 12, 13, 14, 15},
		{0, 2, 3, 6, 9, 10, 11, 13, 14},
		{1, 2, 5, 7, 9, 13, 14, 15, 17},
		{0, 2, 4, 8, 9},
		{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 ,11, 12, 13, 14, 15, 16, 17}
	};

	uint16_t index = 0;

	for (size_t j = 0; j < 19; ++j) {
		for (size_t i = 0; i < 18; ++i) {
			if (emptyHorizontal.at(j).find(i) != emptyHorizontal.at(j).end())
			{
				continue;
			}
			for (size_t k = 0; k < wallh.Vertices.size(); ++k) {
				Vertex vertex;
				vertex.Normal = wallh.Vertices.at(k).Normal;
				vertex.TexC = wallh.Vertices.at(k).TexC;
				vertex.Pos.x = wallh.Vertices.at(k).Position.x + 1.0f + 2.0f * (i - 9.0f);
				vertex.Pos.y = wallh.Vertices.at(k).Position.y + 2.8f;
				vertex.Pos.z = wallh.Vertices.at(k).Position.z + 2.0f * (j - 9.0f);
				vertices.push_back(vertex);
			}
			for (size_t k = 0; k < wallh.Indices32.size(); ++k) {
				indices.push_back(wallh.Indices32.at(k) + index);
			}
			index += wallh.Vertices.size();
		}
	}

	for (size_t j = 0; j < 19; ++j) {
		for (size_t i = 0; i < 19; ++i) {
			for (size_t k = 0; k < walld.Vertices.size(); ++k) {
				Vertex vertex;
				vertex.Normal = walld.Vertices.at(k).Normal;
				vertex.TexC = walld.Vertices.at(k).TexC;
				vertex.Pos.x = walld.Vertices.at(k).Position.x + 2.0f * (i - 9.0f);
				vertex.Pos.y = walld.Vertices.at(k).Position.y + 2.8f;
				vertex.Pos.z = walld.Vertices.at(k).Position.z + 2.0f * (j - 9.0f);
				vertices.push_back(vertex);
			}
			for (size_t k = 0; k < walld.Indices32.size(); ++k) {
				indices.push_back(walld.Indices32.at(k) + index);
			}
			index += walld.Vertices.size();
		}
	}

	for (size_t j = 0; j < 19; ++j) {
		for (auto i : drawVertical.at(j)) {
			for (size_t k = 0; k < wallv.Vertices.size(); ++k) {
				Vertex vertex;
				vertex.Normal = wallv.Vertices.at(k).Normal;
				vertex.TexC = wallv.Vertices.at(k).TexC;
				vertex.Pos.x = wallv.Vertices.at(k).Position.x + 2.0f * (j - 9.0f);
				vertex.Pos.y = wallv.Vertices.at(k).Position.y + 2.8f;
				vertex.Pos.z = wallv.Vertices.at(k).Position.z + 1.0f + 2.0f * (i - 9.0f);
				vertices.push_back(vertex);
			}
			for (size_t k = 0; k < wallv.Indices32.size(); ++k) {
				indices.push_back(wallv.Indices32.at(k) + index);
			}
			index += wallv.Vertices.size();
		}
	}


	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "wallGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["wall"] = submesh;

	mGeometries["wallGeo"] = std::move(geo);
}

void TexColumnsApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
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
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

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
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TexColumnsApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(2.0f, 0.5f, 40, 40);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(2.0f, 5.0f, 20, 20);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(2.0f, 0.0f, 5.0f, 6u);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(2.0f, 2.0f, 2.0f, 3u);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(2.0f, 4.0f, 20, 20);
	GeometryGenerator::MeshData prism = geoGen.CreateTriangularPrism(2.0f, 4.0f, 20);
	GeometryGenerator::MeshData box = geoGen.CreateBox(2.0f, 12.f, 2.f, 3);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.2f, 1.2f, 12.f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT sphereVertexOffset = (UINT)box.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT torusVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT coneVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
	UINT pyramidVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT wedgeVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT diamondVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT prismVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT sphereIndexOffset = (UINT)box.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT torusIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT coneIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
	UINT pyramidIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT wedgeIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT diamondIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT prismIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry prismSubmesh;
	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();
	prismSubmesh.StartIndexLocation = prismIndexOffset;
	prismSubmesh.BaseVertexLocation = prismVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		torus.Vertices.size() +
		cone.Vertices.size() +
		pyramid.Vertices.size() +
		wedge.Vertices.size() +
		diamond.Vertices.size() +
		prism.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
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

	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
		vertices[k].TexC = torus.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
		vertices[k].TexC = wedge.Vertices[i].TexC;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}

	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = prism.Vertices[i].Position;
		vertices[k].Normal = prism.Vertices[i].Normal;
		vertices[k].TexC = prism.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));

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
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["prism"] = prismSubmesh;


	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::BuildTreeSpritesGeometry()
{
	////step5
	//struct TreeSpriteVertex
	//{
	//	XMFLOAT3 Pos;
	//	XMFLOAT2 Size;
	//};

	//static const int treeCount = 16;
	//std::array<TreeSpriteVertex, 16> vertices;
	//for (UINT i = 0; i < treeCount; ++i)
	//{
	//	float x = MathHelper::RandF(-45.0f, 45.0f);
	//	float z = MathHelper::RandF(-45.0f, 45.0f);
	//	float y = GetHillsHeight(x, z);

	//	// Move tree slightly above land height.
	//	y += 8.0f;

	//	vertices[i].Pos = XMFLOAT3(x, y, z);
	//	vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	//}

	//std::array<std::uint16_t, 16> indices =
	//{
	//	0, 1, 2, 3, 4, 5, 6, 7,
	//	8, 9, 10, 11, 12, 13, 14, 15
	//};

	//const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	//const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	//auto geo = std::make_unique<MeshGeometry>();
	//geo->Name = "treeSpritesGeo";

	//ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	//CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	//ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	//CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	//geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
	//	mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	//geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
	//	mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	//geo->VertexByteStride = sizeof(TreeSpriteVertex);
	//geo->VertexBufferByteSize = vbByteSize;
	//geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	//geo->IndexBufferByteSize = ibByteSize;

	//SubmeshGeometry submesh;
	//submesh.IndexCount = (UINT)indices.size();
	//submesh.StartIndexLocation = 0;
	//submesh.BaseVertexLocation = 0;

	//geo->DrawArgs["points"] = submesh;

	//mGeometries["treeSpritesGeo"] = std::move(geo);



	//step5
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	int treeCount = 0;
	std::vector<TreeSpriteVertex> vertices;
	std::vector<std::uint16_t> indices;
	for (int i = -60; i <= 60; i += 120)
	{
		for (int j = 0; j <= 120; j += 10) {
			float x = i;
			float z = j - 60;
			float y = GetHillsHeight(x, z);

			// Move tree slightly above land height.
			y += 8.0f;

			TreeSpriteVertex treeVertex = TreeSpriteVertex();
			treeVertex.Pos = XMFLOAT3(x, y, z);
			treeVertex.Size = XMFLOAT2(20.0f, 20.0f);
			vertices.push_back(std::move(treeVertex));
			indices.push_back(treeCount++);
		}
	}

	for (int i = -60; i <= 60; i += 120)
	{
		for (int j = -50; j <= 50; j += 10) {
			float x = j;
			float z = i;

			if ((x == 0 || x == 10 || x == -10) && z == -60) continue;

			float y = GetHillsHeight(x, z);

			// Move tree slightly above land height.
			y += 8.0f;

			TreeSpriteVertex treeVertex = TreeSpriteVertex();
			treeVertex.Pos = XMFLOAT3(x, y, z);
			treeVertex.Size = XMFLOAT2(20.0f, 20.0f);
			vertices.push_back(std::move(treeVertex));
			indices.push_back(treeCount++);
		}



	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);

}

void TexColumnsApp::BuildPSOs()
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

	
	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	//step1
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void TexColumnsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TexColumnsApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
    bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;


	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 3;
	grass->DiffuseSrvHeapIndex = 3;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 4;
	water->DiffuseSrvHeapIndex = 4;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto bricks3 = std::make_unique<Material>();
	bricks3->Name = "bricks3";
	bricks3->MatCBIndex = 5;
	bricks3->DiffuseSrvHeapIndex = 5;
	bricks3->DiffuseAlbedo = XMFLOAT4(Colors::RosyBrown);
	bricks3->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks3->Roughness = 0.1f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 6;
	treeSprites->DiffuseSrvHeapIndex = 6;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["bricks3"] = std::move(bricks3);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["treeSprites"] = std::move(treeSprites);

}

void TexColumnsApp::BuildRenderItems()
{

	UINT objCBIndex = 0;

	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = objCBIndex++;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
	mAllRitems.push_back(std::move(wavesRitem));

	auto wallRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRitem->World,  XMMatrixTranslation(0.0f, 0.0f, -40.0f)); //-7.5f
	wallRitem->ObjCBIndex = objCBIndex++;
	wallRitem->Mat = mMaterials["tile0"].get();
	wallRitem->Geo = mGeometries["wallGeo"].get();
	wallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem->IndexCount = wallRitem->Geo->DrawArgs["wall"].IndexCount;
	wallRitem->StartIndexLocation = wallRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	wallRitem->BaseVertexLocation = wallRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem.get());
	mAllRitems.push_back(std::move(wallRitem));
	
	
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.2f, 1.3f, 2.2f) * XMMatrixTranslation(-4.4f, 1.5f + 6.f, -10.9f)); //-7.5f
	boxRitem->ObjCBIndex = objCBIndex++;
	boxRitem->Mat = mMaterials["bricks3"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));


	auto boxRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem1->World, XMMatrixScaling(2.2f, 1.3f, 2.2f) * XMMatrixTranslation(4.4f, 1.5f + 6.f, -10.9f)); //-7.5f
	boxRitem1->ObjCBIndex = objCBIndex++;
	boxRitem1->Mat = mMaterials["bricks3"].get();
	boxRitem1->Geo = mGeometries["shapeGeo"].get();
	boxRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem1->IndexCount = boxRitem1->Geo->DrawArgs["box"].IndexCount;
	boxRitem1->StartIndexLocation = boxRitem1->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem1->BaseVertexLocation = boxRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem1.get());
	mAllRitems.push_back(std::move(boxRitem1));


	auto boxRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem2->World, XMMatrixScaling(8.5f, 1.3f, 1.3f) * XMMatrixTranslation(0.f, 1.5f + 6.f, -10.3f)); //-7.5f
	boxRitem2->ObjCBIndex = objCBIndex++;
	boxRitem2->Mat = mMaterials["bricks3"].get();
	boxRitem2->Geo = mGeometries["shapeGeo"].get();
	boxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem2->IndexCount = boxRitem2->Geo->DrawArgs["box"].IndexCount;
	boxRitem2->StartIndexLocation = boxRitem2->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem2->BaseVertexLocation = boxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem2.get());
	mAllRitems.push_back(std::move(boxRitem2));

	
	auto boxRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem3->World, XMMatrixScaling(5.8f, 0.86f, 0.9f) * XMMatrixTranslation(-13.7f, -1.06f + 6.f, -9.8f)); //-7.5f
	boxRitem3->ObjCBIndex = objCBIndex++;
	boxRitem3->Mat = mMaterials["bricks3"].get();
	boxRitem3->Geo = mGeometries["shapeGeo"].get();
	boxRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem3->IndexCount = boxRitem3->Geo->DrawArgs["box"].IndexCount;
	boxRitem3->StartIndexLocation = boxRitem3->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem3->BaseVertexLocation = boxRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem3.get());
	mAllRitems.push_back(std::move(boxRitem3));

	auto boxRitem4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem4->World, XMMatrixScaling(5.8f, 0.86f, 0.9f) * XMMatrixTranslation(13.7f, -1.06f + 6.f, -9.8f)); //-7.5f
	boxRitem4->ObjCBIndex = objCBIndex++;
	boxRitem4->Mat = mMaterials["bricks3"].get();
	boxRitem4->Geo = mGeometries["shapeGeo"].get();
	boxRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem4->IndexCount = boxRitem4->Geo->DrawArgs["box"].IndexCount;
	boxRitem4->StartIndexLocation = boxRitem4->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem4->BaseVertexLocation = boxRitem4->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem4.get());
	mAllRitems.push_back(std::move(boxRitem4));



	//left wall
	auto boxRitem6 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem6->World, XMMatrixScaling(2.9f, 1.1f, 2.9f)* XMMatrixTranslation(-17.8f, 0.7f + 6.f, 4.f)); //-7.5f
	boxRitem6->ObjCBIndex = objCBIndex++;
	boxRitem6->Mat = mMaterials["bricks3"].get();
	boxRitem6->Geo = mGeometries["shapeGeo"].get();
	boxRitem6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem6->IndexCount = boxRitem6->Geo->DrawArgs["box"].IndexCount;
	boxRitem6->StartIndexLocation = boxRitem6->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem6->BaseVertexLocation = boxRitem6->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem6.get());
	mAllRitems.push_back(std::move(boxRitem6));

	auto boxRitem5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem5->World, XMMatrixScaling(6.2f, 0.86f, 0.9f)* XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(-17.8f, -0.65f + 6.f, -2.15f)); //-7.5f
	boxRitem5->ObjCBIndex = objCBIndex++;
	boxRitem5->Mat = mMaterials["bricks3"].get();
	boxRitem5->Geo = mGeometries["shapeGeo"].get();
	boxRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem5->IndexCount = boxRitem5->Geo->DrawArgs["box"].IndexCount;
	boxRitem5->StartIndexLocation = boxRitem5->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem5->BaseVertexLocation = boxRitem5->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem5.get());
	mAllRitems.push_back(std::move(boxRitem5));

	auto boxRitem7 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem7->World, XMMatrixScaling(6.2f, 0.86f, 0.9f)* XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(-17.8f, -0.65f + 6.f, 12.15f)); //-7.5f
	boxRitem7->ObjCBIndex = objCBIndex++;
	boxRitem7->Mat = mMaterials["bricks3"].get();
	boxRitem7->Geo = mGeometries["shapeGeo"].get();
	boxRitem7->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem7->IndexCount = boxRitem7->Geo->DrawArgs["box"].IndexCount;
	boxRitem7->StartIndexLocation = boxRitem7->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem7->BaseVertexLocation = boxRitem7->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem7.get());
	mAllRitems.push_back(std::move(boxRitem7));



	//right wall


	auto boxRitem8 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem8->World, XMMatrixScaling(2.9f, 1.1f, 2.9f)* XMMatrixTranslation(17.8f, 0.7f + 6.f, 4.f)); //-7.5f
	boxRitem8->ObjCBIndex = objCBIndex++;
	boxRitem8->Mat = mMaterials["bricks3"].get();
	boxRitem8->Geo = mGeometries["shapeGeo"].get();
	boxRitem8->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem8->IndexCount = boxRitem8->Geo->DrawArgs["box"].IndexCount;
	boxRitem8->StartIndexLocation = boxRitem8->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem8->BaseVertexLocation = boxRitem8->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem8.get());
	mAllRitems.push_back(std::move(boxRitem8));

	auto boxRitem9 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem9->World, XMMatrixScaling(6.2f, 0.86f, 0.9f)* XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(17.8f, -0.65f + 6.f, -2.15f)); //-7.5f
	boxRitem9->ObjCBIndex = objCBIndex++;
	boxRitem9->Mat = mMaterials["bricks3"].get();
	boxRitem9->Geo = mGeometries["shapeGeo"].get();
	boxRitem9->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem9->IndexCount = boxRitem9->Geo->DrawArgs["box"].IndexCount;
	boxRitem9->StartIndexLocation = boxRitem9->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem9->BaseVertexLocation = boxRitem9->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem9.get());
	mAllRitems.push_back(std::move(boxRitem9));

	auto boxRitem10 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem10->World, XMMatrixScaling(6.2f, 0.86f, 0.9f)* XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(17.8f, -0.65f + 6.f, 12.15f)); //-7.5f
	boxRitem10->ObjCBIndex = objCBIndex++;
	boxRitem10->Mat = mMaterials["bricks3"].get();
	boxRitem10->Geo = mGeometries["shapeGeo"].get();
	boxRitem10->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem10->IndexCount = boxRitem10->Geo->DrawArgs["box"].IndexCount;
	boxRitem10->StartIndexLocation = boxRitem10->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem10->BaseVertexLocation = boxRitem10->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem10.get());
	mAllRitems.push_back(std::move(boxRitem10));


	//back Wall

	auto boxRitem11 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem11->World, XMMatrixScaling(15.6f, 0.86f, 0.9f) * XMMatrixTranslation(0.f, -1.06f + 6.f, 20.8f)); //-7.5f
	boxRitem11->ObjCBIndex = objCBIndex++;
	boxRitem11->Mat = mMaterials["bricks3"].get();
	boxRitem11->Geo = mGeometries["shapeGeo"].get();
	boxRitem11->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem11->IndexCount = boxRitem11->Geo->DrawArgs["box"].IndexCount;
	boxRitem11->StartIndexLocation = boxRitem11->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem11->BaseVertexLocation = boxRitem11->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem11.get());
	mAllRitems.push_back(std::move(boxRitem11));

	
	//sculpture

	auto CylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&CylRitem->World, XMMatrixScaling(2.2f, 2.f, 2.2f)* XMMatrixTranslation(0.f, 4.5f + 6.f, 4.f));
	CylRitem->ObjCBIndex = objCBIndex++;
	CylRitem->Geo = mGeometries["shapeGeo"].get();
	CylRitem->Mat = mMaterials["bricks0"].get();
	CylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	CylRitem->IndexCount = CylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	CylRitem->StartIndexLocation = CylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	CylRitem->BaseVertexLocation = CylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(CylRitem));

	auto torusRitem = std::make_unique<RenderItem>();
	// torusRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&torusRitem->World, XMMatrixScaling(1.2f, 1.2f, 1.2f) * XMMatrixRotationX(XM_PIDIV2) *XMMatrixTranslation(0.0f, 17.f + 6.f, 4.f));
	torusRitem->ObjCBIndex = objCBIndex++;
	torusRitem->Geo = mGeometries["shapeGeo"].get();
	torusRitem->Mat = mMaterials["stone0"].get();
	torusRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torusRitem->IndexCount = torusRitem->Geo->DrawArgs["torus"].IndexCount;
	torusRitem->StartIndexLocation = torusRitem->Geo->DrawArgs["torus"].StartIndexLocation;
	torusRitem->BaseVertexLocation = torusRitem->Geo->DrawArgs["torus"].BaseVertexLocation;
	mAllRitems.push_back(std::move(torusRitem));
	
	
	auto coneRitem = std::make_unique<RenderItem>();
	// coneRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(1.35f, 1.35f, 1.35f) * XMMatrixTranslation(0.0f, 21.f + 6.f, 4.f));
	coneRitem->ObjCBIndex = objCBIndex++;
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->Mat = mMaterials["stone0"].get();
	coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	auto diamondRitem = std::make_unique<RenderItem>();
	// diamondRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixTranslation(0.0f, 27.6f + 6.f, 4.f));
	diamondRitem->ObjCBIndex = objCBIndex++;
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->Mat = mMaterials["stone0"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));


	//Front Ornamental

	
	
	for (int i = 0; i < 2; ++i)
	{
		auto leftwedgeRitem = std::make_unique<RenderItem>();
		auto rightwedgeRitem = std::make_unique<RenderItem>();
		auto leftboxRitem = std::make_unique<RenderItem>();
		auto rightboxRitem = std::make_unique<RenderItem>();
		//right forward corner
		// wedgeRitem->World = MathHelper::Identity4x4();
		XMStoreFloat4x4(&leftwedgeRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(-17.8f, 8.f + 6.f, 16.f - i * 30));
		leftwedgeRitem->ObjCBIndex = objCBIndex++;
		leftwedgeRitem->Geo = mGeometries["shapeGeo"].get();
		leftwedgeRitem->Mat = mMaterials["stone0"].get();
		leftwedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftwedgeRitem->IndexCount = leftwedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
		leftwedgeRitem->StartIndexLocation = leftwedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftwedgeRitem->BaseVertexLocation = leftwedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftwedgeRitem));

		XMStoreFloat4x4(&rightwedgeRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(17.8f, 8.f + 6.f, 16.f - i * 30));
		rightwedgeRitem->ObjCBIndex = objCBIndex++;
		rightwedgeRitem->Geo = mGeometries["shapeGeo"].get();
		rightwedgeRitem->Mat = mMaterials["stone0"].get();
		rightwedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightwedgeRitem->IndexCount = rightwedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
		rightwedgeRitem->StartIndexLocation = rightwedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightwedgeRitem->BaseVertexLocation = rightwedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightwedgeRitem));

		XMStoreFloat4x4(&leftboxRitem->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(-17.8f, 10.5f + 6.f, 16.f - i * 30));
		leftboxRitem->ObjCBIndex = objCBIndex++;
		leftboxRitem->Geo = mGeometries["shapeGeo"].get();
		leftboxRitem->Mat = mMaterials["stone0"].get();
		leftboxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftboxRitem->IndexCount = leftboxRitem->Geo->DrawArgs["box"].IndexCount;
		leftboxRitem->StartIndexLocation = leftboxRitem->Geo->DrawArgs["box"].StartIndexLocation;
		leftboxRitem->BaseVertexLocation = leftboxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftboxRitem));

		XMStoreFloat4x4(&rightboxRitem->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(17.8f, 10.5f + 6.f, 16.f - i * 30));
		rightboxRitem->ObjCBIndex = objCBIndex++;
		rightboxRitem->Geo = mGeometries["shapeGeo"].get();
		rightboxRitem->Mat = mMaterials["stone0"].get();
		rightboxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightboxRitem->IndexCount = rightboxRitem->Geo->DrawArgs["box"].IndexCount;
		rightboxRitem->StartIndexLocation = rightboxRitem->Geo->DrawArgs["box"].StartIndexLocation;
		rightboxRitem->BaseVertexLocation = rightboxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightboxRitem));


	}



	//left Ornamental

	/*auto leftwedgeRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftwedgeRitem1->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(-21.8f, 8.f, 20.f));
	leftwedgeRitem1->ObjCBIndex = objCBIndex++;
	leftwedgeRitem1->Geo = mGeometries["shapeGeo"].get();
	leftwedgeRitem1->Mat = mMaterials["stone0"].get();
	leftwedgeRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftwedgeRitem1->IndexCount = leftwedgeRitem1->Geo->DrawArgs["wedge"].IndexCount;
	leftwedgeRitem1->StartIndexLocation = leftwedgeRitem1->Geo->DrawArgs["wedge"].StartIndexLocation;
	leftwedgeRitem1->BaseVertexLocation = leftwedgeRitem1->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftwedgeRitem1));*/

	for (int i = 0; i < 2; ++i)
	{
	auto leftwedgeRitem1 = std::make_unique<RenderItem>();
	auto rightwedgeRitem1 = std::make_unique<RenderItem>();
	auto leftboxRitem1 = std::make_unique<RenderItem>();
	auto rightboxRitem1 = std::make_unique<RenderItem>();


	
	XMStoreFloat4x4(&leftwedgeRitem1->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(-21.8f, 8.f + 6.f, 20.f - i * 30));
	leftwedgeRitem1->ObjCBIndex = objCBIndex++;
	leftwedgeRitem1->Geo = mGeometries["shapeGeo"].get();
	leftwedgeRitem1->Mat = mMaterials["stone0"].get();
	leftwedgeRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftwedgeRitem1->IndexCount = leftwedgeRitem1->Geo->DrawArgs["wedge"].IndexCount;
	leftwedgeRitem1->StartIndexLocation = leftwedgeRitem1->Geo->DrawArgs["wedge"].StartIndexLocation;
	leftwedgeRitem1->BaseVertexLocation = leftwedgeRitem1->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftwedgeRitem1));

	XMStoreFloat4x4(&rightwedgeRitem1->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(13.8f, 8.f + 6.f, 20.f - i * 30));
	rightwedgeRitem1->ObjCBIndex = objCBIndex++;
	rightwedgeRitem1->Geo = mGeometries["shapeGeo"].get();
	rightwedgeRitem1->Mat = mMaterials["stone0"].get();
	rightwedgeRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightwedgeRitem1->IndexCount = rightwedgeRitem1->Geo->DrawArgs["wedge"].IndexCount;
	rightwedgeRitem1->StartIndexLocation = rightwedgeRitem1->Geo->DrawArgs["wedge"].StartIndexLocation;
	rightwedgeRitem1->BaseVertexLocation = rightwedgeRitem1->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightwedgeRitem1));


	XMStoreFloat4x4(&leftboxRitem1->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(-21.8f, 10.5f + 6.f, 20.f - i * 30));
	leftboxRitem1->ObjCBIndex = objCBIndex++;
	leftboxRitem1->Geo = mGeometries["shapeGeo"].get();
	leftboxRitem1->Mat = mMaterials["stone0"].get();
	leftboxRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftboxRitem1->IndexCount = leftboxRitem1->Geo->DrawArgs["box"].IndexCount;
	leftboxRitem1->StartIndexLocation = leftboxRitem1->Geo->DrawArgs["box"].StartIndexLocation;
	leftboxRitem1->BaseVertexLocation = leftboxRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftboxRitem1));

	XMStoreFloat4x4(&rightboxRitem1->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(13.8f, 10.5f + 6.f, 20.f - i * 30));
	rightboxRitem1->ObjCBIndex = objCBIndex++;
	rightboxRitem1->Geo = mGeometries["shapeGeo"].get();
	rightboxRitem1->Mat = mMaterials["stone0"].get();
	rightboxRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightboxRitem1->IndexCount = rightboxRitem1->Geo->DrawArgs["box"].IndexCount;
	rightboxRitem1->StartIndexLocation = rightboxRitem1->Geo->DrawArgs["box"].StartIndexLocation;
	rightboxRitem1->BaseVertexLocation = rightboxRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightboxRitem1));



	}


	//right Ornamental

	for (int i = 0; i < 2; ++i)
	{
		auto leftwedgeRitem2 = std::make_unique<RenderItem>();
		auto rightwedgeRitem2 = std::make_unique<RenderItem>();
		auto leftboxRitem2 = std::make_unique<RenderItem>();
		auto rightboxRitem2 = std::make_unique<RenderItem>();



		XMStoreFloat4x4(&leftwedgeRitem2->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PIDIV2 + XM_PI) * XMMatrixTranslation(-13.8f, 8.f + 6.f, 20.f - i * 30));
		leftwedgeRitem2->ObjCBIndex = objCBIndex++;
		leftwedgeRitem2->Geo = mGeometries["shapeGeo"].get();
		leftwedgeRitem2->Mat = mMaterials["stone0"].get();
		leftwedgeRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftwedgeRitem2->IndexCount = leftwedgeRitem2->Geo->DrawArgs["wedge"].IndexCount;
		leftwedgeRitem2->StartIndexLocation = leftwedgeRitem2->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftwedgeRitem2->BaseVertexLocation = leftwedgeRitem2->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftwedgeRitem2));

		XMStoreFloat4x4(&rightwedgeRitem2->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PIDIV2 + XM_PI) * XMMatrixTranslation(21.8f, 8.f + 6.f, 20.f - i * 30));
		rightwedgeRitem2->ObjCBIndex = objCBIndex++;
		rightwedgeRitem2->Geo = mGeometries["shapeGeo"].get();
		rightwedgeRitem2->Mat = mMaterials["stone0"].get();
		rightwedgeRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightwedgeRitem2->IndexCount = rightwedgeRitem2->Geo->DrawArgs["wedge"].IndexCount;
		rightwedgeRitem2->StartIndexLocation = rightwedgeRitem2->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightwedgeRitem2->BaseVertexLocation = rightwedgeRitem2->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightwedgeRitem2));


		XMStoreFloat4x4(&leftboxRitem2->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(-13.8f, 10.5f + 6.f, 20.f - i * 30));
		leftboxRitem2->ObjCBIndex = objCBIndex++;
		leftboxRitem2->Geo = mGeometries["shapeGeo"].get();
		leftboxRitem2->Mat = mMaterials["stone0"].get();
		leftboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftboxRitem2->IndexCount = leftboxRitem2->Geo->DrawArgs["box"].IndexCount;
		leftboxRitem2->StartIndexLocation = leftboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;
		leftboxRitem2->BaseVertexLocation = leftboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftboxRitem2));

		XMStoreFloat4x4(&rightboxRitem2->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(21.8f, 10.5f + 6.f, 20.f - i * 30));
		rightboxRitem2->ObjCBIndex = objCBIndex++;
		rightboxRitem2->Geo = mGeometries["shapeGeo"].get();
		rightboxRitem2->Mat = mMaterials["stone0"].get();
		rightboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightboxRitem2->IndexCount = rightboxRitem2->Geo->DrawArgs["box"].IndexCount;
		rightboxRitem2->StartIndexLocation = rightboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;
		rightboxRitem2->BaseVertexLocation = rightboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightboxRitem2));



	}

	//back Ornamental
	for (int i = 0; i < 2; ++i)
	{
		auto leftwedgeRitem3 = std::make_unique<RenderItem>();
		auto rightwedgeRitem3 = std::make_unique<RenderItem>();
		auto leftboxRitem3 = std::make_unique<RenderItem>();
		auto rightboxRitem3 = std::make_unique<RenderItem>();
		//right forward corner
		// wedgeRitem->World = MathHelper::Identity4x4();
		XMStoreFloat4x4(&leftwedgeRitem3->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PI)* XMMatrixTranslation(-17.8f, 8.f + 6.f, 16.f - i * 30 + 8.f));
		leftwedgeRitem3->ObjCBIndex = objCBIndex++;
		leftwedgeRitem3->Geo = mGeometries["shapeGeo"].get();
		leftwedgeRitem3->Mat = mMaterials["stone0"].get();
		leftwedgeRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftwedgeRitem3->IndexCount = leftwedgeRitem3->Geo->DrawArgs["wedge"].IndexCount;
		leftwedgeRitem3->StartIndexLocation = leftwedgeRitem3->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftwedgeRitem3->BaseVertexLocation = leftwedgeRitem3->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftwedgeRitem3));

		XMStoreFloat4x4(&rightwedgeRitem3->World, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixRotationX(XM_PI) * XMMatrixRotationY(XM_PI) * XMMatrixTranslation(17.8f, 8.f + 6.f, 16.f - i * 30 + 8.f));
		rightwedgeRitem3->ObjCBIndex = objCBIndex++;
		rightwedgeRitem3->Geo = mGeometries["shapeGeo"].get();
		rightwedgeRitem3->Mat = mMaterials["stone0"].get();
		rightwedgeRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightwedgeRitem3->IndexCount = rightwedgeRitem3->Geo->DrawArgs["wedge"].IndexCount;
		rightwedgeRitem3->StartIndexLocation = rightwedgeRitem3->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightwedgeRitem3->BaseVertexLocation = rightwedgeRitem3->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightwedgeRitem3));

		XMStoreFloat4x4(&leftboxRitem3->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(-17.8f, 10.5f + 6.f, 16.f - i * 30 + 8.f));
		leftboxRitem3->ObjCBIndex = objCBIndex++;
		leftboxRitem3->Geo = mGeometries["shapeGeo"].get();
		leftboxRitem3->Mat = mMaterials["stone0"].get();
		leftboxRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftboxRitem3->IndexCount = leftboxRitem3->Geo->DrawArgs["box"].IndexCount;
		leftboxRitem3->StartIndexLocation = leftboxRitem3->Geo->DrawArgs["box"].StartIndexLocation;
		leftboxRitem3->BaseVertexLocation = leftboxRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftboxRitem3));

		XMStoreFloat4x4(&rightboxRitem3->World, XMMatrixScaling(1.55f, 0.2f, 1.55f) * XMMatrixRotationX(XM_PI) * XMMatrixTranslation(17.8f, 10.5f + 6.f, 16.f - i * 30 + 8.f));
		rightboxRitem3->ObjCBIndex = objCBIndex++;
		rightboxRitem3->Geo = mGeometries["shapeGeo"].get();
		rightboxRitem3->Mat = mMaterials["stone0"].get();
		rightboxRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightboxRitem3->IndexCount = rightboxRitem3->Geo->DrawArgs["box"].IndexCount;
		rightboxRitem3->StartIndexLocation = rightboxRitem3->Geo->DrawArgs["box"].StartIndexLocation;
		rightboxRitem3->BaseVertexLocation = rightboxRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightboxRitem3));


	}









	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = objCBIndex++;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	//step2
	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));

	//UINT objCBIndex = 6;
	for (int i = 0; i < 2; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixScaling(2.2f, 1.3f, 2.2f) * XMMatrixTranslation(-17.8f, 1.5f + 6.f, -10.f + i * 30.f);
		//XMMATRIX leftCylWorld = XMMatrixTranslation(-2.5f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixScaling(2.2f, 1.3f, 2.2f) * XMMatrixTranslation(17.8f, 1.5f + 6.f, -10.0f + i * 30.f);
		//XMMATRIX rightCylWorld = XMMatrixTranslation(+2.5f, 1.5f, -10.0f + i * 5.0f);
		//XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.2f, 1.3f, 2.2f) * XMMatrixTranslation(0.0f, 1.5f, -7.5f));

		/*XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);*/

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		/*XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;*/

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		/*mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));*/
	}

	
	
	
	
	




	
	
	
	
	
	
	//auto torusRitem = std::make_unique<RenderItem>();
	//// torusRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&torusRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 5.0f, -7.5));
	//torusRitem->ObjCBIndex = objCBIndex++;
	//torusRitem->Geo = mGeometries["shapeGeo"].get();
	//torusRitem->Mat = mMaterials["stone0"].get();
	//torusRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//torusRitem->IndexCount = torusRitem->Geo->DrawArgs["torus"].IndexCount;
	//torusRitem->StartIndexLocation = torusRitem->Geo->DrawArgs["torus"].StartIndexLocation;
	//torusRitem->BaseVertexLocation = torusRitem->Geo->DrawArgs["torus"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(torusRitem));

	//auto coneRitem = std::make_unique<RenderItem>();
	//// coneRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(0.8f, 0.8f, 0.8f) * XMMatrixTranslation(0.0f, 4.0f, -7.5f));
	//coneRitem->ObjCBIndex = objCBIndex++;
	//coneRitem->Geo = mGeometries["shapeGeo"].get();
	//coneRitem->Mat = mMaterials["stone0"].get();
	//coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	//coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	//coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(coneRitem));

	//auto pyramidRitem = std::make_unique<RenderItem>();
	//// pyramidRitem->World = MathHelper::Identity4x4();
	////left forward corner
	//XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(-2.5f, 3.7f, -10.0f));
	//pyramidRitem->ObjCBIndex = objCBIndex++;
	//pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	//pyramidRitem->Mat = mMaterials["stone0"].get();
	//pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	//pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	//pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(pyramidRitem));

	//auto wedgeRitem = std::make_unique<RenderItem>();
	////right forward corner
	//// wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&wedgeRitem->World, XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(2.5f, 3.2f, -10.0f));
	//wedgeRitem->ObjCBIndex = objCBIndex++;
	//wedgeRitem->Geo = mGeometries["shapeGeo"].get();
	//wedgeRitem->Mat = mMaterials["stone0"].get();
	//wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	//wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	//wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(wedgeRitem));

	//auto diamondRitem = std::make_unique<RenderItem>();
	//// diamondRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(-2.5f, 3.5f, -5.0f));
	//diamondRitem->ObjCBIndex = objCBIndex++;
	//diamondRitem->Geo = mGeometries["shapeGeo"].get();
	//diamondRitem->Mat = mMaterials["stone0"].get();
	//diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	//diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	//diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(diamondRitem));

	//auto prismRitem = std::make_unique<RenderItem>();
	//// prismRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&prismRitem->World, XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(2.5f, 3.5f, -5.0f));
	//prismRitem->ObjCBIndex = objCBIndex++;
	//prismRitem->Geo = mGeometries["shapeGeo"].get();
	//prismRitem->Mat = mMaterials["stone0"].get();
	//prismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//prismRitem->IndexCount = prismRitem->Geo->DrawArgs["prism"].IndexCount;
	//prismRitem->StartIndexLocation = prismRitem->Geo->DrawArgs["prism"].StartIndexLocation;
	//prismRitem->BaseVertexLocation = prismRitem->Geo->DrawArgs["prism"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(prismRitem));

	int count = 0;
	// All the render items are opaque.
	for (auto& e : mAllRitems) {
		if (count++ < 4) continue;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(e.get());
	}
}

void TexColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexColumnsApp::GetStaticSamplers()
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

//float TexColumnsApp::GetHillsHeight(float x, float z)const
//{
//	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
//}
//
//XMFLOAT3 TexColumnsApp::GetHillsNormal(float x, float z)const
//{
//	// n = (-df/dx, 1, -df/dz)
//	XMFLOAT3 n(
//		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
//		1.0f,
//		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));
//
//	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
//	XMStoreFloat3(&n, unitNormal);
//
//	return n;
//}



float TexColumnsApp::GetHillsHeight(float x, float z)const
{
	if (x < 23.0f && x > -23.0f && z < 30.0f && z > -20.0f) return 2.0f;
	if (x < 20.0f && x > -20.0f && z < 0.0f) return 2.0f;
	if ((x > 60.0f || x < -60.0f) || (z < -60.0f || z > 60.0f)) return 1.0f;
	return -1.f;
}

XMFLOAT3 TexColumnsApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)

	if (x < 20.0f && x > -20.0f && z < 20.0f && z > -20.0f) return XMFLOAT3(0., 1., 0.);
	XMFLOAT3 n(
		-0.006f * x - 0.003f * z * z,
		1.0f,
		-0.003f * x * x - 0.006f * z);

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

