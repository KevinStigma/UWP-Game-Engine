#include "pch.h"
#include "DynamicMapObjectsRenderer.h"
#include "Common\DirectXHelper.h"
#include "Common\MathHelper.h"
#include "Common\GeometryGenerator.h"
#include "Common\BasicReaderWriter.h"
#include "Common\ShaderChangement.h"
#include <fstream>

using namespace DXFramework;

using namespace DirectX;
using namespace Windows::Foundation;
using namespace Microsoft::WRL;

using namespace DX;

// Loads vertex and pixel shaders from files and instantiates the cube geometry.
DynamicMapObjectsRenderer::DynamicMapObjectsRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::shared_ptr<DX::Camera>& camera)
	: m_loadingComplete(false), m_initialized(false), m_deviceResources(deviceResources), m_camera(camera)
{
	m_dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	m_dirLights[0].Diffuse = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	m_dirLights[0].Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	m_dirLights[0].Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);

	m_dirLights[1].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	m_dirLights[1].Diffuse = XMFLOAT4(0.20f, 0.20f, 0.20f, 1.0f);
	m_dirLights[1].Specular = XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f);
	m_dirLights[1].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);

	m_dirLights[2].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	m_dirLights[2].Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	m_dirLights[2].Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	m_dirLights[2].Direction = XMFLOAT3(0.0f, -0.707f, -0.707f);

	XMMATRIX skullScale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
	XMMATRIX skullOffset = XMMatrixTranslation(3.0f, 2.0f, 0.0f);
	XMStoreFloat4x4(&m_skullWorld, skullScale*skullOffset);

	m_perFrameCB = std::make_shared<ConstantBuffer<BasicPerFrameCB>>();
	m_perObjectCB = std::make_shared<ConstantBuffer<BasicPerObjectCB>>();
	m_centerSphere = std::make_unique<BasicObject>(deviceResources, m_perFrameCB, m_perObjectCB);
	m_skull = std::make_unique<BasicObject>(deviceResources, m_perFrameCB, m_perObjectCB);
	m_sphere = std::make_unique<BasicObject>(deviceResources, m_perFrameCB, m_perObjectCB);
	m_base = std::make_unique<BasicObject>(deviceResources, m_perFrameCB, m_perObjectCB);
	m_sky = std::make_unique<Sky>(deviceResources, m_perFrameCB, m_perObjectCB, m_camera);
	m_dynamicCube = std::make_unique<DynamicCubeMapHelper>(deviceResources, m_perFrameCB, camera);
}

// Initialize components
void DynamicMapObjectsRenderer::Initialize()
{
	concurrency::task<void> initTask = concurrency::task_from_result();
	initTask.then([=]()
	{
		m_sky->Initialize(L"Media\\Textures\\sunsetcube1024.dds", 5000.0f);
		m_dynamicCube->Initialize(XMFLOAT3(0.0f, 2.0f, 0.0f));
		InitCenterSphere();
		InitSkull();
		InitSphere();
		InitBase();
		m_initialized = true;
	}, concurrency::task_continuation_context::use_arbitrary());
}

void DynamicMapObjectsRenderer::CreateDeviceDependentResources()
{
	assert(IsMainThread());

	if (!m_initialized)
	{
		OutputDebugString(L"The components haven't been initialized!");
		return;
	}

	// Initialize constant buffer
	m_perFrameCB->Initialize(m_deviceResources->GetD3DDevice());
	m_perObjectCB->Initialize(m_deviceResources->GetD3DDevice());

	m_centerSphere->CreateDeviceDependentResourcesAsync()
		.then([=]()
	{
		return m_skull->CreateDeviceDependentResourcesAsync();
	})
		.then([=]()
	{
		return m_sphere->CreateDeviceDependentResourcesAsync();
	})
		.then([=]()
	{
		return m_base->CreateDeviceDependentResourcesAsync();
	})
		.then([=]()
	{
		return m_sky->CreateDeviceDependentResourcesAsync();
	})
		.then([=]()
	{
		return m_dynamicCube->CreateDeviceDependentResourcesAsync();
	})
		.then([=]()
	{
		// Once the data is loaded, the object is ready to be rendered.
		m_loadingComplete = true;
	});
}

// Initializes view parameters when the window size changes.
void DynamicMapObjectsRenderer::CreateWindowSizeDependentResources()
{
	// To do: create windows size related objects
}

// Called once per frame, rotates the cube and calculates the model and view matrices.
void DynamicMapObjectsRenderer::Update(DX::GameTimer const& timer)
{
	if (!m_loadingComplete)
	{
		return;
	}
	m_sky->Update();

	// Animate the skull around the center sphere.
	XMMATRIX skullScale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
	XMMATRIX skullOffset = XMMatrixTranslation(3.0f, 2.0f, 0.0f);
	XMMATRIX skullLocalRotate = XMMatrixRotationY(2.0f*(float)timer.GetTotalSeconds());
	XMMATRIX skullGlobalRotate = XMMatrixRotationY(0.5f*(float)timer.GetTotalSeconds());
	XMStoreFloat4x4(&m_skullWorld, skullScale*skullLocalRotate*skullOffset*skullGlobalRotate);
	m_skull->SetWorld(0, 0, m_skullWorld);
}

// Renders one frame using the vertex and pixel shaders.
void DynamicMapObjectsRenderer::Render()
{
	// Loading is asynchronous. Only draw geometry after it's loaded.
	if (!m_loadingComplete)
	{
		return;
	}

	ComPtr<ID3D11DeviceContext> context = m_deviceResources->GetD3DDeviceContext();
	// Update per-frame constant buffer
	XMMATRIX view = m_camera->View();
	XMMATRIX proj = m_camera->Proj();
	XMMATRIX viewProj = m_camera->ViewProj();

	XMStoreFloat4x4(&m_perFrameCB->Data.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&m_perFrameCB->Data.InvView, XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(view), view)));
	XMStoreFloat4x4(&m_perFrameCB->Data.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&m_perFrameCB->Data.InvProj, XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(proj), proj)));
	XMStoreFloat4x4(&m_perFrameCB->Data.ViewProj, XMMatrixTranspose(viewProj));

	m_perFrameCB->Data.DirLights[0] = m_dirLights[0];
	m_perFrameCB->Data.DirLights[1] = m_dirLights[1];
	m_perFrameCB->Data.DirLights[2] = m_dirLights[2];
	m_perFrameCB->Data.EyePosW = m_camera->GetPosition();

	m_perFrameCB->Data.FogStart = 10.0f;
	m_perFrameCB->Data.FogRange = 60.0f;
	m_perFrameCB->Data.FogColor = XMFLOAT4(0.65f, 0.65f, 0.65f, 1.0f);

	m_perFrameCB->ApplyChanges(context.Get());

	m_dynamicCube->Render([&]()
	{
		m_skull->Render();
		m_sphere->Render();
		m_base->Render(true);
		m_sky->Render();
	});

	m_centerSphere->UpdateReflectMapSRV(m_dynamicCube->GetDynamicCubeMapSRV());

	m_centerSphere->Render();
	m_skull->Render();
	m_sphere->Render();
	m_base->Render(true);
	m_sky->Render();
}

void DynamicMapObjectsRenderer::ReleaseDeviceDependentResources()
{
	m_loadingComplete = false;

	m_perFrameCB->Reset();
	m_perObjectCB->Reset();
	m_centerSphere->ReleaseDeviceDependentResources();
	m_skull->ReleaseDeviceDependentResources();
	m_sphere->ReleaseDeviceDependentResources();
	m_base->ReleaseDeviceDependentResources();
	m_sky->ReleaseDeviceDependentResources();
	m_dynamicCube->ReleaseDeviceDependentResources();
}

void DynamicMapObjectsRenderer::InitCenterSphere()
{
	// Init spheres
	BasicObjectData* objectData = new BasicObjectData();
	objectData->UseIndex = true;
	objectData->UseEx = false;

	GeometryGenerator::MeshData sphere;
	GeometryGenerator geoGen;
	geoGen.CreateSphere(0.5f, 20, 20, sphere);

	int sphereIndexCount = sphere.Indices.size();

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	auto& vertices = objectData->VertexData;
	vertices.resize(sphere.Vertices.size());
	for (size_t i = 0; i < sphere.Vertices.size(); ++i)
	{
		vertices[i].Pos = sphere.Vertices[i].Position;
		vertices[i].Normal = sphere.Vertices[i].Normal;
		vertices[i].Tex = sphere.Vertices[i].TexC;
	}

	// Pack the indices of all the meshes into one index buffer.
	auto& indices = objectData->IndexData;
	indices.assign(sphere.Indices.begin(), sphere.Indices.end());

	// Set unit data
	XMFLOAT4X4 centerSphereWorld;
	XMMATRIX centerSphereScale = XMMatrixScaling(2.0f, 2.0f, 2.0f);
	XMMATRIX centerSphereOffset = XMMatrixTranslation(0.0f, 2.0f, 0.0f);
	XMStoreFloat4x4(&centerSphereWorld, XMMatrixMultiply(centerSphereScale, centerSphereOffset));

	Material centerSphereMat;
	centerSphereMat.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	centerSphereMat.Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	centerSphereMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	centerSphereMat.Reflect = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);

	objectData->Units.resize(1);
	// sphere
	XMFLOAT4X4 One;
	XMStoreFloat4x4(&One, XMMatrixIdentity());
	auto& unit = objectData->Units[0];
	unit.VCount = vertices.size();
	unit.Base = 0;
	unit.Count = sphereIndexCount;
	unit.Start = 0;
	unit.Worlds.push_back(centerSphereWorld);
	unit.Material.push_back(centerSphereMat);

	BasicFeatureConfigure objectFeature = { 0 };
	objectFeature.LightCount = 3;
	objectFeature.ReflectEnable = true;

	m_centerSphere->Initialize(objectData, objectFeature);
}

void DynamicMapObjectsRenderer::InitSkull()
{
	// Init skull
	BasicObjectData* objectData = new BasicObjectData();
	objectData->UseEx = false;
	objectData->UseIndex = true;

	// Read binary data
	std::ifstream ss(L"Media\\Models\\skull.txt");
	if (!ss)
		throw ref new Platform::FailureException("Cannot open object model file!");

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	ss >> ignore >> vcount;
	ss >> ignore >> tcount;
	ss >> ignore >> ignore >> ignore >> ignore;

	auto& vertices = objectData->VertexData;
	vertices.resize(vcount);

	for (UINT i = 0; i < vcount; ++i)
	{
		ss >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		ss >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	ss >> ignore;
	ss >> ignore;
	ss >> ignore;

	int skullIndexCount = 3 * tcount;
	auto& indices = objectData->IndexData;
	indices.resize(skullIndexCount);
	for (UINT i = 0; i < tcount; ++i)
	{
		ss >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	ss.clear();

	// Set unit data
	Material skullMat;
	skullMat.Ambient = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	skullMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	skullMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	skullMat.Reflect = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);

	objectData->Units.resize(1);
	auto& unit = objectData->Units[0];
	unit.VCount = vcount;
	unit.Base = 0;
	unit.Count = skullIndexCount;
	unit.Start = 0;
	unit.Worlds.push_back(m_skullWorld);
	unit.Material.push_back(skullMat);

	BasicFeatureConfigure objectFeature = { 0 };
	objectFeature.LightCount = 3;

	m_skull->Initialize(objectData, objectFeature);
}

void DynamicMapObjectsRenderer::InitSphere()
{
	// Init spheres
	BasicObjectData* objectData = new BasicObjectData();
	objectData->UseIndex = true;
	objectData->UseEx = false;

	GeometryGenerator::MeshData sphere;
	GeometryGenerator geoGen;
	geoGen.CreateSphere(0.5f, 20, 20, sphere);

	int sphereIndexCount = sphere.Indices.size();

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	auto& vertices = objectData->VertexData;
	vertices.resize(sphere.Vertices.size());
	for (size_t i = 0; i < sphere.Vertices.size(); ++i)
	{
		vertices[i].Pos = sphere.Vertices[i].Position;
		vertices[i].Normal = sphere.Vertices[i].Normal;
		vertices[i].Tex = sphere.Vertices[i].TexC;
	}

	// Pack the indices of all the meshes into one index buffer.
	auto& indices = objectData->IndexData;
	indices.assign(sphere.Indices.begin(), sphere.Indices.end());

	// Set unit data
	XMFLOAT4X4 m_sphereWorld[10];
	for (int i = 0; i < 5; ++i)
	{
		XMStoreFloat4x4(&m_sphereWorld[i * 2 + 0], XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f));
		XMStoreFloat4x4(&m_sphereWorld[i * 2 + 1], XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f));
	}

	Material sphereMat;
	sphereMat.Ambient = XMFLOAT4(0.2f, 0.3f, 0.4f, 1.0f);
	sphereMat.Diffuse = XMFLOAT4(0.2f, 0.3f, 0.4f, 1.0f);
	sphereMat.Specular = XMFLOAT4(0.9f, 0.9f, 0.9f, 16.0f);
	sphereMat.Reflect = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);

	objectData->Units.resize(1);
	// sphere
	XMFLOAT4X4 One;
	XMStoreFloat4x4(&One, XMMatrixIdentity());
	auto& unit = objectData->Units[0];
	unit.VCount = vertices.size();
	unit.Base = 0;
	unit.Count = sphereIndexCount;
	unit.Start = 0;
	unit.Worlds.assign(m_sphereWorld, m_sphereWorld + 10);
	unit.Material.push_back(sphereMat);
	unit.MaterialStepRate = 10;
	unit.TextureFileNames.push_back(L"Media\\Textures\\stone.dds");
	unit.TextureStepRate = 10;
	unit.TextureTransform.push_back(One);
	unit.TextureTransformStepRate = 10;

	BasicFeatureConfigure objectFeature = { 0 };
	objectFeature.LightCount = 3;
	objectFeature.TextureEnable = true;
	objectFeature.ReflectEnable = true;
	objectFeature.ReflectFileName = L"Media\\Textures\\sunsetcube1024.dds";

	m_sphere->Initialize(objectData, objectFeature);
}

void DynamicMapObjectsRenderer::InitBase()
{
	// Init base objects
	BasicObjectData* objectData = new BasicObjectData();
	objectData->UseIndex = true;
	objectData->UseEx = true;

	// Init shapes
	GeometryGenerator::MeshData box;
	GeometryGenerator::MeshData grid;
	GeometryGenerator::MeshData cylinder;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);
	geoGen.CreateGrid(20.0f, 30.0f, 60, 40, grid);
	geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20, cylinder);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	int boxVertexOffset = 0;
	int gridVertexOffset = box.Vertices.size();
	int cylinderVertexOffset = gridVertexOffset + grid.Vertices.size();

	// Cache the index count of each object.
	int boxIndexCount = box.Indices.size();
	int gridIndexCount = grid.Indices.size();
	int cylinderIndexCount = cylinder.Indices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	int boxIndexOffset = 0;
	int gridIndexOffset = boxIndexCount;
	int cylinderIndexOffset = gridIndexOffset + gridIndexCount;

	UINT totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		cylinder.Vertices.size();

	UINT totalIndexCount =
		boxIndexCount +
		gridIndexCount +
		cylinderIndexCount;

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto& vertices = objectData->VertexDataEx;
	vertices.resize(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].Tex = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].Tex = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].Tex = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	// Pack the indices of all the meshes into one index buffer.
	auto& indices = objectData->IndexData;
	indices.insert(indices.end(), box.Indices.begin(), box.Indices.end());
	indices.insert(indices.end(), grid.Indices.begin(), grid.Indices.end());
	indices.insert(indices.end(), cylinder.Indices.begin(), cylinder.Indices.end());

	// Set unit data
	XMFLOAT4X4 gridWorld, boxWorld, cylWorld[10];

	XMMATRIX I = XMMatrixIdentity();
	XMFLOAT4X4 One;
	XMStoreFloat4x4(&One, I);
	XMStoreFloat4x4(&gridWorld, I);

	XMMATRIX boxScale = XMMatrixScaling(3.0f, 1.0f, 3.0f);
	XMMATRIX boxOffset = XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	XMStoreFloat4x4(&boxWorld, XMMatrixMultiply(boxScale, boxOffset));

	for (int i = 0; i < 5; ++i)
	{
		XMStoreFloat4x4(&cylWorld[i * 2 + 0], XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f));
		XMStoreFloat4x4(&cylWorld[i * 2 + 1], XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f));
	}

	Material gridMat, cylinderMat, boxMat;

	gridMat.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	gridMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	gridMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	gridMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	cylinderMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cylinderMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cylinderMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	cylinderMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	boxMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	boxMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	boxMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	boxMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	objectData->Units.resize(3);
	// box
	auto& unit0 = objectData->Units[0];
	unit0.VCount = box.Vertices.size();
	unit0.Base = boxVertexOffset;
	unit0.Count = boxIndexCount;
	unit0.Start = boxIndexOffset;
	unit0.Worlds.push_back(boxWorld);
	unit0.TextureFileNames.push_back(L"Media\\Textures\\stone.dds");
	unit0.NorTextureFileNames.push_back(L"Media\\Textures\\stones_nmap.dds");
	unit0.TextureTransform.push_back(One);
	unit0.Material.push_back(boxMat);

	// grid
	auto& unit1 = objectData->Units[1];
	unit1.VCount = grid.Vertices.size();
	unit1.Base = gridVertexOffset;
	unit1.Count = gridIndexCount;
	unit1.Start = gridIndexOffset;
	unit1.Worlds.push_back(gridWorld);
	unit1.TextureFileNames.push_back(L"Media\\Textures\\floor.dds");
	unit1.NorTextureFileNames.push_back(L"Media\\Textures\\floor_nmap.dds");
	XMFLOAT4X4 texTransform;
	XMStoreFloat4x4(&texTransform, XMMatrixScaling(6.0f, 8.0f, 1.0f));
	unit1.TextureTransform.push_back(texTransform);
	unit1.Material.push_back(gridMat);

	// cylinder
	auto& unit2 = objectData->Units[2];
	unit2.VCount = cylinder.Vertices.size();
	unit2.Base = cylinderVertexOffset;
	unit2.Count = cylinderIndexCount;
	unit2.Start = cylinderIndexOffset;
	unit2.Worlds.assign(cylWorld, cylWorld + 10);
	unit2.TextureFileNames.push_back(L"Media\\Textures\\bricks.dds");
	unit2.TextureStepRate = 10;
	unit2.NorTextureFileNames.push_back(L"Media\\Textures\\bricks_nmap.dds");
	unit2.NorTextureStepRate = 10;
	unit2.TextureTransform.push_back(One);
	unit2.TextureTransformStepRate = 10;
	unit2.Material.push_back(cylinderMat);
	unit2.MaterialStepRate = 10;

	BasicFeatureConfigure objectFeature = { 0 };
	objectFeature.LightCount = 3;
	objectFeature.TextureEnable = true;
	/*objectFeature->NormalEnable = true;
	objectFeature->TessEnable = true;
	objectFeature->TessDesc.HeightScale = 0.07f;
	objectFeature->TessDesc.MaxTessDistance = 1.0f;
	objectFeature->TessDesc.MaxTessFactor = 5.0f;
	objectFeature->TessDesc.MinTessDistance = 25.0f;
	objectFeature->TessDesc.MinTessFactor = 1.0f;*/

	m_base->Initialize(objectData, objectFeature);
}

// Input control
void DynamicMapObjectsRenderer::OnPointerPressed(Windows::UI::Core::PointerEventArgs^ args)
{

}
void DynamicMapObjectsRenderer::OnPointerReleased(Windows::UI::Core::PointerEventArgs^ args)
{

}
void DynamicMapObjectsRenderer::OnPointerMoved(Windows::UI::Core::PointerEventArgs^ args)
{

}
void DynamicMapObjectsRenderer::OnKeyDown(Windows::UI::Core::KeyEventArgs^ args)
{

}
void DynamicMapObjectsRenderer::OnKeyUp(Windows::UI::Core::KeyEventArgs^ args)
{

}
