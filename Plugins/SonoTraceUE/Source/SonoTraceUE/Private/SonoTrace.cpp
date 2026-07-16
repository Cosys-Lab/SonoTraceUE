// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details.

#include "SonoTrace.h"
#include "GlobalShader.h"
#include "RHIDefinitions.h"
#include "Modules/ModuleManager.h"
#include "RenderGraphUtils.h"
#include "../Private/ScenePrivate.h"

#if RHI_RAYTRACING

DEFINE_LOG_CATEGORY(SonoTraceUE);

// GPU Stats for profiling - shows up in 'stat gpu' and profilegpu
DECLARE_GPU_STAT_NAMED(SonoTraceRayTracing, TEXT("SonoTrace RayTracing"));

class FSonoTraceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSonoTraceCS)
	SHADER_USE_PARAMETER_STRUCT(FSonoTraceCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RayTracingSceneMetadata)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, SceneUniformBuffer)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, EmitterCount)
		SHADER_PARAMETER(uint32, DistributionRayCount)
		SHADER_PARAMETER(uint32, TotalRayCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, AzimuthAnglesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ElevationAnglesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SensorConfigurationBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FStructuredOutputBufferElem>, OutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsRayTracingEnabledForProject(Parameters.Platform) && FDataDrivenShaderPlatformInfo::GetSupportsInlineRayTracing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Current inline ray tracing implementation requires a 1:1 mapping between thread groups and waves,
		// and only supports Wave32 mode.
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);

		// Needed to resolve Nanite ray tracing proxy geometry through GetInstanceSceneData()/vertex fetch.
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}

	static constexpr uint32 ThreadGroupSize = 32;
};
IMPLEMENT_GLOBAL_SHADER(FSonoTraceCS, "/Plugin/SonoTraceUE/private/SonoTrace.usf", "SonoTraceCS", SF_Compute);

FSonoTrace::FSonoTrace()
{
}

void FSonoTrace::BeginRendering()
{
	// If the handle is already initialized and valid, no need to do anything
	if (SonoTraceRenderDelegate.IsValid())
		return;

	// Get the Renderer Module
	// and add the entry to the callbacks so it can be executed each frame after the scene rendering is done
	const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	if (RendererModule)
	{
		SonoTraceRenderDelegate = RendererModule->RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this, &FSonoTrace::Execute_RenderThread));
	}

}

//Stop the compute shader execution
void FSonoTrace::EndRendering()
{
	//If the handle is not valid, then there is no cleanup to do
	if (!SonoTraceRenderDelegate.IsValid())
	{
		return;
	}
	//Get the Renderer Module and remove the entry from the PostOpaqueRender callback
	const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	if (RendererModule)
	{
		RendererModule->RemovePostOpaqueRenderDelegate(SonoTraceRenderDelegate);
	}

	SonoTraceRenderDelegate.Reset();

}


void FSonoTrace::UpdateParameters(const FSonoTraceParameters& InputParameters)
{
	CachedParams = InputParameters;
	bCachedParamsAreValid = true;
}

// Delegate Handles
void FSonoTrace::Execute_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	if (!CachedParams.Scene || !CachedParams.Scene->RayTracingScene.IsCreated()) return;

	FRDGBuilder* GraphBuilder = Parameters.GraphBuilder;

	//If there are no cached parameters to use, skip
	//If no Render Target is supplied in the cachedParams, skip
	if (!bCachedParamsAreValid)
	{
		return;
	}

	//Render Thread Assertion
	check(IsInRenderingThread());

	// If using a fixed rate, calculate it here
	const double CurrentTime = FPlatformTime::Seconds();

	if (!RunOnTriggerOnly)
	{
		if (CurrentTime - LastExecutionTime < 1 / RunRate)
		{
			return;
		}
		RunState = 1;
	}else if (RunState != 1)
	{
		return;
	}

	LastExecutionTime = CurrentTime;
	ExecutionCounter += 1;
	CurrentTimestamp = FDateTime::Now().ToUnixTimestamp();

	// Setup CS
	const TShaderRef<FSonoTraceCS> ComputeShader = Parameters.View->ShaderMap->GetShader<FSonoTraceCS>();
	if (!ComputeShader.IsValid())
		return;

	const FRayTracingScene::FViewHandle& ViewHandle = Parameters.View->GetRayTracingSceneViewHandle();
	const FRDGBufferSRVRef LayerView = CachedParams.Scene->RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, ViewHandle);
	if (!LayerView)
		return;

	// Set shader parameters
	FSonoTraceCS::FParameters* PassParameters = GraphBuilder->AllocParameters<FSonoTraceCS::FParameters>();
	PassParameters->TLAS = LayerView;
	PassParameters->ViewUniformBuffer = Parameters.View->ViewUniformBuffer;
	PassParameters->SceneUniformBuffer = GetSceneUniformBufferRef(*GraphBuilder, *Parameters.View);
	// Only actually read on platforms where PLATFORM_SUPPORTS_INLINE_RAY_TRACING_TRIANGLE_NORMALS is 0 (e.g. D3D12); nullable since the buffer is only populated when the engine has an inline ray tracing pass
	// enabled elsewhere this frame.
	PassParameters->RayTracingSceneMetadata = Parameters.View->InlineRayTracingBindingDataBuffer ? GraphBuilder->CreateSRV(Parameters.View->InlineRayTracingBindingDataBuffer) : nullptr;
	PassParameters->MaxBounces = CachedParams.MaxBounces;
	PassParameters->EmitterCount = CachedParams.EmitterCount;
	PassParameters->DistributionRayCount = CachedParams.NumDistributionRays;

	// Set up all angles and figure out counts
	uint32 NumOfRays = CachedParams.NumDistributionRays;
	const uint32 NumOfDirectPathAngles = CachedParams.DirectPathAzimuthAngles.Num();
	TArray<float> AzimuthAngles = CachedParams.DistributionAzimuthAngles;
	TArray<float> ElevationAngles = CachedParams.DistributionElevationAngles;
	if (CachedParams.EnableDirectPath)
	{
		NumOfRays += NumOfDirectPathAngles;
		AzimuthAngles.Append(CachedParams.DirectPathAzimuthAngles);
		ElevationAngles.Append(CachedParams.DirectPathElevationAngles);
	}
	PassParameters->TotalRayCount = NumOfRays;
	const uint32 NumOutput = NumOfRays * CachedParams.MaxBounces;
	AzimuthAnglesBufferRef = CreateStructuredBuffer(
		*GraphBuilder,
		TEXT("AzimuthAnglesInputBuffer"),
		sizeof(float),
		NumOfRays,
		AzimuthAngles.GetData(),
		sizeof(float) * AzimuthAngles.Num(),
		ERDGInitialDataFlags::None);
	ElevationAnglesBufferRef = CreateStructuredBuffer(
		*GraphBuilder,
		TEXT("ElevationAnglesInputBuffer"),
		sizeof(float),
		NumOfRays,
		ElevationAngles.GetData(),
		sizeof(float) * ElevationAngles.Num(),
		ERDGInitialDataFlags::None);
	PassParameters->AzimuthAnglesBuffer = GraphBuilder->CreateUAV(AzimuthAnglesBufferRef, PF_R16_UINT);
	PassParameters->ElevationAnglesBuffer = GraphBuilder->CreateUAV(ElevationAnglesBufferRef, PF_R16_UINT);

	constexpr uint32 SensorConfigSize = 7 + 3 * MaxEmitterCount;
    float SensorConfigurationData[SensorConfigSize];
	SensorConfigurationData[0] = static_cast<float>(CachedParams.SensorPosition.X);
	SensorConfigurationData[1] = static_cast<float>(CachedParams.SensorPosition.Y);
	SensorConfigurationData[2] = static_cast<float>(CachedParams.SensorPosition.Z);
	SensorConfigurationData[3] = static_cast<float>(CachedParams.SensorRotation.Roll);
	SensorConfigurationData[4] = static_cast<float>(CachedParams.SensorRotation.Pitch);
	SensorConfigurationData[5] = static_cast<float>(CachedParams.SensorRotation.Yaw);
	SensorConfigurationData[6] = CachedParams.MaxTraceDistance;
	for (uint32 i = 0; i < CachedParams.EmitterCount; i++)
	{
		SensorConfigurationData[7 + 3 * i] = CachedParams.EmitterPositions[i].X;
		SensorConfigurationData[7 + 3 * i + 1] = CachedParams.EmitterPositions[i].Y;
		SensorConfigurationData[7 + 3 * i + 2] = CachedParams.EmitterPositions[i].Z;
	}
	SensorConfigurationBufferRef = CreateStructuredBuffer(
		*GraphBuilder,
		TEXT("SensorConfigurationBuffer"),
		sizeof(float),
		SensorConfigSize,
		SensorConfigurationData,
		sizeof(SensorConfigurationData),
		ERDGInitialDataFlags::None);
	PassParameters->SensorConfigurationBuffer = GraphBuilder->CreateUAV(SensorConfigurationBufferRef, PF_R32_FLOAT);

	StructuredOutputBufferRef = CreateStructuredBuffer(
	*GraphBuilder,
	TEXT("StructuredOutputBuffer"),
	sizeof(FStructuredOutputBufferElem),
	NumOutput,
	nullptr,
	0,
	ERDGInitialDataFlags::None);
	PassParameters->OutputBuffer = GraphBuilder->CreateUAV(StructuredOutputBufferRef, PF_Unknown);

	// Add GPU stat scope for profiling (shows in 'stat gpu' and 'profilegpu')
	RDG_EVENT_SCOPE_STAT(*GraphBuilder, SonoTraceRayTracing, "SonoTrace RayTracing");

	FComputeShaderUtils::AddPass(
		*GraphBuilder,
		RDG_EVENT_NAME("SonoTrace"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(NumOfRays, FSonoTraceCS::ThreadGroupSize));

	AddEnqueueCopyPass(*GraphBuilder, CachedParams.GPUReadback, StructuredOutputBufferRef, NumOutput * sizeof(FStructuredOutputBufferElem));

	RunState = 2;
}
#else // !RHI_RAYTRACING
{
	unimplemented();
}
#endif
