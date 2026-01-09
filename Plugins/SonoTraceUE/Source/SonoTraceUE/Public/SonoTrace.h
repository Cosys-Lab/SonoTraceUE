// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details.

#pragma once

#include "CoreMinimal.h"
#include "SceneInterface.h"
#include "RendererInterface.h" 
#include "RHIGPUReadback.h"

DECLARE_LOG_CATEGORY_EXTERN(SonoTraceUE, Log, All);

inline constexpr UINT32 MaxEmitterCount = 32;

class FSonoTrace;

struct FStructuredOutputBufferElem
{
	bool	IsHit;
	float   HitPosX;
	float   HitPosY;
	float   HitPosZ;
	float   HitReflectionX;
	float   HitReflectionY;
	float   HitReflectionZ;
	int     HitScenePrimitiveIndex;
	int     HitTriangleIndex;
	float   RayDistanceTotal;	
	bool    HitLineOfSightToSensor;	
	int     DirectPath;
	float   DistancesFromEmitterTotal[MaxEmitterCount];
};

struct  FSonoTraceParameters
{
	FScene* Scene = nullptr;
	FRHIGPUBufferReadback* GPUReadback = nullptr;

	TArray<float> DistributionAzimuthAngles; // Azimuth angles array for the reflection distribution
	TArray<float> DistributionElevationAngles; // Elevation angles array for the reflection distribution
	uint32 NumDistributionRays = 0; // Number of rays (matches the azimuth/elevation array size)
	FVector SensorPosition; // Sensor position (x, y, z) in centimeters
	FRotator SensorRotation; // Sensor rotation (yaw, pitch, roll) in degrees
	TArray<FVector> EmitterPositions; // Emitter positions array (x, y, z) in centimeters
	float MaxTraceDistance = 0; // Maximum trace distance in centimeters
	uint32 MaxBounces = 0; // Maximum number of bounces
	uint32 EmitterCount = 0; // Number of emitters
	bool EnableDirectPath = true; // Toggle for the direct LOS check between sensor and receiver
	TArray<float> DirectPathAzimuthAngles; // Direct path azimuth angles from sensor to receivers
	TArray<float> DirectPathElevationAngles; // Direct path elevation angles array to receivers
};


class SONOTRACEUE_API FSonoTrace
{
public:
	FSonoTrace();
	explicit FSonoTrace(const float SimulationRate, const bool RunOnTriggerOnly):
		RunRate(SimulationRate),
		RunOnTriggerOnly(RunOnTriggerOnly) {}

	void BeginRendering();
	void EndRendering();
	void UpdateParameters(const FSonoTraceParameters& InputParameters);	
	
	uint64 ExecutionCounter = 0;
	double CurrentTimestamp = -1;
	int32 RunState = 0; // 0: simulation ready to run (default), 1: simulation should run, 2: simulation has run
	
private:
	void Execute_RenderThread(FPostOpaqueRenderParameters& Parameters);
	static void BindSonoTraceCHSBindings(FRHICommandList& RHICmdList, const FViewInfo& View, FRHIRayTracingScene* RHIScene, FRHIUniformBuffer* SceneUniformBuffer, FRayTracingPipelineState* PipelineState);
	
	FDelegateHandle SonoTraceRenderDelegate;
	FSonoTraceParameters CachedParams;
	volatile bool bCachedParamsAreValid = false;
	FRDGBufferRef StructuredOutputBufferRef = nullptr;
	FRDGBufferRef SensorConfigurationBufferRef = nullptr;
	FRDGBufferRef AzimuthAnglesBufferRef = nullptr;
	FRDGBufferRef ElevationAnglesBufferRef = nullptr;
	double LastExecutionTime = -1;
	double RunRate = 30;
	bool RunOnTriggerOnly = true;
};