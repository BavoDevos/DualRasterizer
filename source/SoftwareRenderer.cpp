#include "pch.h"
#include "SoftwareRenderer.h"
#include "Mesh.h"
#include "Camera.h"
#include "Texture.h"
#include "Utils.h"
#include <ppl.h> // Parallel Stuff
#include <thread>
#include <future>
#include <vector>

namespace dae
{
	dae::SoftwareRenderer::SoftwareRenderer(SDL_Window* pWindow)
		: m_pWindow{ pWindow }
	{
		//Initialize
		SDL_GetWindowSize(pWindow, &m_Width, &m_Height);

		//Create Buffers
		m_pFrontBuffer = SDL_GetWindowSurface(pWindow);
		m_pBackBuffer = SDL_CreateRGBSurface(0, m_Width, m_Height, 32, 0, 0, 0, 0);
		m_pBackBufferPixels = static_cast<uint32_t*>(m_pBackBuffer->pixels);
		m_pDepthBufferPixels = new float[static_cast<uint32_t>(m_Width * m_Height)];
		ResetDepthBuffer();
	}

	dae::SoftwareRenderer::~SoftwareRenderer()
	{
		delete[] m_pDepthBufferPixels;
	}
	void dae::SoftwareRenderer::Render(const std::unique_ptr<Camera>& pCamera, bool useUniformBackground)
	{
		// Reset the depth buffer and paint the canvas black in one step
		ResetDepthBuffer();
		ClearBackground(useUniformBackground);

		//Lock BackBuffer
		SDL_LockSurface(m_pBackBuffer);

		if (m_pMesh)
		{
			std::vector<Vertex_Out> verticesOut;

			// Convert all the vertices in the mesh from world space to NDC space
			VertexTransformationFunction(verticesOut, pCamera);

			// Create a vector for all the vertices in raster space
			std::vector<Vector2> verticesRasterSpace;

			// Convert all the vertices from NDC space to raster space in one step
			for (const Vertex_Out& ndcVertex : verticesOut)
			{
				verticesRasterSpace.push_back(CalculateNDCToRaster(ndcVertex.position));
			}

			std::vector<uint32_t>& indices{ m_pMesh->GetIndices() };
			std::vector<std::future<void>> asyncFutures{};
			unsigned int nrCores{ std::thread::hardware_concurrency() };
			unsigned int trianglesPerTask{};
			unsigned int remainingTriangles{};
			unsigned int curTriangleIdx{};

			// Depending on the topology of the mesh, use indices differently
			switch (m_pMesh->GetPrimitiveTopology())
			{
			case PrimitiveTopology::TriangleList:
				// For each triangle
				switch (m_ThreadMode)
				{
				case dae::ThreadMode::Synchronous:
					for (int curStartVertexIdx = 0; curStartVertexIdx < indices.size(); curStartVertexIdx += 3)
					{
						RenderTriangle(verticesRasterSpace, verticesOut, indices, curStartVertexIdx, false);
					}
					break;
				case dae::ThreadMode::Async:
					trianglesPerTask = indices.size() / (3 * nrCores);
					remainingTriangles = indices.size() % (3 * nrCores);
					curTriangleIdx = 0; // Initialize curTriangleIdx
					for (unsigned int coreIdx = 0; coreIdx < nrCores; ++coreIdx)
					{
						unsigned int taskSize{ trianglesPerTask };
						if (remainingTriangles > 0)
						{
							++taskSize;
							--remainingTriangles;
						}
						asyncFutures.push_back(
							std::async(std::launch::async, [=, &verticesRasterSpace, &verticesOut, &indices]
								{
									const unsigned int endTriangleIdx{ curTriangleIdx + taskSize * 3 };
						for (unsigned int triangleIdx{ curTriangleIdx }; triangleIdx < endTriangleIdx; triangleIdx += 3)
						{
							if (triangleIdx + 2 < indices.size()) // Check if indices are within bounds
							{
								RenderTriangle(verticesRasterSpace, verticesOut, indices, triangleIdx, false);
							}
						}
								})
						);
						curTriangleIdx += taskSize * 3;
					}
					for (const std::future<void>& f : asyncFutures)
					{
						f.wait();
					}
					break;

				case dae::ThreadMode::Parallel:
					concurrency::parallel_for(0, static_cast<int>((indices.size() / 3)),
						[=, this](int i)
						{
							RenderTriangle(verticesRasterSpace, verticesOut, indices, (i * 3), false);
						});
					break;
				}
				break;
			case PrimitiveTopology::TriangleStrip:
				// For each triangle
				switch (m_ThreadMode)
				{
				case dae::ThreadMode::Synchronous:
					for (int curStartVertexIdx = 0; curStartVertexIdx < indices.size() - 2; ++curStartVertexIdx)
					{
						RenderTriangle(verticesRasterSpace, verticesOut, indices, curStartVertexIdx, curStartVertexIdx % 2);
					}
					break;
				case dae::ThreadMode::Async:
					trianglesPerTask = (indices.size() - 2) / nrCores;
					remainingTriangles = (indices.size() - 2) % nrCores;
					curTriangleIdx = 0; // Initialize curTriangleIdx
					for (unsigned int coreIdx = 0; coreIdx < nrCores; ++coreIdx)
					{
						unsigned int taskSize{ trianglesPerTask };
						if (remainingTriangles > 0)
						{
							++taskSize;
							--remainingTriangles;
						}
						asyncFutures.push_back(
							std::async(std::launch::async, [=, &verticesRasterSpace, &verticesOut, &indices]
								{
									const unsigned int endTriangleIdx{ curTriangleIdx + taskSize };
						for (unsigned int triangleIdx{ curTriangleIdx }; triangleIdx < endTriangleIdx; ++triangleIdx)
						{
							if (triangleIdx + 2 < indices.size()) // Check if indices are within bounds
							{
								RenderTriangle(verticesRasterSpace, verticesOut, indices, triangleIdx, triangleIdx % 2);
							}
						}
								})
						);
						curTriangleIdx += taskSize;
					}
					for (const std::future<void>& f : asyncFutures)
					{
						f.wait();
					}
					break;

				case dae::ThreadMode::Parallel:
					concurrency::parallel_for(0, static_cast<int>((indices.size() - 2)),
						[=, this](int i)
						{
							RenderTriangle(verticesRasterSpace, verticesOut, indices, i, i % 2);
						});
					break;
				}
				break;
			}
		}
		
		if (m_ThreadModeChange)
		{
			m_ThreadMode = m_NextMode;
			m_ThreadModeChange = false;
		}

		//Update SDL Surface
		SDL_UnlockSurface(m_pBackBuffer);
		SDL_BlitSurface(m_pBackBuffer, 0, m_pFrontBuffer, 0);
		SDL_UpdateWindowSurface(m_pWindow);
	}

	void SoftwareRenderer::SetTextures(Texture* pDiffuseTexture, Texture* pNormalTexture, Texture* pSpecularTexture, Texture* pGlossinessTexture)
	{
		m_pDiffuseTexture = pDiffuseTexture;
		m_pNormalTexture = pNormalTexture;
		m_pSpecularTexture = pSpecularTexture;
		m_pGlossinessTexture = pGlossinessTexture;
	}

	void SoftwareRenderer::SetMesh(Mesh* pMesh)
	{
		// Set the current mesh that should be displayed
		m_pMesh = pMesh;
	}

	void SoftwareRenderer::SetCulling(CullMode cullMode)
	{
		m_CullMode = cullMode;
	}

	void dae::SoftwareRenderer::VertexTransformationFunction(std::vector<Vertex_Out>& verticesOut, const std::unique_ptr<Camera>& pCamera)
	{
		// Retrieve the world matrix of the current mesh
		Matrix& worldMatrix{ m_pMesh->GetWorldMatrix() };
		std::vector<Vertex> vertices{ m_pMesh->GetVertices() };

		// Calculate the transformation matrix for this mesh
		Matrix worldViewProjectionMatrix{ worldMatrix * pCamera->GetViewMatrix() * pCamera->GetProjectionMatrix() };

		// Reserve the amount of vertices into the new vertex list
		verticesOut.reserve(vertices.size());

		// For each vertex in the mesh
		for (const Vertex& v : vertices)
		{
			// Create a new vertex	
			Vertex_Out vOut{ {}, v.normal, v.tangent, v.uv, v.color };

			// Tranform the vertex using the inversed view matrix
			vOut.position = worldViewProjectionMatrix.TransformPoint({ v.position, 1.0f });

			// Calculate the view direction
			vOut.viewDirection = Vector3{ vOut.position.x, vOut.position.y, vOut.position.z };
			vOut.viewDirection.Normalize();

			// Divide all properties of the position by the original z (stored in position.w)
			vOut.position.x /= vOut.position.w;
			vOut.position.y /= vOut.position.w;
			vOut.position.z /= vOut.position.w;

			// Transform the normal and the tangent of the vertex
			vOut.normal = worldMatrix.TransformVector(v.normal);
			vOut.tangent = worldMatrix.TransformVector(v.tangent);

			// Add the new vertex to the list of NDC vertices
			verticesOut.emplace_back(vOut);
		}
	}

	void dae::SoftwareRenderer::RenderTriangle(const std::vector<Vector2>& rasterVertices, const std::vector<Vertex_Out>& verticesOut, const std::vector<uint32_t>& indices, int curVertexIdx, bool swapVertices) const
	{
		// Calcalate the indexes of the vertices on this triangle
		const uint32_t vertexIdx0{ indices[static_cast<uint32_t>(curVertexIdx)] };
		const uint32_t vertexIdx1{ indices[static_cast<uint32_t>(curVertexIdx + 1 * !swapVertices + 2 * swapVertices)] };
		const uint32_t vertexIdx2{ indices[static_cast<uint32_t>(curVertexIdx + 2 * !swapVertices + 1 * swapVertices)] };
	
		// If a triangle has the same vertex twice
		// Or if a one of the vertices is outside the frustum
		// Continue
		if (vertexIdx0 == vertexIdx1 || vertexIdx1 == vertexIdx2 || vertexIdx0 == vertexIdx2 ||
			IsOutsideFrustum(verticesOut[vertexIdx0].position) ||
			IsOutsideFrustum(verticesOut[vertexIdx1].position) ||
			IsOutsideFrustum(verticesOut[vertexIdx2].position))
			return;
	
		// Get all the current vertices
		const Vector2 v0{ rasterVertices[vertexIdx0] };
		const Vector2 v1{ rasterVertices[vertexIdx1] };
		const Vector2 v2{ rasterVertices[vertexIdx2] };
	
		// Calculate the edges of the current triangle
		const Vector2 edge01{ v1 - v0 };
		const Vector2 edge12{ v2 - v1 };
		const Vector2 edge20{ v0 - v2 };
	
		// Calculate the area of the current triangle
		const float fullTriangleArea{ Vector2::Cross(edge01, edge12) };
	
		// If the triangle area is 0 or NaN, continue to the next triangle
		if (abs(fullTriangleArea) < FLT_EPSILON || isnan(fullTriangleArea)) return;
	
		// Calculate the bounding box of this triangle
		Vector2 minBoundingBox{ Vector2::Min(v0, Vector2::Min(v1, v2)) };
		Vector2 maxBoundingBox{ Vector2::Max(v0, Vector2::Max(v1, v2)) };
	
		// A margin that enlarges the bounding box, makes sure that some pixels do no get ignored
		const int margin{ 1 };
	
		// Calculate the start and end pixel bounds of this triangle
		const int startX{ std::max(static_cast<int>(minBoundingBox.x - margin), 0) };
		const int startY{ std::max(static_cast<int>(minBoundingBox.y - margin), 0) };
		const int endX{ std::min(static_cast<int>(maxBoundingBox.x + margin), m_Width) };
		const int endY{ std::min(static_cast<int>(maxBoundingBox.y + margin), m_Height) };
	
		for (int py = startY; py < endY; ++py)
		{
			for (int px = startX; px < endX; ++px)
			{
				int pixelIdx = px + py * m_Width;
				Vector2 curPixel(static_cast<float>(px),static_cast<float>(py));
				if (m_ShowBoundingBox)
				{
					m_pBackBufferPixels[pixelIdx] = SDL_MapRGB(m_pBackBuffer->format, 255, 255, 255);
					continue;
				}
	
				const Vector2 v0ToPoint = curPixel - v0;
				const Vector2 v1ToPoint = curPixel - v1;
				const Vector2 v2ToPoint = curPixel - v2;
	
				float edge01PointCross = Vector2::Cross(edge01, v0ToPoint);
				float edge12PointCross = Vector2::Cross(edge12, v1ToPoint);
				float edge20PointCross = Vector2::Cross(edge20, v2ToPoint);
	
				bool isFrontFaceHit = edge01PointCross >= 0 && edge12PointCross >= 0 && edge20PointCross >= 0;
				bool isBackFaceHit = edge01PointCross <= 0 && edge12PointCross <= 0 && edge20PointCross <= 0;
	
				if ((m_CullMode == CullMode::Back && !isFrontFaceHit) ||
					(m_CullMode == CullMode::Front && !isBackFaceHit) ||
					(m_CullMode == CullMode::None && !isBackFaceHit && !isFrontFaceHit)) continue;
	
				// Calculate the barycentric weights
				float weightV0 = edge12PointCross / fullTriangleArea;
				float weightV1 = edge20PointCross / fullTriangleArea;
				float weightV2 = edge01PointCross / fullTriangleArea;
	
				// Calculate the Z depth at this pixel
				float interpolatedZDepth = 1.0f / (weightV0 / verticesOut[vertexIdx0].position.z + weightV1 / verticesOut[vertexIdx1].position.z + weightV2 / verticesOut[vertexIdx2].position.z);
	
				// If the depth is outside the frustum, or if the current depth buffer is less than the current depth, continue to the next pixel
				if (m_pDepthBufferPixels[pixelIdx] < interpolatedZDepth) continue;
	
				// Save the new depth
				m_pDepthBufferPixels[pixelIdx] = interpolatedZDepth;
	
				// The pixel info
				Vertex_Out pixelInfo;
	
				if (m_ShowDepthBuffer)
				{
					// Remap the Z depth
					float depthColor = Remap(interpolatedZDepth, 0.997f, 1.0f);
	
					// Set the color of the current pixel to showcase the depth
					pixelInfo.color = { depthColor, depthColor, depthColor };
	
				}
				else
				{
					// Calculate the W depth at this pixel
					const float interpolatedWDepth
					{
						1.0f /
							(weightV0 / verticesOut[vertexIdx0].position.w +
							weightV1 / verticesOut[vertexIdx1].position.w +
							weightV2 / verticesOut[vertexIdx2].position.w)
					};
	
					// Calculate the UV coordinate at this pixel
					pixelInfo.uv =
					{
						(weightV0 * verticesOut[vertexIdx0].uv / verticesOut[vertexIdx0].position.w +
						weightV1 * verticesOut[vertexIdx1].uv / verticesOut[vertexIdx1].position.w +
						weightV2 * verticesOut[vertexIdx2].uv / verticesOut[vertexIdx2].position.w)
							* interpolatedWDepth
					};
	
					// Calculate the normal at this pixel
					pixelInfo.normal =
						Vector3
					{
						(weightV0 * verticesOut[vertexIdx0].normal / verticesOut[vertexIdx0].position.w +
						weightV1 * verticesOut[vertexIdx1].normal / verticesOut[vertexIdx1].position.w +
						weightV2 * verticesOut[vertexIdx2].normal / verticesOut[vertexIdx2].position.w)
							* interpolatedWDepth
					}.Normalized();
	
					// Calculate the tangent at this pixel
					pixelInfo.tangent =
						Vector3
					{
						(weightV0 * verticesOut[vertexIdx0].tangent / verticesOut[vertexIdx0].position.w +
						weightV1 * verticesOut[vertexIdx1].tangent / verticesOut[vertexIdx1].position.w +
						weightV2 * verticesOut[vertexIdx2].tangent / verticesOut[vertexIdx2].position.w)
							* interpolatedWDepth
					}.Normalized();
	
					// Calculate the view direction at this pixel
					pixelInfo.viewDirection =
						Vector3
					{
						(weightV0 * verticesOut[vertexIdx0].viewDirection / verticesOut[vertexIdx0].position.w +
						weightV1 * verticesOut[vertexIdx1].viewDirection / verticesOut[vertexIdx1].position.w +
						weightV2 * verticesOut[vertexIdx2].viewDirection / verticesOut[vertexIdx2].position.w)
							* interpolatedWDepth
					}.Normalized();
	
				}
	
				// Calculate the shading at this pixel and display it on screen
				PixelShading(px + (py * m_Width), pixelInfo);
			}
		}
	}
#pragma region testing

#pragma endregion

	void SoftwareRenderer::ClearBackground(bool useUniformBackground) const
	{
		// Fill the background
		int colorValue{ static_cast<int>((useUniformBackground ? 0.1f : 0.39f) * 255) };
		SDL_FillRect(m_pBackBuffer, NULL, SDL_MapRGB(m_pBackBuffer->format, colorValue, colorValue, colorValue));
	}

	void SoftwareRenderer::ResetDepthBuffer() const
	{
		// The nr of pixels in the buffer
		const int nrPixels{ m_Width * m_Height };

		// Set everything in the depth buffer to the value FLT_MAX
		std::fill_n(m_pDepthBufferPixels, nrPixels, FLT_MAX);
	}

	void SoftwareRenderer::PixelShading(int pixelIdx, const Vertex_Out& pixelInfo) const
	{
		// The normal that should be used in calculations
		Vector3 useNormal{ pixelInfo.normal };

		// If the normal map is active
		if (m_NormalMapActive)
		{
			// Calculate the binormal in this pixel
			Vector3 binormal = Vector3::Cross(pixelInfo.normal, pixelInfo.tangent);

			// Create a matrix using the tangent, normal and binormal
			Matrix tangentSpaceAxis = Matrix{ pixelInfo.tangent, binormal, pixelInfo.normal, Vector3::Zero };

			// Sample a color from the normal map and clamp it between -1 and 1
			ColorRGB currentNormalMap{ 2.0f * m_pNormalTexture->Sample(pixelInfo.uv) - ColorRGB{ 1.0f, 1.0f, 1.0f } };

			// Make a vector3 of the colorRGB object
			Vector3 normalMapSample{ currentNormalMap.r, currentNormalMap.g, currentNormalMap.b };

			// Transform the normal map value using the calculated matrix of this pixel
			useNormal = tangentSpaceAxis.TransformVector(normalMapSample);
		}
		else
		{
			// If the normal map is not active, use the original normal
			useNormal = pixelInfo.normal;
		}

		// Create the light data
		Vector3 lightDirection{ 0.577f, -0.577f, 0.577f };
		lightDirection.Normalize();
		const float lightIntensity{ 7.0f };
		const float specularShininess{ 25.0f };

		// The final color that will be rendered
		ColorRGB finalColor{};
		ColorRGB ambientColor{ 0.025f, 0.025f, 0.025f };

		// Depending on the rendering state, do other things
		if (m_ShowDepthBuffer)
		{

			// Only render the depth which is saved in the color attribute of the pixel info
			finalColor += pixelInfo.color;
		}
		else
		{
			// Calculate the observed area in this pixel
			const float observedArea{ Vector3::DotClamped(useNormal.Normalized(), -lightDirection.Normalized()) };

			// Depending on the lighting mode, different shading should be applied
			switch (m_LightingMode)
			{
				case LightingMode::Combined:
				{
					// Calculate the lambert shader
					const ColorRGB lambert{ LightingUtils::Lambert(1.0f, m_pDiffuseTexture->Sample(pixelInfo.uv)) };
					// Calculate the phong exponent
					const float specularExp{ specularShininess * m_pGlossinessTexture->Sample(pixelInfo.uv).r };
					// Calculate the phong shader
					const ColorRGB specular{ m_pSpecularTexture->Sample(pixelInfo.uv) * LightingUtils::Phong(1.0f, specularExp, -lightDirection, pixelInfo.viewDirection, useNormal) };

					// Lambert + Phong + ObservedArea
					finalColor += (lightIntensity * lambert + specular) * observedArea + ambientColor;
					break;
				}
				case LightingMode::ObservedArea:
				{
					// Only show the calculated observed area
					finalColor += ColorRGB{ observedArea, observedArea, observedArea };
					break;
				}
				case LightingMode::Diffuse:
				{
					// Calculate the lambert shader and display it on screen together with the observed area
					finalColor += lightIntensity * observedArea * LightingUtils::Lambert(1.0f, m_pDiffuseTexture->Sample(pixelInfo.uv));
					break;
				}
				case LightingMode::Specular:
				{
					// Calculate the phong exponent
					const float specularExp{ specularShininess * m_pGlossinessTexture->Sample(pixelInfo.uv).r };
					// Calculate the phong shader
					const ColorRGB specular{ m_pSpecularTexture->Sample(pixelInfo.uv) * LightingUtils::Phong(1.0f, specularExp, -lightDirection, pixelInfo.viewDirection, useNormal) };
					// Phong + observed area
					finalColor += specular * observedArea;
					break;
				}
			}

			// Create the ambient color	and add it to the final color
			const ColorRGB ambientColor{ 0.025f, 0.025f, 0.025f };
			finalColor += ambientColor;
		}

		//Update Color in Buffer
		finalColor.MaxToOne();

		m_pBackBufferPixels[pixelIdx] = SDL_MapRGB(m_pBackBuffer->format,
			static_cast<uint8_t>(finalColor.r * 255),
			static_cast<uint8_t>(finalColor.g * 255),
			static_cast<uint8_t>(finalColor.b * 255));
	}

	inline Vector2 dae::SoftwareRenderer::CalculateNDCToRaster(const Vector3& ndcVertex) const
	{
		return Vector2
		{
			(ndcVertex.x + 1) / 2.0f * m_Width,
			(1.0f - ndcVertex.y) / 2.0f * m_Height
		};
	}

	inline bool SoftwareRenderer::IsOutsideFrustum(const Vector4& v) const
	{
		return v.x < -1.0f || v.x > 1.0f || v.y < -1.0f || v.y > 1.0f || v.z < 0.0f || v.z > 1.0f;
	}

	void SoftwareRenderer::ToggleDepthBuffer()
	{
		m_ShowDepthBuffer = !m_ShowDepthBuffer;

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "**(SOFTWARE) DepthBuffer Visualization ";
		if (m_ShowDepthBuffer)
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void SoftwareRenderer::ToggleBoundingBoxVisible()
	{
		m_ShowBoundingBox = !m_ShowBoundingBox;

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "**(SOFTWARE) BoundingBox Visualization ";
		if (m_ShowBoundingBox)
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void dae::SoftwareRenderer::ToggleLightingMode()
	{
		// Shuffle through all the lighting modes
		m_LightingMode = static_cast<LightingMode>((static_cast<int>(m_LightingMode) + 1) % (static_cast<int>(LightingMode::Specular) + 1));

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "**(SOFTWARE) Shading Mode = ";
		switch (m_LightingMode)
		{
		case dae::SoftwareRenderer::LightingMode::Combined:
			std::cout << "COMBINED\n";
			break;
		case dae::SoftwareRenderer::LightingMode::ObservedArea:
			std::cout << "OBSERVED_AREA\n";
			break;
		case dae::SoftwareRenderer::LightingMode::Diffuse:
			std::cout << "DIFFUSE\n";
			break;
		case dae::SoftwareRenderer::LightingMode::Specular:
			std::cout << "SPECULAR\n";
			break;
		}
	}

	void dae::SoftwareRenderer::ToggleNormalMap()
	{
		// Toggle the normal map active variable
		m_NormalMapActive = !m_NormalMapActive;

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "**(SOFTWARE) NormalMap ";
		if (m_NormalMapActive)
		{
			std::cout << "ON\n";
		}
		else
		{
			std::cout << "OFF\n";
		}
	}

	void SoftwareRenderer::ToggleMultiThreading()
	{

		m_NextMode = static_cast<ThreadMode>((static_cast<int>(m_NextMode) + 1) % (static_cast<int>(ThreadMode::Parallel) + 1));

		SetConsoleTextAttribute(m_hConsole, 13); // 13 is the color code for purple
		std::cout << "**(SOFTWARE) ThreadMode ";

		switch (m_NextMode)
		{
		case dae::ThreadMode::Synchronous:
			std::cout << "Synchronous\n";
			break;
		case dae::ThreadMode::Async:
			std::cout << "Async\n";
			break;
		case dae::ThreadMode::Parallel:
			std::cout << "Parallel_for\n";
			break;
		}

		m_ThreadModeChange = true;

	}

	bool dae::SoftwareRenderer::SaveBufferToImage() const
	{
		return SDL_SaveBMP(m_pBackBuffer, "Rasterizer_ColorBuffer.bmp");
	}
}



