// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details. 

#include "SonoTrace.h"
#include "GlobalShader.h"
#include "RHIDefinitions.h"
#include "Modules/ModuleManager.h"
#include "../Private/ScenePrivate.h"
#include "../Private/Nanite/NaniteRayTracing.h"
#if RHI_RAYTRACING
#define NUM_THREADS_PER_GROUP_DIMENSION 8

DEFINE_LOG_CATEGORY(SonoTraceUE);

class FSonoTraceRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSonoTraceRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSonoTraceRGS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, SceneUniformBuffer)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, EmitterCount)
		SHADER_PARAMETER(uint32, DistributionRayCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, AzimuthAnglesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ElevationAnglesBuffer)
	    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SensorConfigurationBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FStructuredOutputBufferElem>, OutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::RayTracingDebug;
	}
	
	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), NUM_THREADS_PER_GROUP_DIMENSION);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), NUM_THREADS_PER_GROUP_DIMENSION);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Z"), NUM_THREADS_PER_GROUP_DIMENSION);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSonoTraceRGS, "/Plugin/SonoTraceUE/private/SonoTrace.usf", "SonoTraceRGS", SF_RayGen);

class FSonoTraceCHS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSonoTraceCHS);

public:

	FSonoTraceCHS() = default;
	FSonoTraceCHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	class FNaniteRayTracing : SHADER_PERMUTATION_BOOL("NANITE_RAY_TRACING");
	using FPermutationDomain = TShaderPermutationDomain<FNaniteRayTracing>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FNaniteRayTracing>())
		{
			OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		}
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::RayTracingDebug;
	}
};
IMPLEMENT_GLOBAL_SHADER(FSonoTraceCHS, "/Plugin/SonoTraceUE/private/SonoTrace.usf", "closesthit=SonoTraceCHS", SF_RayHitGroup);

class FSonoTraceMS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSonoTraceMS);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	FSonoTraceMS() = default;
	FSonoTraceMS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::RayTracingDebug;
	}
};
IMPLEMENT_GLOBAL_SHADER(FSonoTraceMS, "/Plugin/SonoTraceUE/private/SonoTrace.usf", "SonoTraceMS", SF_RayMiss);

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

void FSonoTrace::BindSonoTraceCHSBindings(FRHICommandList& RHICmdList, const FViewInfo& View, FRHIRayTracingScene* RHIScene, FRHIUniformBuffer* SceneUniformBuffer, FRayTracingPipelineState* PipelineState)
{
	FSceneRenderingBulkObjectAllocator Allocator;

	auto Alloc = [&](uint32 Size, uint32 Align)
	{
		return RHICmdList.Bypass()
			? Allocator.Malloc(Size, Align)
			: RHICmdList.Alloc(Size, Align);
	};

	const int32 NumTotalBindings = View.VisibleRayTracingMeshCommands.Num();
	const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;
	FRayTracingLocalShaderBindings* Bindings = (FRayTracingLocalShaderBindings*)Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings));

	struct FBinding
	{
		int32 ShaderIndexInPipeline;
		uint32 NumUniformBuffers;
		FRHIUniformBuffer** UniformBufferArray;
	};

	auto SetupBinding = [&](FSonoTraceCHS::FPermutationDomain PermutationVector)
	{
		auto Shader = View.ShaderMap->GetShader<FSonoTraceCHS>(PermutationVector);
		auto HitGroupShader = Shader.GetRayTracingShader();

		FBinding Binding;
		Binding.ShaderIndexInPipeline = FindRayTracingHitGroupIndex(PipelineState, HitGroupShader, true);
		Binding.NumUniformBuffers = Shader->ParameterMapInfo.UniformBuffers.Num();
		Binding.UniformBufferArray = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * Binding.NumUniformBuffers, alignof(FRHIUniformBuffer*));

		const auto& ViewUniformBufferParameter = Shader->GetUniformBufferParameter<FViewUniformShaderParameters>();
		const auto& SceneUniformBufferParameter = Shader->GetUniformBufferParameter<FSceneUniformParameters>();

		if (ViewUniformBufferParameter.IsBound())
		{
			check(ViewUniformBufferParameter.GetBaseIndex() < Binding.NumUniformBuffers);
			Binding.UniformBufferArray[ViewUniformBufferParameter.GetBaseIndex()] = View.ViewUniformBuffer.GetReference();
		}

		if (SceneUniformBufferParameter.IsBound())
		{
			check(SceneUniformBufferParameter.GetBaseIndex() < Binding.NumUniformBuffers);
			Binding.UniformBufferArray[SceneUniformBufferParameter.GetBaseIndex()] = SceneUniformBuffer;
		}

		return Binding;
	};

	FSonoTraceCHS::FPermutationDomain PermutationVector;

	PermutationVector.Set<FSonoTraceCHS::FNaniteRayTracing>(false);
	FBinding ShaderBinding = SetupBinding(PermutationVector);

	PermutationVector.Set<FSonoTraceCHS::FNaniteRayTracing>(true);
	FBinding ShaderBindingNaniteRT = SetupBinding(PermutationVector);

	uint32 BindingIndex = 0;
	for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : View.VisibleRayTracingMeshCommands)
	{
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

		const FBinding& HelperBinding = MeshCommand.IsUsingNaniteRayTracing() ? ShaderBindingNaniteRT : ShaderBinding;

		FRayTracingLocalShaderBindings Binding = {};
		Binding.ShaderIndexInPipeline = HelperBinding.ShaderIndexInPipeline;
		Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
		Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
		Binding.UniformBuffers = HelperBinding.UniformBufferArray;
		Binding.NumUniformBuffers = HelperBinding.NumUniformBuffers;
		Binding.UserData = VisibleMeshCommand.InstanceIndex;

		Bindings[BindingIndex] = Binding;
		BindingIndex++;
	}
	constexpr bool bCopyDataToInlineStorage = false;
	RHICmdList.SetRayTracingHitGroups(
		RHIScene,
		PipelineState,
		NumTotalBindings, Bindings,
		bCopyDataToInlineStorage);
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

	// Setup RGS
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	auto RayGenShader = ShaderMap->GetShader<FSonoTraceRGS>();
	const bool bIsShaderValid = RayGenShader.IsValid();
	if (!bIsShaderValid)
		return;

	// Setup pipeline
	FRayTracingPipelineStateInitializer Initializer;
	Initializer.MaxPayloadSizeInBytes = GetRayTracingPayloadTypeMaxSize(ERayTracingPayloadType::RayTracingDebug);

	FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	FSonoTraceCHS::FPermutationDomain PermutationVectorCHS;

	PermutationVectorCHS.Set<FSonoTraceCHS::FNaniteRayTracing>(false);
	const auto HitGroupShader = Parameters.View->ShaderMap->GetShader<FSonoTraceCHS>(PermutationVectorCHS);

	PermutationVectorCHS.Set<FSonoTraceCHS::FNaniteRayTracing>(true);
	const auto HitGroupShaderNaniteRT = Parameters.View->ShaderMap->GetShader<FSonoTraceCHS>(PermutationVectorCHS);

	FRHIRayTracingShader* HitGroupTable[] = { HitGroupShader.GetRayTracingShader(), HitGroupShaderNaniteRT.GetRayTracingShader()};
	Initializer.SetHitGroupTable(HitGroupTable);
	Initializer.bAllowHitGroupIndexing = true; // Required for stable output using GetBaseInstanceIndex().

	const auto MissShader = ShaderMap->GetShader<FSonoTraceMS>();
	FRHIRayTracingShader* MissTable[] = { MissShader.GetRayTracingShader() };
	Initializer.SetMissShaderTable(MissTable);

	FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(GraphBuilder->RHICmdList, Initializer);

	// Set shader parameters
	FSonoTraceRGS::FParameters* PassParameters = GraphBuilder->AllocParameters<FSonoTraceRGS::FParameters>();	
	PassParameters->ViewUniformBuffer = Parameters.View->ViewUniformBuffer;
	PassParameters->MaxBounces = CachedParams.MaxBounces;
	PassParameters->EmitterCount = CachedParams.EmitterCount;
	PassParameters->DistributionRayCount = CachedParams.NumDistributionRays;
	PassParameters->SceneUniformBuffer = GetSceneUniformBufferRef(*GraphBuilder, *Parameters.View);

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

	FRHIRayTracingScene* RHIScene = CachedParams.Scene->RayTracingScene.GetRHIRayTracingScene();
	
	// Add the ray trace dispatch pass
	GraphBuilder->AddPass(
		RDG_EVENT_NAME("SonoTrace"),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, RayGenShader, RHIScene, Pipeline, Parameters, NumOfRays, this](FRHIRayTracingCommandList& RHICmdList)
		{
			const FRDGBufferSRVRef LayerView = CachedParams.Scene->RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base);
			if (LayerView) {
				PassParameters->TLAS = LayerView->GetRHI();

				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
				
				BindSonoTraceCHSBindings(RHICmdList, *Parameters.View, RHIScene, PassParameters->SceneUniformBuffer->GetRHI(), Pipeline);
				
				RHICmdList.SetRayTracingMissShader(RHIScene, 0, Pipeline, 0 /* ShaderIndexInPipeline */, 0, nullptr, 0);
				
				RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RHIScene, GlobalResources, NumOfRays, 1);
			}
		}
	);	
	AddEnqueueCopyPass(*GraphBuilder, CachedParams.GPUReadback, StructuredOutputBufferRef, NumOutput * sizeof(FStructuredOutputBufferElem));

	RunState = 2;
}
#else // !RHI_RAYTRACING
{
	unimplemented();
}
#endif