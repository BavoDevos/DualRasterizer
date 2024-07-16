#include "pch.h"
#include "Renderer.h"
#include "HardwareRenderer.h"
#include "SoftwareRenderer.h"
#include "Camera.h"
#include "Mesh.h"
#include "Texture.h"
#include "MaterialShaded.h"
#include "MaterialTransparent.h"
#include <memory>

namespace dae {

	Renderer::Renderer(SDL_Window* pWindow) 
		: m_pWindow(pWindow)
	{
		//Initialize
		SDL_GetWindowSize(pWindow, &m_Width, &m_Height);

		// Create and initialize the camera
		m_pCamera = std::unique_ptr<Camera>(new Camera(45.0f, { 0.0f, 0.0f, 0.0f }, static_cast<float>(m_Width) / m_Height));

		// Create the software rasterizer
		m_pSoftwareRenderer = std::make_unique<SoftwareRenderer>(pWindow);

		// Create the hardware rasterizer
		m_pHardwareRenderer = std::make_unique<HardwareRenderer>(pWindow);
		
		// Load all the textures and meshes
		LoadResources();

		// Show keybinds
		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "[Key Bindings - SHARED]\n";
		std::cout << "\t[F1]  Toggle Rasterizer Mode (HARDWARE / SOFTWARE)\n";
		std::cout << "\t[F2]  Toggle Vehicle Rotation (ON / OFF)\n";
		std::cout << "\t[F9]  Cycle CullMode (BACK / FRONT / NONE)\n";
		std::cout << "\t[F10] Toggle Uniform ClearColor (ON / OFF)\n";
		std::cout << "\t[F11] Toggle Print FPS (ON / OFF)\n";
		std::cout << "\n";

		SetConsoleTextAttribute(m_hConsole, 10); // 10 is the color code for green
		std::cout << "[Key Bindings - HARDWARE]\n";
		std::cout << "\t[F3] Toggle FireFX (ON / OFF)\n";
		std::cout << "\t[F4] Cycle Sampler State (POINT / LINEAR / ANISOTROPIC)\n";
		std::cout << "\n";

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "[Key Bindings - SOFTWARE]\n";
		std::cout << "\t[F5] Cycle Shading Mode (COMBINED / OBSERVED_AREA / DIFFUSE / SPECULAR)\n";
		std::cout << "\t[F6] Toggle NormalMap (ON / OFF)\n";
		std::cout << "\t[F7] Toggle DepthBuffer Visualization (ON / OFF)\n";
		std::cout << "\t[F8] Toggle BoundingBox Visualization (ON / OFF)\n";
		std::cout << "\t[0] Toggle Multithreading (Synchronous / Async/ Parallel_for)\n";
		std::cout << "\n\n";
		std::cout << "I added Async threading and parallel_for threading to the Software rasterizer\n";
	}

	Renderer::~Renderer()
	{
		//delete what we have to delete
		for (Texture* pTexture : m_pTextVec)
		{
			delete pTexture;
		}

		for (Mesh* pMesh : m_pMeshVec)
		{
			delete pMesh;
		}
	}

	void Renderer::Render() const
	{
		switch (m_RenderMode)
		{
		case dae::Renderer::RenderMode::Software:
			// Render the scene using the software rasterizer
			m_pSoftwareRenderer->Render(m_pCamera, m_IsBackgroundUniform);
			break;
		case dae::Renderer::RenderMode::Hardware:
			// Render the scene using the software rasterizer
			m_pHardwareRenderer->Render(m_pMeshVec, m_IsBackgroundUniform);
			break;
		}
	}

	void Renderer::Update(const Timer* pTimer)
	{
		// Update camera movement
		m_pCamera->Update(pTimer);

		// Calculate viewprojection matrix
		const Matrix ViewProjMatrix{ m_pCamera->GetViewMatrix() * m_pCamera->GetProjectionMatrix() };

		//Rotate meshes
		const float rotationSpeed{ 45.0f * TO_RADIANS };
		for (Mesh* pMesh : m_pMeshVec)
		{
			if(m_IsMeshRotating) pMesh->RotateY(rotationSpeed * pTimer->GetElapsed());
			pMesh->SetMatrices(ViewProjMatrix, m_pCamera->GetInverseViewMatrix());
		}
	}

	void Renderer::LoadResources()
	{
		// Retrieve the DirectX device from the hardware renderer
		ID3D11Device* pDirectXDevice{ m_pHardwareRenderer->GetDevice() };

		// Retrieve the current sample state from the hardware renderer
		ID3D11SamplerState* pSampleState{ m_pHardwareRenderer->GetSampleState() };

		// Create the vehicle effect
		MaterialShaded* vehicleMaterial{ new MaterialShaded{ pDirectXDevice, L"Resources/PosTex3D.fx" } };

		// Load all the textures needed for the vehicle
		Texture* pVehicleDiffText{ Texture::LoadFromFile(pDirectXDevice, "Resources/vehicle_diffuse.png", Texture::TextureType::Diffuse) };
		Texture* pNormalText{ Texture::LoadFromFile(pDirectXDevice, "Resources/vehicle_normal.png", Texture::TextureType::Normal) };
		Texture* pSpecularText{ Texture::LoadFromFile(pDirectXDevice, "Resources/vehicle_specular.png", Texture::TextureType::Specular) };
		Texture* pGlossText{ Texture::LoadFromFile(pDirectXDevice, "Resources/vehicle_gloss.png", Texture::TextureType::Glossiness) };

		m_pTextVec.push_back(pGlossText);
		m_pTextVec.push_back(pVehicleDiffText);
		m_pTextVec.push_back(pNormalText);
		m_pTextVec.push_back(pSpecularText);

		// Apply the textures to the vehicle effect
		vehicleMaterial->SetTexture(pVehicleDiffText);
		vehicleMaterial->SetTexture(pNormalText);
		vehicleMaterial->SetTexture(pSpecularText);
		vehicleMaterial->SetTexture(pGlossText);

		// Create the vehicle mesh and add it to the list of meshes
		Mesh* pVehicle{ new Mesh{ pDirectXDevice, "Resources/vehicle.obj", vehicleMaterial, pSampleState } };
		pVehicle->SetPosition({ 0.0f, 0.0f, 50.0f });
		m_pMeshVec.push_back(pVehicle);

		// Create the fire effect
		MaterialTransparent* transparentMaterial{ new MaterialTransparent{ pDirectXDevice, L"Resources/Transparent3D.fx" } };

		// Load the texture needed for the fire
		Texture* pFireDiffuseTexture{ Texture::LoadFromFile(pDirectXDevice, "Resources/fireFX_diffuse.png", Texture::TextureType::Diffuse) };
		m_pTextVec.push_back(pFireDiffuseTexture);

		// Apply the texture to the fire effect
		transparentMaterial->SetTexture(pFireDiffuseTexture);

		// Create the fire mesh and add it ot the list of meshes
		Mesh* pFire{ new Mesh{ pDirectXDevice, "Resources/fireFX.obj", transparentMaterial, pSampleState } };
		pFire->SetPosition({ 0.0f, 0.0f, 50.0f });
		m_pMeshVec.push_back(pFire);


		
		// Set the software renderer to render the vehicle
		m_pSoftwareRenderer->SetMesh(pVehicle);
		m_pSoftwareRenderer->SetTextures(pVehicleDiffText, pNormalText, pSpecularText, pGlossText);
	}

	void Renderer::ToggleRenderMode()
	{
		// Go to the next render mode
		m_RenderMode = static_cast<RenderMode>((static_cast<int>(m_RenderMode) + 1) % (static_cast<int>(RenderMode::Hardware) + 1));

		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "**(SHARED) Rasterizer Mode = ";
		switch (m_RenderMode)
		{
		case dae::Renderer::RenderMode::Software:
			std::cout << "SOFTWARE\n";
			break;
		case dae::Renderer::RenderMode::Hardware:
			std::cout << "HARDWARE\n";
			break;
		}
	}

	void Renderer::ToggleMeshRotation()
	{
		m_IsMeshRotating = !m_IsMeshRotating;

		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "**(SHARED) Vehicle Rotation ";
		if (m_IsMeshRotating)
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void Renderer::ToggleFireMesh()
	{
		if (m_RenderMode != RenderMode::Hardware) return;

		Mesh* pFireMesh{ m_pMeshVec[1] };

		pFireMesh->SetVisibility(!pFireMesh->IsVisible());

		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "**(HARDWARE)FireFX ";
		if (pFireMesh->IsVisible())
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void Renderer::ToggleSamplerState()
	{
		if (m_RenderMode != RenderMode::Hardware) return;

		m_pHardwareRenderer->ToggleSampleState(m_pMeshVec);
	}

	void Renderer::ToggleShadingMode()
	{
		if (m_RenderMode != RenderMode::Software) return;

		m_pSoftwareRenderer->ToggleLightingMode();
	}

	void Renderer::ToggleNormalMap()
	{
		if (m_RenderMode != RenderMode::Software) return;

		m_pSoftwareRenderer->ToggleNormalMap();
	}

	void Renderer::ToggleShowingDepthBuffer()
	{
		if (m_RenderMode != RenderMode::Software) return;

		m_pSoftwareRenderer->ToggleDepthBuffer();
	}

	void Renderer::ToggleShowingBoundingBoxes()
	{
		if (m_RenderMode != RenderMode::Software) return;

		m_pSoftwareRenderer->ToggleBoundingBoxVisible();
	}

	void Renderer::ToggleUniformBackground()
	{
		m_IsBackgroundUniform = !m_IsBackgroundUniform;

		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "**(SHARED) Uniform ClearColor ";
		if (m_IsBackgroundUniform)
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void Renderer::ToggleCulling()
	{
		// Go to the next cull mode
		m_CullMode = static_cast<CullMode>((static_cast<int>(m_CullMode) + 1) % (static_cast<int>(CullMode::None) + 1));

		SetConsoleTextAttribute(m_hConsole, 14); // 14 is the color code for yellow
		std::cout << "**(SHARED) CullMode = ";
		switch (m_CullMode)
		{
		case dae::CullMode::Back:
			std::cout << "BACK\n";
			break;
		case dae::CullMode::Front:
			std::cout << "FRONT\n";
			break;
		case dae::CullMode::None:
			std::cout << "NONE\n";
			break;
		}

		m_pSoftwareRenderer->SetCulling(m_CullMode);
		m_pHardwareRenderer->SetCulling(m_CullMode, m_pMeshVec);
	}

	void Renderer::ToggleMultiThreading()
	{
		if (m_RenderMode != RenderMode::Software) return;
		m_pSoftwareRenderer->ToggleMultiThreading();
	}

}
