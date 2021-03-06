//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#pragma once

#include "BsRenderBeastPrerequisites.h"
#include "BsTriangulation.h"
#include "BsMatrix4.h"
#include "BsMatrixNxM.h"
#include "BsRendererMaterial.h"
#include "BsGpuResourcePool.h"
#include "BsParamBlocks.h"
#include "BsLightRendering.h"

namespace bs { namespace ct
{
	struct LightProbesInfo;
	struct GBufferTextures;
	struct FrameInfo;
	class LightProbeVolume;

	/** @addtogroup RenderBeast
	 *  @{
	 */

	/** 
	 * Shader that renders the tetrahedra used for light probe evaluation. Tetrahedra depth is compare with current scene
	 * depth, and for each scene pixel the matching tetrahedron index is written to the output target.
	 */
	class TetrahedraRenderMat : public RendererMaterial<TetrahedraRenderMat>
	{
		RMAT_DEF("TetrahedraRender.bsl");

	public:
		TetrahedraRenderMat();

		/**
		 * Executes the material using the provided parameters.
		 * 
		 * @param[in]	view		View that is currently being rendered.
		 * @param[in]	sceneDepth	Depth of scene objects that should be lit.
		 * @param[in]	mesh		Mesh to render.
		 * @param[in]	output		Output texture created using the descriptor returned by getOutputDesc().
		 */
		void execute(const RendererView& view, const SPtr<Texture>& sceneDepth, const SPtr<Mesh>& mesh, 
			const SPtr<RenderTexture>& output);

		/**
		 * Returns the descriptors that can be used for creating the output render texture for this material. The render
		 * texture is expected to have a single color attachment, and a depth attachment. 
		 */
		static void getOutputDesc(const RendererView& view, POOLED_RENDER_TEXTURE_DESC& colorDesc, 
			POOLED_RENDER_TEXTURE_DESC& depthDesc);

		/** Returns the material variation matching the provided parameters. */
		static TetrahedraRenderMat* getVariation(bool msaa);
	private:
		GpuParamTexture mDepthBufferTex;

		static ShaderVariation VAR_NoMSAA;
		static ShaderVariation VAR_MSAA;
	};

	BS_PARAM_BLOCK_BEGIN(IrradianceEvaluateParamDef)
		BS_PARAM_BLOCK_ENTRY(float, gSkyBrightness)
		BS_PARAM_BLOCK_ENTRY(INT32, gNumTetrahedra)
	BS_PARAM_BLOCK_END

	extern IrradianceEvaluateParamDef gIrradianceEvaluateParamDef;

	/** Evaluates radiance from the light probe volume, or the sky if light probes are not available. */
	class IrradianceEvaluateMat : public RendererMaterial<IrradianceEvaluateMat>
	{
		RMAT_DEF("IrradianceEvaluate.bsl");

	public:
		IrradianceEvaluateMat();

		/**
		 * Executes the material using the provided parameters.
		 * 
		 * @param[in]	view				View that is currently being rendered.
		 * @param[in]	gbuffer				Previously rendered GBuffer textures.
		 * @param[in]	lightProbeIndices	Indices calculated by TetrahedraRenderMat.
		 * @param[in]	lightProbesInfo		Information about light probes.
		 * @param[in]	skybox				Skybox, if available. If sky is not available, but sky rendering is enabled, 
		 *									the system will instead use a default irradiance texture.
		 * @param[in]	output				Output texture to write the radiance to. The evaluated value will be added to 
		 *									existing radiance in the texture, using blending.
		 */
		void execute(const RendererView& view, const GBufferTextures& gbuffer, const SPtr<Texture>& lightProbeIndices,
			const LightProbesInfo& lightProbesInfo, const Skybox* skybox, const SPtr<RenderTexture>& output);

		/** 
		 * Returns the material variation matching the provided parameters. 
		 *
		 * @param[in]	msaaCount	Number of MSAA samples used by the view rendering the material.
		 * @param[in]	skyOnly		When true, only the sky irradiance will be evaluated. Otherwise light probe irradiance
		 *							will be evaluated.
		 */
		static IrradianceEvaluateMat* getVariation(UINT32 msaaCount, bool skyOnly);
	private:
		GBufferParams mGBufferParams;
		SPtr<GpuParamBlockBuffer> mParamBuffer;
		GpuParamTexture mParamInputTex;
		GpuParamTexture mParamSkyIrradianceTex;
		GpuParamBuffer mParamSHCoeffsBuffer;
		GpuParamBuffer mParamTetrahedraBuffer;
		GpuParamBuffer mParamTetFacesBuffer;
		bool mSkyOnly;

		static ShaderVariation VAR_MSAA_Probes;
		static ShaderVariation VAR_NoMSAA_Probes;
		static ShaderVariation VAR_MSAA_Sky;
		static ShaderVariation VAR_NoMSAA_Sky;
	};

	/** Contains information required by light probe shaders. Output by LightProbes. */
	struct LightProbesInfo
	{
		/** Contains a set of spherical harmonic coefficients for every light probe. */
		SPtr<GpuBuffer> shCoefficients;

		/** 
		 * Contains information about tetrahedra formed by light probes. First half of the buffer is populated by actual
		 * tetrahedrons, while the second half is populated by information about outer faces (triangles). @p numTetrahedra
		 * marks the spot where split happens.
		 */
		SPtr<GpuBuffer> tetrahedra;

		/** Contains additional information about outer tetrahedron faces, required for extrapolating tetrahedron data. */
		SPtr<GpuBuffer> faces;

		/** 
		 * Mesh representing the entire light probe volume. Each vertex has an associated tetrahedron (or face) index which
		 * can be used to map into the tetrahedra array to retrieve probe information.
		 */
		SPtr<Mesh> tetrahedraVolume;

		/** Total number of valid tetrahedra in the @p tetrahedra buffer. */
		UINT32 numTetrahedra;
	};

	/** Handles any pre-processing for light (irradiance) probe lighting. */
	class LightProbes
	{
		/** Internal information about a single light probe volume. */
		struct VolumeInfo
		{
			/** Volume containing the information about the probes. */
			LightProbeVolume* volume;
			/** Remains true as long as there are dirty probes in the volume. */
			bool isDirty;
		};

		/** 
		 * Information about a single tetrahedron, including neighbor information. Neighbor 4th index will be set to -1
		 * if the tetrahedron represents an outer face (which is not actually a tetrahedron, but a triangle, but is stored
		 * in the same array for convenience).
		 */
		struct TetrahedronData
		{
			Tetrahedron volume;
			Matrix4 transform;
		};

		/**
		 * Information about a single tetrahedron face, with information about extrusion and how to project a point in
		 * the extrusion volume, on to the face.
		 */
		struct TetrahedronFaceData
		{
			UINT32 innerVertices[3];
			UINT32 outerVertices[3];
			Vector3 normals[3];
			Matrix4 transform;
			UINT32 tetrahedron;
			bool quadratic;
		};
	public:
		LightProbes();

		/** Notifies sthe manager that the provided light probe volume has been added. */
		void notifyAdded(LightProbeVolume* volume);

		/** Notifies the manager that the provided light probe volume has some dirty light probes. */
		void notifyDirty(LightProbeVolume* volume);

		/** Notifies the manager that all the probes in the provided volume have been removed. */
		void notifyRemoved(LightProbeVolume* volume);

		/** Updates light probe tetrahedron data after probes changed (added/removed/moved). */
		void updateProbes();

		/** Returns true if there are any registered light probes. */
		bool hasAnyProbes() const;

		/** 
		 * Returns a set of buffers that can be used for rendering the light probes. updateProbes() must be called
		 * at least once before the buffer is populated. If the probes changed since the last call, call updateProbes()
		 * to refresh the buffer. 
		 */
		LightProbesInfo getInfo() const;

	private:
		/**
		 * Perform tetrahedrization of the provided point list, and outputs a list of tetrahedrons and outer faces of the
		 * volume. Each entry contains connections to nearby tetrahedrons/faces, as well as a matrix that can be used for
		 * calculating barycentric coordinates within the tetrahedron (or projected triangle barycentric coordinates for
		 * faces). 
		 * 
		 * @param[in,out]	positions					A set of positions to generate the tetrahedra from. If 
		 *												@p generateExtrapolationVolume is enabled then this array will be
		 *												appended with new vertices forming that volume.
		 * @param[out]		tetrahedra					A list of generated tetrahedra and relevant data.
		 * @param[out]		faces						A list of faces representing the surface of the tetrahedra volume.
		 * @param[in]		generateExtrapolationVolume	If true, the tetrahedron volume will be surrounded with points
		 *												at "infinity" (technically just far away).
		 */
		void generateTetrahedronData(Vector<Vector3>& positions, Vector<TetrahedronData>& tetrahedra, 
			Vector<TetrahedronFaceData>& faces, bool generateExtrapolationVolume = false);

		/** Resizes the GPU buffer used for holding tetrahedron data, to the specified size (in number of tetraheda). */
		void resizeTetrahedronBuffer(UINT32 count);

		/** Resizes the GPU buffer used for holding tetrahedron face data, to the specified size (in number of faces). */
		void resizeTetrahedronFaceBuffer(UINT32 count);

		/** 
		 * Resized the GPU buffer that stores light probe SH coefficients, to the specified size (in the number of probes). 
		 */
		void resizeCoefficientBuffer(UINT32 count);

		Vector<VolumeInfo> mVolumes;
		bool mTetrahedronVolumeDirty;

		UINT32 mMaxCoefficients;
		UINT32 mMaxTetrahedra;
		UINT32 mMaxFaces;

		Vector<TetrahedronData> mTetrahedronInfos;

		SPtr<GpuBuffer> mProbeCoefficientsGPU;
		SPtr<GpuBuffer> mTetrahedronInfosGPU;
		SPtr<GpuBuffer> mTetrahedronFaceInfosGPU;
		SPtr<Mesh> mVolumeMesh;
		UINT32 mNumValidTetrahedra;

		// Temporary buffers
		Vector<Vector3> mTempTetrahedronPositions;
		Vector<UINT32> mTempTetrahedronBufferIndices;
	};

	/** @} */
}}
