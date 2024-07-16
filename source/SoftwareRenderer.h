#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include "DataTypes.h"

namespace dae
{
	class Mesh;
	class Camera;
	class Texture;

	class SoftwareRenderer
	{
	public:
		SoftwareRenderer(SDL_Window* pWindow);
		~SoftwareRenderer();

		//good practice
		SoftwareRenderer(const SoftwareRenderer&) = delete;
		SoftwareRenderer(SoftwareRenderer&&) noexcept = delete;
		SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
		SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = delete;

		void Render(const std::unique_ptr<Camera>& pCamera, bool useUniformBackground);
		void ToggleDepthBuffer();
		void ToggleBoundingBoxVisible();
		void ToggleLightingMode();
		void ToggleNormalMap();
		void ToggleMultiThreading();
		void SetTextures(Texture* pDiffuseTexture, Texture* pNormalTexture, Texture* pSpecularTexture, Texture* pGlossinessTexture);
		void SetMesh(Mesh* pMesh);
		void SetCulling(CullMode cullMode);

		bool SaveBufferToImage() const;

	private:
		enum class LightingMode
		{
			Combined,
			ObservedArea,
			Diffuse,
			Specular
		};

		//console color code thing
		HANDLE m_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);


		SDL_Window* m_pWindow{};
		int m_Width{};
		int m_Height{};

		//buffer stuff
		SDL_Surface* m_pFrontBuffer{ nullptr };
		SDL_Surface* m_pBackBuffer{ nullptr };
		uint32_t* m_pBackBufferPixels{};

		float* m_pDepthBufferPixels{};

		ThreadMode m_ThreadMode = ThreadMode::Synchronous;

		ThreadMode m_NextMode = ThreadMode::Synchronous;

		//DifferingModes and conditional
		bool m_ShowDepthBuffer{};
		bool m_ShowBoundingBox{};

		bool m_ThreadModeChange = false;

		LightingMode m_LightingMode{ LightingMode::Combined };
		CullMode m_CullMode{ CullMode::Back };
		bool m_RotateMesh{ true };
		bool m_NormalMapActive{ true };


		//resources
		Mesh* m_pMesh{};
		Texture* m_pDiffuseTexture{};
		Texture* m_pNormalTexture{};
		Texture* m_pSpecularTexture{};
		Texture* m_pGlossinessTexture{};

		//Function that transforms the vertices from the mesh from World space to Screen space
		void VertexTransformationFunction(std::vector<Vertex_Out>& verticesOut, const std::unique_ptr<Camera>& pCamera);
		void RenderTriangle(const std::vector<Vector2>& rasterVertices, const std::vector<Vertex_Out>& verticesOut, const std::vector<uint32_t>& indices, int vertexIdx, bool swapVertices) const;

		void ClearBackground(bool useUniformBackground) const;
		void ResetDepthBuffer() const;
		void PixelShading(int pixelIdx, const Vertex_Out& pixelInfo) const;
		inline Vector2 CalculateNDCToRaster(const Vector3& ndcVertex) const;
		inline bool IsOutsideFrustum(const Vector4& v) const;

	};
}
