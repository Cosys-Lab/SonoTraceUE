// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details. 

#include "SonoTraceUEActor.h"
#include "RandomInterator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "../Private/ScenePrivate.h"
#include "SceneInterface.h"
#include "Engine/DataTable.h"
#include "SonoTrace.h"
#include "Math/UnrealMathUtility.h"
#include <string>

#include "GuidStructCustomization.h"
#include "ObjectDeliverer/Public/Protocol/ProtocolTcpIpClient.h"
#include "ObjectDeliverer/Public/Protocol/ProtocolTcpIpServer.h"
#include "ObjectDeliverer/Public/PacketRule/PacketRuleSizeBody.h"
#include "ObjectDeliverer/Public/PacketRule/PacketRuleNodivision.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/SceneUtilityFunctions.h"
#include "MeshCurvature.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Rendering/SkeletalMeshRenderData.h"

// Sets default values
ASonoTraceUEActor::ASonoTraceUEActor()
{
	PrimaryActorTick.bCanEverTick = true;
	
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void ASonoTraceUEActor::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(true);

	if (InterfaceSettings == nullptr)
	{
		InterfaceSettings = NewObject<USonoTraceUEInterfaceSettingsData>();
		UE_LOG(SonoTraceUE, Warning, TEXT("Interface Settings Data Asset is not set. Using default input settings."));
	}

	if ((EnableInterfaceEnableOverride && EnableInterface) || (!EnableInterfaceEnableOverride && InterfaceSettings->EnableInterface))
	{
		ObjectDelivererManager = NewObject<UObjectDelivererManager>();		
		ObjectDelivererManager->Connected.AddDynamic(this, &ASonoTraceUEActor::InterfaceOnConnect);
		ObjectDelivererManager->Disconnected.AddDynamic(this, &ASonoTraceUEActor::InterfaceOnDisconnect);
		const int32 InterfacePortSet = InterfaceSettings->InterfacePort;
		const FString InterfaceIPSet = InterfaceSettings->InterfaceIP;
		Utf8StringDeliveryBox = NewObject<UUtf8StringDeliveryBox>();
		Utf8StringDeliveryBox->Received.AddDynamic(this, &ASonoTraceUEActor::InterfaceOnReceiveString);
		ObjectDelivererManager->Start(UProtocolFactory::CreateProtocolTcpIpClient(InterfaceIPSet, InterfacePortSet, true),
						              UPacketRuleFactory::CreatePacketRuleNodivision(), Utf8StringDeliveryBox);
	}

	if (InputSettings == nullptr)
	{
		InputSettings = NewObject<USonoTraceUEInputSettingsData>();
		UE_LOG(SonoTraceUE, Warning, TEXT("Input Settings Data Asset is not set. Using default simulation settings."));
	}
	
	if(!InputSettings->EnableEmitterDirectivity)
	{
		if (InputSettings->DrawPointsColorMode==ESonoTraceUESimulationDrawColorModeEnum::EmitterDirectivity)
		{
			InputSettings->DrawPointsColorMode = ESonoTraceUESimulationDrawColorModeEnum::Static;
			UE_LOG(SonoTraceUE, Warning, TEXT("Emitter directivity color mode disabled because emitter directivity is not enabled. Setting color mode to static."));
		}
	}
	
	if (!InputSettings->DebugDisableInitialization)
	{
		if (InputSettings->EnableSpecularComponentCalculation || InputSettings->EnableDirectPathComponentCalculation || InputSettings->EnableDiffractionComponentCalculation)
		{
			InputSettings->EnableRaytracing = true;
		}

		if ((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation))
		{
			GeneratedSettings = GenerateInputSettings(InputSettings, &AssetToObjectTypeIndexSettings);
			CurrentEmitterSignalIndexes = GeneratedSettings.DefaultEmitterSignalIndexes;

			if (InputSettings->EnableDirectPathComponentCalculation)
			{
				DirectPathAzimuthAngles.Init(0, GeneratedSettings.FinalReceiverPositions.Num());
				DirectPathElevationAngles.Init(0, GeneratedSettings.FinalReceiverPositions.Num());
				DirectPathReceiverOutput.Init(TTuple<bool, FVector>(false, FVector()), GeneratedSettings.FinalReceiverPositions.Num());
			}else
			{
				DirectPathAzimuthAngles.Empty();
				DirectPathElevationAngles.Empty();
				DirectPathReceiverOutput.Empty();
			}
		}

		if (InputSettings->EnableRaytracing)
		{
			SonoTrace = FSonoTrace(InputSettings->SimulationRate, InputSettings->EnableRunSimulationOnlyOnTrigger);
	
			if (GPUReadback != nullptr)
			{
				UpdateShaderParameters();
			}else
			{
				GPUReadback = new FRHIGPUBufferReadback(TEXT("SonoTraceReadback"));
			}
		}
	
		RandomStream.Initialize(FPlatformTime::Cycles());

		UpdateTransformations();
	
		TranscurredTime = 0;
		Initialized = false;
	}
}

void ASonoTraceUEActor::GenerateAllInitialMeshData()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AddActor(*ActorItr, true, false);
	}
}

void ASonoTraceUEActor::UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable()
{
	TArray<int32> PersistentPrimitiveIndexes;
	ScenePrimitiveIndexToPersistentPrimitiveIndex.GenerateValueArray(PersistentPrimitiveIndexes);
	ScenePrimitiveIndexToPersistentPrimitiveIndex.Empty();	

	if (PersistentPrimitiveIndexes.Num() > 0)
	{
		FScene* RenderScene = GetWorld()->Scene->GetRenderScene();
		for (int32 PersistentPrimitiveIndex : PersistentPrimitiveIndexes)
		{
			FPersistentPrimitiveIndex CurrentPersistentPrimitiveIndex;
			CurrentPersistentPrimitiveIndex.Index = PersistentPrimitiveIndex;
			int32 ScenePrimitiveIndex = RenderScene->GetPrimitiveIndex(CurrentPersistentPrimitiveIndex);
			ScenePrimitiveIndexToPersistentPrimitiveIndex.Add(ScenePrimitiveIndex, PersistentPrimitiveIndex);
		}
	}
	UE_LOG(SonoTraceUE, Log, TEXT("Updated SPI to PPI table."));
}

bool ASonoTraceUEActor::AddActor(AActor* Actor, const bool OverrideInitialization, const bool UpdateTable)
{
	if (!Initialized && !OverrideInitialization)
		return false;
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
	FString ActorName = Actor->GetName();
	bool ReturnValue = true;
	for (UStaticMeshComponent* MeshComponent : StaticMeshComponents)
	{
		const bool CurrentSuccess = AddStaticMeshComponent(MeshComponent, ActorName, OverrideInitialization, false);
		if (!CurrentSuccess)
			ReturnValue = false;
	}
	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	Actor->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents,true);
	for (USkeletalMeshComponent* MeshComponent : SkeletalMeshComponents)
	{
		const bool CurrentSuccess = AddSkeletalMeshComponent(MeshComponent, ActorName, OverrideInitialization, false);
		if (!CurrentSuccess)
			ReturnValue = false;
	}
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return ReturnValue;
}

bool ASonoTraceUEActor::AddStaticMeshComponent(UStaticMeshComponent* MeshComponent, FString ObjectNamePrefix, const bool OverrideInitialization, const bool UpdateTable, const bool OverrideAddingToLoadList, const int32 PreviousAttempts)
{
	if (!Initialized && !OverrideInitialization)
		return false;
	if (MeshComponent && MeshComponent->GetStaticMesh())
	{
		FSonoTraceUEObjectSettingsStruct* ObjectSettings = &GeneratedSettings.ObjectSettings[0];
		int32 ObjectTypeIndex = 0;
		if (AssetToObjectTypeIndexSettings.Contains(MeshComponent->GetStaticMesh()))
		{
			ObjectTypeIndex = AssetToObjectTypeIndexSettings.FindChecked(MeshComponent->GetStaticMesh());
			ObjectSettings = &GeneratedSettings.ObjectSettings[ObjectTypeIndex];
		}
		if (MeshComponent->SceneProxy)
		{
			const int32 PersistentPrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetPersistentIndex().Index;
			const int32 ScenePrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetIndex();
			UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
			FName Label = FName(ObjectNamePrefix + TEXT("_") + MeshComponent->GetName() + TEXT("_") + StaticMesh->GetName());
			if (ScenePrimitiveIndex != -1)
			{
				if (PersistentPrimitiveIndexToMeshDataIndex.Contains(PersistentPrimitiveIndex))
				{
					UE_LOG(SonoTraceUE, Warning, TEXT("Object with PPI #%d, SPI #%d and label '%s' using StaticMesh '%s' and object type '%s (#%d)' was already added."),
						   PersistentPrimitiveIndex, ScenePrimitiveIndex, *Label.ToString(), *StaticMesh->GetName(), *ObjectSettings->Name.ToString(), ObjectTypeIndex);
					if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
					return false;
				}
				if (!StaticMeshToMeshDataIndex.Contains(StaticMesh)) // Only process unique meshes
				{					
					if (InputSettings->MeshDataGenerationTriangleMaximum > 0)
					{
						const int32 TriangleCount = GetMeshComponentTriangleCount(MeshComponent);
						if (TriangleCount > InputSettings->MeshDataGenerationTriangleMaximum)
						{
							FName MeshName = FName(StaticMesh->GetName());
							SkippedMeshesDueToTriangleCount.Add(MeshName, TriangleCount);
							UE_LOG(SonoTraceUE, Warning, TEXT("SKIPPED: StaticMesh '%s' with %d triangles exceeds the maximum set (%d). Mesh data will not be generated for this mesh."),
								   *StaticMesh->GetName(), TriangleCount, InputSettings->MeshDataGenerationTriangleMaximum);
							PersistentPrimitiveIndexToPrimitiveComponent.Add(PersistentPrimitiveIndex, MeshComponent);
							ScenePrimitiveIndexToPersistentPrimitiveIndex.Add(ScenePrimitiveIndex, PersistentPrimitiveIndex);
							PersistentPrimitiveIndexToLabelsAndObjectTypes.Add(PersistentPrimitiveIndex, TTuple<FName, int32>(Label, ObjectTypeIndex));
							if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
							return true;
						}
					}
					
					FSonoTraceUEMeshDataStruct NewMeshData;
					CalculateMeshCurvature(MeshComponent, NewMeshData, InputSettings->CurvatureScale, InputSettings->EnableCurvatureTriangleSizeBasedScaler,
						InputSettings->CurvatureScalerMinimumEffect, InputSettings->CurvatureScalerMaximumEffect, InputSettings->CurvatureScalerLowerTriangleSizeThreshold, InputSettings->CurvatureScalerUpperTriangleSizeThreshold, InputSettings->DiffractionTriangleSizeThreshold);
					GenerateBRDFAndMaterial(ObjectSettings, &NewMeshData);
					if (ObjectSettings->DrawDebugFirstOccurrence)
						DrawMeshDebug(MeshComponent, NewMeshData);
					const int32 MeshDataIndex = MeshData.Num();
					MeshData.Add(NewMeshData);
					PersistentPrimitiveIndexToMeshDataIndex.Add(PersistentPrimitiveIndex, MeshDataIndex);
					StaticMeshToMeshDataIndex.Add(StaticMesh, MeshDataIndex);
					StaticMeshCounter.Add(StaticMesh, 1);
				}else
				{
					PersistentPrimitiveIndexToMeshDataIndex.Add(PersistentPrimitiveIndex, StaticMeshToMeshDataIndex.FindChecked(StaticMesh));
					const int32 CurrentCount = StaticMeshCounter.FindChecked(StaticMesh);
					StaticMeshCounter.Add(StaticMesh, CurrentCount + 1);
				}
				const int32 TriangleCount = GetMeshComponentTriangleCount(MeshComponent);
				PersistentPrimitiveIndexToPrimitiveComponent.Add(PersistentPrimitiveIndex, MeshComponent);
				ScenePrimitiveIndexToPersistentPrimitiveIndex.Add(ScenePrimitiveIndex, PersistentPrimitiveIndex);
				UE_LOG(SonoTraceUE, Log, TEXT("Added object with PPI #%d, SPI #%d and label '%s' using StaticMesh '%s' (%d triangles) and object type '%s (#%d)'."),
					   PersistentPrimitiveIndex, ScenePrimitiveIndex, *Label.ToString(), *StaticMesh->GetName(), TriangleCount, *ObjectSettings->Name.ToString(), ObjectTypeIndex);
				PersistentPrimitiveIndexToLabelsAndObjectTypes.Add(PersistentPrimitiveIndex, TTuple<FName, int32>(Label, ObjectTypeIndex));
				if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
				return true;
			}
			if (OverrideAddingToLoadList)
			{
				UE_LOG(SonoTraceUE, Warning, TEXT("Object with label '%s' using StaticMesh '%s' and object type '%s (#%d)' is not spawned yet so could not add."),
				       *Label.ToString(), *StaticMesh->GetName(), *ObjectSettings->Name.ToString(), ObjectTypeIndex);
			}else
			{
				StaticMeshComponentsToLoad.Add(TTuple<FString, UStaticMeshComponent*, int32>(ObjectNamePrefix, MeshComponent, PreviousAttempts + 1));
			}
			if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
			return false;
		}	
	}
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return false;
}

bool ASonoTraceUEActor::AddSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, FString ObjectNamePrefix, const bool OverrideInitialization, const bool UpdateTable, const bool OverrideAddingToLoadList, const int32 PreviousAttempts)
{
	if (!Initialized && !OverrideInitialization)
		return false;
	if (MeshComponent && MeshComponent->GetSkeletalMeshAsset())
    {
    	FSonoTraceUEObjectSettingsStruct* ObjectSettings = &GeneratedSettings.ObjectSettings[0];
    	int32 ObjectTypeIndex = 0;
    	if (AssetToObjectTypeIndexSettings.Contains(MeshComponent->GetSkeletalMeshAsset()))
    	{
    		ObjectTypeIndex = AssetToObjectTypeIndexSettings.FindChecked(MeshComponent->GetSkeletalMeshAsset());
    		ObjectSettings = &GeneratedSettings.ObjectSettings[ObjectTypeIndex];
    	}
    	if (MeshComponent->SceneProxy)
    	{
    		const int32 PersistentPrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetPersistentIndex().Index;
    		const int32 ScenePrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetIndex();
    		USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
    		FName Label = FName(ObjectNamePrefix + TEXT("_") + MeshComponent->GetName() + TEXT("_") + SkeletalMesh->GetName());
    		if (ScenePrimitiveIndex != -1)
    		{
    			if (PersistentPrimitiveIndexToMeshDataIndex.Contains(PersistentPrimitiveIndex))
    			{
				    UE_LOG(SonoTraceUE, Warning, TEXT("Object with PPI #%d, SPI #%d and label '%s' using SkeletalMesh '%s' and object type '%s (#%d)' was already added."),
				           PersistentPrimitiveIndex, ScenePrimitiveIndex, *Label.ToString(), *SkeletalMesh->GetName(), *ObjectSettings->Name.ToString(), ObjectTypeIndex);
				    if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
				    return false;
			    }
			    if (!SkeletalMeshToMeshDataIndex.Contains(SkeletalMesh)) 
			    {
				    if (InputSettings->MeshDataGenerationTriangleMaximum > 0)
				    {
					    const int32 TriangleCount = GetMeshComponentTriangleCount(MeshComponent);
					    if (TriangleCount > InputSettings->MeshDataGenerationTriangleMaximum)
					    {
						    FName MeshName = FName(SkeletalMesh->GetName());
						    SkippedMeshesDueToTriangleCount.Add(MeshName, TriangleCount);
						    UE_LOG(SonoTraceUE, Warning, TEXT("SKIPPED: SkeletalMesh '%s' with %d triangles exceeds the maximum set (%d). Mesh data will not be generated for this mesh."),
							       *SkeletalMesh->GetName(), TriangleCount, InputSettings->MeshDataGenerationTriangleMaximum);
						    PersistentPrimitiveIndexToPrimitiveComponent.Add(PersistentPrimitiveIndex, MeshComponent);
						    PersistentPrimitiveIndexToLabelsAndObjectTypes.Add(PersistentPrimitiveIndex, TTuple<FName, int32>(Label, ObjectTypeIndex));
						    if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
						    return true;
					    }
				    }				    
				    FSonoTraceUEMeshDataStruct NewMeshData;
			    	CalculateMeshCurvature(MeshComponent, NewMeshData, InputSettings->CurvatureScale, InputSettings->EnableCurvatureTriangleSizeBasedScaler,
						InputSettings->CurvatureScalerMinimumEffect, InputSettings->CurvatureScalerMaximumEffect, InputSettings->CurvatureScalerLowerTriangleSizeThreshold, InputSettings->CurvatureScalerUpperTriangleSizeThreshold, InputSettings->DiffractionTriangleSizeThreshold);
			    	GenerateBRDFAndMaterial(ObjectSettings, &NewMeshData);
			    	if (ObjectSettings->DrawDebugFirstOccurrence)
			    		DrawMeshDebug(MeshComponent, NewMeshData);
				    const int32 MeshDataIndex = MeshData.Num();
				    MeshData.Add(NewMeshData);
				    PersistentPrimitiveIndexToMeshDataIndex.Add(PersistentPrimitiveIndex, MeshDataIndex);
				    SkeletalMeshToMeshDataIndex.Add(SkeletalMesh, MeshDataIndex);
				    SkeletalMeshCounter.Add(SkeletalMesh, 1);
			    }else
			    {
				    PersistentPrimitiveIndexToMeshDataIndex.Add(PersistentPrimitiveIndex, SkeletalMeshToMeshDataIndex.FindChecked(SkeletalMesh));
				    const int32 CurrentCount = SkeletalMeshCounter.FindChecked(SkeletalMesh);
				    SkeletalMeshCounter.Add(SkeletalMesh, CurrentCount + 1);
			    }
    			const int32 TriangleCount = GetMeshComponentTriangleCount(MeshComponent);
			    PersistentPrimitiveIndexToPrimitiveComponent.Add(PersistentPrimitiveIndex, MeshComponent);
			    UE_LOG(SonoTraceUE, Log, TEXT("Added object with PPI #%d, SPI #%d and label '%s' using SkeletalMesh '%s'(%d triangles) and object type '%s (#%d)'."),
			           PersistentPrimitiveIndex, ScenePrimitiveIndex, *Label.ToString(), *SkeletalMesh->GetName(), TriangleCount, *ObjectSettings->Name.ToString(), ObjectTypeIndex);
			    PersistentPrimitiveIndexToLabelsAndObjectTypes.Add(PersistentPrimitiveIndex, TTuple<FName, int32>(Label, ObjectTypeIndex));
			    if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
			    return true;
		    }
		    if (OverrideAddingToLoadList)
		    {
			    UE_LOG(SonoTraceUE, Warning, TEXT("Object with label '%s' using SkeletalMesh '%s' and object type '%s (#%d)' is not spawned yet so could not add."),
			           *Label.ToString(), *SkeletalMesh->GetName(), *ObjectSettings->Name.ToString(), ObjectTypeIndex);
		    }else
		    {
			    SkeletalMeshComponentsToLoad.Add(TTuple<FString, USkeletalMeshComponent*, int32>(ObjectNamePrefix, MeshComponent, PreviousAttempts + 1));
		    }
		    if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
		    return false;
	    }	
    }
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return false;
}

bool ASonoTraceUEActor::RemoveActor(AActor* Actor, const bool UpdateTable)
{
	if (!Initialized)
		return false;
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
	bool ReturnValue = true;
	for (UStaticMeshComponent* MeshComponent : StaticMeshComponents)
	{
		const bool CurrentSuccess = RemoveStaticMeshComponent(MeshComponent, false);
		if (!CurrentSuccess)
			ReturnValue = false;
	}
	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	Actor->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents,true);
	for (USkeletalMeshComponent* MeshComponent : SkeletalMeshComponents)
	{
		const bool CurrentSuccess = RemoveSkeletalMeshComponent(MeshComponent, false);
		if (!CurrentSuccess)
			ReturnValue = false;
	}
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return ReturnValue;
}

bool ASonoTraceUEActor::RemoveStaticMeshComponent(UStaticMeshComponent* MeshComponent, const bool UpdateTable)
{
	if (!Initialized)
		return false;
	UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	if (MeshComponent && MeshComponent->GetStaticMesh())
	{
		if (MeshComponent->SceneProxy)
		{
			const int32 PersistentPrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetPersistentIndex().Index;
			const int32 ScenePrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetIndex();
			const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
			if (StaticMeshCounter.Contains(StaticMesh) && PersistentPrimitiveIndexToLabelsAndObjectTypes.Contains(PersistentPrimitiveIndex))
			{
				TTuple<FName, int32> ObjectNameAndTypeIndex = PersistentPrimitiveIndexToLabelsAndObjectTypes.FindChecked(PersistentPrimitiveIndex);
				const FName ObjectName = ObjectNameAndTypeIndex.Get<0>();
				PersistentPrimitiveIndexToMeshDataIndex.Remove(PersistentPrimitiveIndex);
				PersistentPrimitiveIndexToPrimitiveComponent.Remove(PersistentPrimitiveIndex);
				PersistentPrimitiveIndexToLabelsAndObjectTypes.Remove(PersistentPrimitiveIndex);
				ScenePrimitiveIndexToPersistentPrimitiveIndex.Remove(ScenePrimitiveIndex);
				UE_LOG(SonoTraceUE, Log, TEXT("Removed object with PPI #%d, SPI #%d, and label '%s' using StaticMesh '%s'."),
	                   PersistentPrimitiveIndex, ScenePrimitiveIndex, *ObjectName.ToString(), *StaticMesh->GetName());
				if (StaticMeshCounter.FindChecked(StaticMesh) == 1)
				{
					StaticMeshCounter.Remove(StaticMesh);
					MeshData.RemoveAt(StaticMeshToMeshDataIndex.FindChecked(StaticMesh));
					StaticMeshToMeshDataIndex.Remove(StaticMesh);
					UE_LOG(SonoTraceUE, Log, TEXT("Removed StaticMesh '%s' mesh data."), *StaticMesh->GetName());
				}
				if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
				return true;
			}	
		}
	}
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return false;
}

bool ASonoTraceUEActor::RemoveSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, const bool UpdateTable)
{
	if (!Initialized)
		return false;
	if (MeshComponent && MeshComponent->GetSkeletalMeshAsset())
	{
		if (MeshComponent->SceneProxy)
		{
			const int32 PersistentPrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetPersistentIndex().Index;
			const int32 ScenePrimitiveIndex = MeshComponent->SceneProxy->GetPrimitiveSceneInfo()->GetIndex();
			const USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
			if (SkeletalMeshCounter.Contains(SkeletalMesh) && PersistentPrimitiveIndexToLabelsAndObjectTypes.Contains(PersistentPrimitiveIndex))
			{
				TTuple<FName, int32> ObjectNameAndTypeIndex = PersistentPrimitiveIndexToLabelsAndObjectTypes.FindChecked(PersistentPrimitiveIndex);
				const FName ObjectName = ObjectNameAndTypeIndex.Get<0>();
				PersistentPrimitiveIndexToMeshDataIndex.Remove(PersistentPrimitiveIndex);
				PersistentPrimitiveIndexToPrimitiveComponent.Remove(PersistentPrimitiveIndex);
				PersistentPrimitiveIndexToLabelsAndObjectTypes.Remove(PersistentPrimitiveIndex);
				ScenePrimitiveIndexToPersistentPrimitiveIndex.Remove(ScenePrimitiveIndex);
				UE_LOG(SonoTraceUE, Log, TEXT("Removed object with PPI #%d, SPI #%d and label '%s' using SkeletalMesh '%s'."),
	                   PersistentPrimitiveIndex, ScenePrimitiveIndex, *ObjectName.ToString(), *SkeletalMesh->GetName());
				if (SkeletalMeshCounter.FindChecked(SkeletalMesh) == 1)
				{
					SkeletalMeshCounter.Remove(SkeletalMesh);
					MeshData.RemoveAt(SkeletalMeshToMeshDataIndex.FindChecked(SkeletalMesh));
					SkeletalMeshToMeshDataIndex.Remove(SkeletalMesh);
					UE_LOG(SonoTraceUE, Log, TEXT("Removed SkeletalMesh '%s' mesh data."), *SkeletalMesh->GetName());
				}
				if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
				return true;
			}	
		}
	}
	if (UpdateTable) UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	return false;
}

void ASonoTraceUEActor::UpdateTransformations()
{
	SensorLocation = GetActorLocation();
	SensorRotation = GetActorRotation();

	if (const AActor* OwnerActor = GetAttachParentActor()) {
		OwnerLocation = OwnerActor->GetActorLocation();
		OwnerRotation = OwnerActor->GetActorRotation();
		FTransform OwnerTransform = OwnerActor->GetActorTransform();
		FTransform SensorTransform = GetActorTransform();
		FTransform RelativeTransform = SensorTransform.GetRelativeTransform(OwnerTransform);
		SensorToOwnerTranslation = RelativeTransform.GetTranslation();
		SensorToOwnerRotation = RelativeTransform.Rotator();
	}else{
		OwnerLocation = FVector::ZeroVector;
		OwnerRotation = FRotator::ZeroRotator;
		SensorToOwnerTranslation = FVector::ZeroVector;
		SensorToOwnerRotation = FRotator::ZeroRotator;
	}
	
	const FTransform ActorTransform(SensorRotation, SensorLocation);
	StartingActorTransform = ActorTransform;
	// Calculate emitter poses
	if (EmitterPoses.IsEmpty())
		EmitterPoses.Init(FTransform(), GeneratedSettings.LoadedEmitterPositions.Num());	
	for (int i = 0; i < GeneratedSettings.LoadedEmitterPositions.Num(); i++)
	{
		const FVector EmitterFinalPosition = ActorTransform.TransformPosition(GeneratedSettings.FinalEmitterPositions[i]);
		EmitterPoses[i] = FTransform(SensorRotation, EmitterFinalPosition);
	}

	// Calculate receiver poses
	if (ReceiverPoses.IsEmpty())
	{
		ReceiverPoses.Init(FTransform(), GeneratedSettings.FinalReceiverPositions.Num());
		if (InputSettings->EnableStaticReceivers)
		{
			for (int i = 0; i < GeneratedSettings.FinalReceiverPositions.Num(); i++)
			{
				if (InputSettings->EnableUseWorldCoordinatesReceivers)
				{
					ReceiverPoses[i] = FTransform(FRotator(), GeneratedSettings.FinalReceiverPositions[i]);
				}else
				{
					const FVector ReceiverFinalPosition = StartingActorTransform.TransformPosition(GeneratedSettings.FinalReceiverPositions[i]);
					ReceiverPoses[i] = FTransform(SensorRotation, ReceiverFinalPosition);
				}
			}
		}
	}
	if (!InputSettings->EnableStaticReceivers)
	{
		for (int i = 0; i < GeneratedSettings.FinalReceiverPositions.Num(); i++)
		{
			const FVector ReceiverFinalPosition = ActorTransform.TransformPosition(GeneratedSettings.FinalReceiverPositions[i]);
			ReceiverPoses[i] = FTransform(SensorRotation, ReceiverFinalPosition);
		}
	}	

	// Calculate direction angles from sensor to receivers
	if (InputSettings->EnableDirectPathComponentCalculation)
	{
		for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
		{
			const FTransform& ReceiverTransform = ReceiverPoses[ReceiverIndex];
			FVector DirectionVector = ReceiverTransform.GetLocation() - SensorLocation;
			DirectionVector.Normalize();
			FVector LocalDirectionVector = SensorRotation.UnrotateVector(DirectionVector);
			DirectPathAzimuthAngles[ReceiverIndex] = FMath::Atan2(LocalDirectionVector.Y, LocalDirectionVector.X);
			DirectPathElevationAngles[ReceiverIndex] = FMath::Atan2(LocalDirectionVector.Z, FVector(LocalDirectionVector.X, LocalDirectionVector.Y, 0).Size());
		}
	}
}

bool ASonoTraceUEActor::SetNewEmitterPositions(const TArray<int32>& EmitterIndexes, const TArray<FVector>& NewEmitterPositions, const bool RelativeTransform, const bool ReApplyOffset)
{
	bool bSuccess = true;
	int32 ReceiverCounter = 0;
    for(int32 EmitterIndex : EmitterIndexes)
    {
	    if (EmitterIndex < 0 || EmitterIndex >= GeneratedSettings.FinalEmitterPositions.Num())
	    {
	    	UE_LOG(SonoTraceUE, Error, TEXT("Emitter index #%i is not valid, cannot change emitter position for this index."), EmitterIndex);
	    	bSuccess = false;
	    }else
	    {
	    	if (RelativeTransform)
	    	{
	    		GeneratedSettings.FinalEmitterPositions[EmitterIndex] = GeneratedSettings.FinalEmitterPositions[EmitterIndex] + NewEmitterPositions[ReceiverCounter];
	    	}else
	    	{
	    		if(ReApplyOffset)
	    		{
	    			GeneratedSettings.FinalEmitterPositions[EmitterIndex] = NewEmitterPositions[ReceiverCounter] + InputSettings->EmitterPositionsOffset;
	    		}else
	    		{
	    			GeneratedSettings.FinalEmitterPositions[EmitterIndex] = NewEmitterPositions[ReceiverCounter];
	    		}    		
	    	}
	    }
    	ReceiverCounter++;
    }  
	return bSuccess;
}

bool ASonoTraceUEActor::SetNewReceiverPositions(const TArray<int32>& ReceiverIndexes, const TArray<FVector>& NewReceiverPositions, const bool RelativeTransform, const bool ReApplyOffset)
{
	bool bSuccess = true;
	int32 ReceiverCounter = 0;
	for(int32 ReceiverIndex : ReceiverIndexes)
	{
		if (ReceiverIndex < 0 || ReceiverIndex >= GeneratedSettings.FinalReceiverPositions.Num())
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Receiver index #%i is not valid, cannot change receiver position for this index."), ReceiverIndex);
			bSuccess = false;
		}else
		{
			if (RelativeTransform)
			{
				GeneratedSettings.FinalReceiverPositions[ReceiverIndex] = GeneratedSettings.FinalReceiverPositions[ReceiverIndex] + NewReceiverPositions[ReceiverCounter];
			}else
			{
				if(ReApplyOffset)
				{
					GeneratedSettings.FinalReceiverPositions[ReceiverIndex] = NewReceiverPositions[ReceiverCounter] + InputSettings->ReceiverPositionsOffset;
				}else
				{
					GeneratedSettings.FinalReceiverPositions[ReceiverIndex] = NewReceiverPositions[ReceiverCounter];
				}
			}
			if (InputSettings->EnableStaticReceivers)
			{			
				if (InputSettings->EnableUseWorldCoordinatesReceivers)
				{
					ReceiverPoses[ReceiverIndex] = FTransform(FRotator(), GeneratedSettings.FinalReceiverPositions[ReceiverIndex]);
				}else
				{
					const FVector ReceiverFinalPosition = StartingActorTransform.TransformPosition(GeneratedSettings.FinalReceiverPositions[ReceiverIndex]);
					ReceiverPoses[ReceiverIndex] = FTransform(SensorRotation, ReceiverFinalPosition);
				}
			}	
		}
		ReceiverCounter++;
	}  
	return bSuccess;

}

bool ASonoTraceUEActor::SetNewSensorRelativeTransform(const FVector& NewSensorToOwnerTranslation, const FRotator& NewSensorToOwnerRotator)
{
	
	if (const AActor* ParentActor = GetAttachParentActor()) {
		TArray<UChildActorComponent*> ChildActorComponents;
		ParentActor->GetComponents<UChildActorComponent>(ChildActorComponents);

		for (UChildActorComponent* ChildComponent : ChildActorComponents)
		{
			if (ChildComponent && ChildComponent->GetChildActor() == this)
			{				
				ChildComponent->SetRelativeTransform(FTransform(NewSensorToOwnerRotator, NewSensorToOwnerTranslation), false, nullptr, ETeleportType::None);
				return true;
			}
		}
	}
	UE_LOG(SonoTraceUE, Error, TEXT("Could not set new sensor relative transform. Is there a parent actor?"));
	return false;
}


bool ASonoTraceUEActor::SetNewSensorWorldTransform(const FVector& NewSensorTranslation, const FRotator& NewSensorRotator, const ETeleportType Teleport)
{
	return SetActorTransform(FTransform(NewSensorRotator, NewSensorTranslation), false, nullptr, Teleport);
}

bool ASonoTraceUEActor::SetNewSensorOwnerWorldTransform(const FVector& NewOwnerTranslation, const FRotator& NewOwnerRotator, const ETeleportType Teleport)
{
	if (AActor* OwnerActor = GetAttachParentActor()) {
		return OwnerActor->SetActorTransform(FTransform(NewOwnerRotator, NewOwnerTranslation), false, nullptr, Teleport);
	}
	UE_LOG(SonoTraceUE, Error, TEXT("Could not set new owner world transform. Is there a parent actor?"));
	return false;
}

void ASonoTraceUEActor::UpdateInterface()
{
	if (InterfaceReadyForSettings && !InterfaceSettingsMessageAnnouncementSent && Initialized)
	{
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_settings\n"));
		InterfaceSettingsMessageAnnouncementSent = true;
	}	
	if (InterfaceSettingsMessageAnnouncementAck && !InterfaceSettingsDataSent && Initialized)
	{
		SendInterfaceSettings();
	}
	if (InterfaceReadyForMessages && InterfaceDataMessageAnnouncementSent){
		if (InterfaceDataMessageAnnouncementAck)
		{
			SendInterfaceData();
		}
	}
	if (InterfaceReadyForMessages && InterfaceMeasurementMessageAnnouncementSent){
		if (InterfaceMeasurementMessageAnnouncementAck)
		{
			SendInterfaceMeasurement();
		}
	}
}

void ASonoTraceUEActor::SendInterfaceSettings()
{
	const double CurrentTime = FPlatformTime::Seconds();
	TArray<uint8> DataToSend;
	DataToSend.Empty();
	
	// Input settings

	// Offsets
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EmitterPositionsOffset.X), sizeof(InputSettings->EmitterPositionsOffset.X));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EmitterPositionsOffset.Y), sizeof(InputSettings->EmitterPositionsOffset.Y));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EmitterPositionsOffset.Z), sizeof(InputSettings->EmitterPositionsOffset.Z));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->ReceiverPositionsOffset.X), sizeof(InputSettings->ReceiverPositionsOffset.X));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->ReceiverPositionsOffset.Y), sizeof(InputSettings->ReceiverPositionsOffset.Y));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->ReceiverPositionsOffset.Z), sizeof(InputSettings->ReceiverPositionsOffset.Z));
    
    // Directivity settings
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableEmitterDirectivity), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableReceiverDirectivity), sizeof(bool));

	// Receiver settings
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableStaticReceivers), sizeof(bool));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableUseWorldCoordinatesReceivers), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableEmitterPatternSimulation), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EmitterPatternRadius), sizeof(InputSettings->EmitterPatternRadius));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EmitterPatternSpacing), sizeof(InputSettings->EmitterPatternSpacing));

    // Simulation settings
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableSimulation), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableRaytracing), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableSpecularComponentCalculation), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableDiffractionComponentCalculation), sizeof(bool));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableDirectPathComponentCalculation), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableRunSimulationOnlyOnTrigger), sizeof(bool));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->PointsInSensorFrame), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SimulationRate), sizeof(InputSettings->SimulationRate));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->NumberOfSimFrequencies), sizeof(InputSettings->NumberOfSimFrequencies));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->MinimumSimFrequency), sizeof(InputSettings->MinimumSimFrequency));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->MaximumSimFrequency), sizeof(InputSettings->MaximumSimFrequency));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SampleRate), sizeof(InputSettings->SampleRate));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SpeedOfSound), sizeof(InputSettings->SpeedOfSound));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->DirectPathStrength), sizeof(InputSettings->DirectPathStrength));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SpecularMinimumStrength), sizeof(InputSettings->SpecularMinimumStrength));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->DiffractionMinimumStrength), sizeof(InputSettings->DiffractionMinimumStrength));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->DiffractionTriangleSizeThreshold), sizeof(InputSettings->DiffractionTriangleSizeThreshold));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->DiffractionSimDivisionFactor), sizeof(InputSettings->DiffractionSimDivisionFactor));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableDiffractionLineOfSightRequired), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableDiffractionForDynamicObjects), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableSpecularSimulationOnlyOnLastHits), sizeof(bool));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->MeshDataGenerationAttempts), sizeof(InputSettings->MeshDataGenerationAttempts));

	// Curvature settings
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->CurvatureScale), sizeof(InputSettings->CurvatureScale));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->EnableCurvatureTriangleSizeBasedScaler), sizeof(bool));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->CurvatureScalerMinimumEffect), sizeof(InputSettings->CurvatureScalerMinimumEffect));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->CurvatureScalerMaximumEffect), sizeof(InputSettings->CurvatureScalerMaximumEffect));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->CurvatureScalerLowerTriangleSizeThreshold), sizeof(InputSettings->CurvatureScalerLowerTriangleSizeThreshold));
	DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->CurvatureScalerUpperTriangleSizeThreshold), sizeof(InputSettings->CurvatureScalerUpperTriangleSizeThreshold));

    // Sensor settings
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SensorLowerAzimuthLimit), sizeof(InputSettings->SensorLowerAzimuthLimit));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SensorUpperAzimuthLimit), sizeof(InputSettings->SensorUpperAzimuthLimit));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SensorLowerElevationLimit), sizeof(InputSettings->SensorLowerElevationLimit));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->SensorUpperElevationLimit), sizeof(InputSettings->SensorUpperElevationLimit));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->NumberOfInitialRays), sizeof(InputSettings->NumberOfInitialRays));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->MaximumRayDistance), sizeof(InputSettings->MaximumRayDistance));
    DataToSend.Append(reinterpret_cast<const uint8*>(&InputSettings->MaximumBounces), sizeof(InputSettings->MaximumBounces));

    // Generated input settings

	// Ray angles
	int32 RayCount = GeneratedSettings.AzimuthAngles.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&RayCount), sizeof(int32));
    for (const float Azimuth : GeneratedSettings.AzimuthAngles)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&Azimuth), sizeof(Azimuth));
    }
    for (float Elevation : GeneratedSettings.ElevationAngles)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&Elevation), sizeof(Elevation));
    }

	// Emitters and receiver positions
	int32 EmitterCount = GeneratedSettings.LoadedEmitterPositions.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&EmitterCount), sizeof(int32));
    for (const FVector& LoadedEmitter : GeneratedSettings.LoadedEmitterPositions)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedEmitter.X), sizeof(LoadedEmitter.X));
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedEmitter.Y), sizeof(LoadedEmitter.Y));
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedEmitter.Z), sizeof(LoadedEmitter.Z));
    }
    for (const FVector& FinalEmitter : GeneratedSettings.FinalEmitterPositions)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalEmitter.X), sizeof(FinalEmitter.X));
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalEmitter.Y), sizeof(FinalEmitter.Y));
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalEmitter.Z), sizeof(FinalEmitter.Z));
    }
	for (float FinalEmitterDirectivity : GeneratedSettings.FinalEmitterDirectivities)
	{
		DataToSend.Append(reinterpret_cast<const uint8*>(&FinalEmitterDirectivity), sizeof(FinalEmitterDirectivity));
	}	
	int32 LoadedReceiverCount = GeneratedSettings.LoadedReceiverPositions.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&LoadedReceiverCount), sizeof(int32));
    for (const FVector& LoadedReceiver : GeneratedSettings.LoadedReceiverPositions)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedReceiver.X), sizeof(LoadedReceiver.X));
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedReceiver.Y), sizeof(LoadedReceiver.Y));
        DataToSend.Append(reinterpret_cast<const uint8*>(&LoadedReceiver.Z), sizeof(LoadedReceiver.Z));
    }
	int32 FinalReceiverCount = GeneratedSettings.FinalReceiverPositions.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&FinalReceiverCount), sizeof(int32));
    for (const FVector& FinalReceiver : GeneratedSettings.FinalReceiverPositions)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalReceiver.X), sizeof(FinalReceiver.X));
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalReceiver.Y), sizeof(FinalReceiver.Y));
        DataToSend.Append(reinterpret_cast<const uint8*>(&FinalReceiver.Z), sizeof(FinalReceiver.Z));
    }
	for (float FinalReceiverDirectivity : GeneratedSettings.FinalReceiverDirectivities)
	{
		DataToSend.Append(reinterpret_cast<const uint8*>(&FinalReceiverDirectivity), sizeof(FinalReceiverDirectivity));
	}	

    // Object settings
	int32 ObjectSettingsCount = GeneratedSettings.ObjectSettings.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&ObjectSettingsCount), sizeof(int32));
    for (FSonoTraceUEObjectSettingsStruct& ObjectSetting : GeneratedSettings.ObjectSettings)
    {
    	TArray<uint8> SerializedObjectSetting = SerializeObjectSettingsStruct(&ObjectSetting);
    	int32 ObjectSettingSize = SerializedObjectSetting.Num();
    	DataToSend.Append(SerializedObjectSetting.GetData(), ObjectSettingSize);
    }
	
    // Frequencies
    for (const float Frequency : GeneratedSettings.Frequencies)
    {
        DataToSend.Append(reinterpret_cast<const uint8*>(&Frequency), sizeof(Frequency));
    }

	// Emitter signals
	int32 EmitterSignalsCount = GeneratedSettings.EmitterSignals.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&EmitterSignalsCount), sizeof(int32));
	for (TArray<float>& EmitterSignal : GeneratedSettings.EmitterSignals)
	{
		int32 EmitterSignalsLength = EmitterSignal.Num();
		DataToSend.Append(reinterpret_cast<uint8*>(&EmitterSignalsLength), sizeof(int32));
		for (const float EmitterValue : EmitterSignal)
		{
			DataToSend.Append(reinterpret_cast<const uint8*>(&EmitterValue), sizeof(EmitterValue));
		}	
	}

	// Default emitter signal indexes
	for (const int32& EmitterSignalIndex : GeneratedSettings.DefaultEmitterSignalIndexes)
	{
		DataToSend.Append(reinterpret_cast<const uint8*>(&EmitterSignalIndex), sizeof(int32));
	}
	
	DataToSend.Shrink();
	TArray<uint8> DataSizeToSend;
	DataSizeToSend.Empty();
	int32 DataSize = DataToSend.Num();
	DataSizeToSend.Append(reinterpret_cast<uint8*>(&DataSize), sizeof(int32));
	DataSizeToSend.Shrink();
	ObjectDelivererManager->Send(DataSizeToSend);
	ObjectDelivererManager->Send(DataToSend);
	UE_LOG(SonoTraceUE, Log, TEXT("Sent settings over interface."));
	if (InputSettings->EnableDebugLogExecutionTimes)
		UE_LOG(SonoTraceUE, Log, TEXT("Interface settings message generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
	InterfaceSettingsDataSent = true;
}

void ASonoTraceUEActor::SendInterfaceData()
{
	const double CurrentTime = FPlatformTime::Seconds();
	FSonoTraceUEDataMessage SonoTraceUEDataToSend = InterfaceDataMessageDataBuffer[0];
	InterfaceDataMessageDataBuffer.RemoveAt(0);
	TArray<uint8> DataToSend;
	DataToSend.Empty();

	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEDataToSend.Type), sizeof(int32));

	int32 OrderCount = SonoTraceUEDataToSend.Order.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&OrderCount), sizeof(int32));
	for (int OrderIndex = 0; OrderIndex < OrderCount; OrderIndex++)
	{
		int32 CurrentOrder = SonoTraceUEDataToSend.Order[OrderIndex];
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentOrder), sizeof(int32));
	}

	int32 StringCount = SonoTraceUEDataToSend.Strings.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&StringCount), sizeof(int32));
	for (int StringIndex = 0; StringIndex < StringCount; StringIndex++)
	{
		TArray<uint8> StringByteArray;
		FMemoryWriter Writer(StringByteArray, true);	
		FString CurrentString = SonoTraceUEDataToSend.Strings[StringIndex];		
		const FTCHARToUTF8 UTF8StringConverter(*CurrentString);
		int StringLength = UTF8StringConverter.Length();
		Writer << StringLength;
		Writer.Serialize((void*)UTF8StringConverter.Get(), UTF8StringConverter.Length());
		int32 StringSize = StringByteArray.Num();
		DataToSend.Append(StringByteArray.GetData(), StringSize);
	}

	int32 IntegerCount = SonoTraceUEDataToSend.Integers.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&IntegerCount), sizeof(int32));
	for (int IntegerIndex = 0; IntegerIndex < IntegerCount; IntegerIndex++)
	{
		int32 CurrentInteger = SonoTraceUEDataToSend.Integers[IntegerIndex];
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentInteger), sizeof(int32));
	}

	int32 FloatCount = SonoTraceUEDataToSend.Floats.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&FloatCount), sizeof(int32));
	for (int FloatIndex = 0; FloatIndex < FloatCount; FloatIndex++)
	{
		float CurrentFloat = SonoTraceUEDataToSend.Floats[FloatIndex];
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentFloat), sizeof(float));
	}		

	DataToSend.Shrink();
	TArray<uint8> DataSizeToSend;
	DataSizeToSend.Empty();
	int32 DataSize = DataToSend.Num();
	DataSizeToSend.Append(reinterpret_cast<uint8*>(&DataSize), sizeof(int32));
	DataSizeToSend.Shrink();
	ObjectDelivererManager->Send(DataSizeToSend);
	ObjectDelivererManager->Send(DataToSend);
	InterfaceDataMessageAnnouncementAck = false;
	UE_LOG(SonoTraceUE, Log, TEXT("Sent data message of type #%i. Queue size: %i."), SonoTraceUEDataToSend.Type, InterfaceDataMessageDataBuffer.Num());
	if (InputSettings->EnableDebugLogExecutionTimes)
		UE_LOG(SonoTraceUE, Log, TEXT("Interface data message generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
	if (!InterfaceDataMessageDataBuffer.IsEmpty())
	{
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_data\n"));
	}else
	{
		InterfaceDataMessageAnnouncementSent = false;
	}
}

void ASonoTraceUEActor::SendInterfaceMeasurement()
{
	const double CurrentTime = FPlatformTime::Seconds();
	FSonoTraceUEOutputStruct SonoTraceUEOutputToSend = InterfaceMeasurementDataBuffer[0];
	InterfaceMeasurementDataBuffer.RemoveAt(0);
	TArray<uint8> DataToSend;
	DataToSend.Empty();

	// Basics
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.Index), sizeof(int32));
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.Timestamp), sizeof(double));
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.MaximumStrength), sizeof(float));
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.MaximumCurvature), sizeof(float));
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.MaximumTotalDistance), sizeof(float));

	// Some variables for easier parsing
	DataToSend.Append(reinterpret_cast<uint8*>(&InputSettings->NumberOfSimFrequencies), sizeof(int32));
	DataToSend.Append(reinterpret_cast<uint8*>(&InputSettings->SampleRate), sizeof(int32));
	DataToSend.Append(reinterpret_cast<uint8*>(&InputSettings->PointsInSensorFrame), sizeof(bool));
	
	// Transforms
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SensorLocation), sizeof(FVector));
	FQuat SensorRotationQuaternion = SonoTraceUEOutputToSend.SensorRotation.Quaternion();
	DataToSend.Append(reinterpret_cast<uint8*>(&SensorRotationQuaternion), sizeof(FQuat));
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SensorToOwnerTranslation), sizeof(FVector));
	FQuat SensorToOwnerRotationQuaternion = SonoTraceUEOutputToSend.SensorToOwnerRotation.Quaternion();
	DataToSend.Append(reinterpret_cast<uint8*>(&SensorToOwnerRotationQuaternion), sizeof(FQuat));	
	DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.OwnerLocation), sizeof(FVector));
	FQuat OwnerRotationQuaternion = SonoTraceUEOutputToSend.OwnerRotation.Quaternion();
	DataToSend.Append(reinterpret_cast<uint8*>(&OwnerRotationQuaternion), sizeof(FQuat));	
	int32 EmitterCount = SonoTraceUEOutputToSend.EmitterPoses.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&EmitterCount), sizeof(int32));
	for (int EmitterIndex = 0; EmitterIndex < EmitterCount; EmitterIndex++)
	{
		FVector CurrentEmitterLocation = SonoTraceUEOutputToSend.EmitterPoses[EmitterIndex].GetLocation();
		FQuat CurrentEmitterRotationQuaternion = SonoTraceUEOutputToSend.EmitterPoses[EmitterIndex].GetRotation();
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentEmitterLocation), sizeof(FVector));
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentEmitterRotationQuaternion), sizeof(FQuat));
	}			
	int32 ReceiverCount = SonoTraceUEOutputToSend.ReceiverPoses.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&ReceiverCount), sizeof(int32));
	for (int ReceiverIndex = 0; ReceiverIndex < ReceiverCount; ReceiverIndex++)
	{
		FVector CurrentReceiverLocation = SonoTraceUEOutputToSend.ReceiverPoses[ReceiverIndex].GetLocation();
		FQuat CurrentReceiverRotationQuaternion = SonoTraceUEOutputToSend.ReceiverPoses[ReceiverIndex].GetRotation();
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentReceiverLocation), sizeof(FVector));
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentReceiverRotationQuaternion), sizeof(FQuat));
	}

	// Direct path LOS results
	int32 DirectPathLOSCount = SonoTraceUEOutputToSend.DirectPathLOS.Num();
	DataToSend.Append(reinterpret_cast<uint8*>(&DirectPathLOSCount), sizeof(int32));
	for (int ReceiverIndex = 0; ReceiverIndex < DirectPathLOSCount; ReceiverIndex++)
	{
		bool CurrentDirectPathLOS = SonoTraceUEOutputToSend.DirectPathLOS[ReceiverIndex];
		DataToSend.Append(reinterpret_cast<uint8*>(&CurrentDirectPathLOS), sizeof(bool));
	}

	// Emitter signal indexes
	for (int EmitterIndex = 0; EmitterIndex < EmitterCount; EmitterIndex++)
	{
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.EmitterSignalIndexes[EmitterIndex]), sizeof(int32));
	}
	
	// Reflected points data
	int32 ReflectedPointsCount = 0;
	if (SonoTraceUEOutputToSend.ReflectedPoints.IsEmpty())
	{
		DataToSend.Append(reinterpret_cast<uint8*>(&ReflectedPointsCount), sizeof(int32));

	}else
	{
		ReflectedPointsCount = SonoTraceUEOutputToSend.ReflectedPoints.Num();
		DataToSend.Append(reinterpret_cast<uint8*>(&ReflectedPointsCount), sizeof(int32));
		for (FSonoTraceUEPointStruct& Point : SonoTraceUEOutputToSend.ReflectedPoints)
		{
			TArray<uint8> SerializedPoint = SerializePointStruct(&Point);
			int32 PointSize = SerializedPoint.Num();
			DataToSend.Append(reinterpret_cast<uint8*>(&PointSize), sizeof(int32));
			DataToSend.Append(SerializedPoint.GetData(), PointSize);
		}
	}

	// Specular sub result
	bool SpecularResultIncluded = false;
	if (SonoTraceUEOutputToSend.SpecularSubOutput.Timestamp == 0 || !InterfaceSettings->EnableSubOutput)
	{
		DataToSend.Append(reinterpret_cast<uint8*>(&SpecularResultIncluded), sizeof(bool));
	}else
	{
		SpecularResultIncluded = true;
		DataToSend.Append(reinterpret_cast<uint8*>(&SpecularResultIncluded), sizeof(bool));

		// Basics
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SpecularSubOutput.Timestamp), sizeof(double));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SpecularSubOutput.MaximumStrength), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SpecularSubOutput.MaximumCurvature), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.SpecularSubOutput.MaximumTotalDistance), sizeof(float));

		// Reflected points data
		int32 SpecularSubResultReflectedPointsCount = 0;
		if (SonoTraceUEOutputToSend.SpecularSubOutput.ReflectedPoints.IsEmpty())
		{
			DataToSend.Append(reinterpret_cast<uint8*>(&SpecularSubResultReflectedPointsCount), sizeof(int32));

		}else
		{
			SpecularSubResultReflectedPointsCount = SonoTraceUEOutputToSend.SpecularSubOutput.ReflectedPoints.Num();
			DataToSend.Append(reinterpret_cast<uint8*>(&SpecularSubResultReflectedPointsCount), sizeof(int32));
			for (FSonoTraceUEPointStruct& Point : SonoTraceUEOutputToSend.SpecularSubOutput.ReflectedPoints)
			{
				TArray<uint8> SerializedPoint = SerializePointStruct(&Point);
				int32 PointSize = SerializedPoint.Num();
				DataToSend.Append(reinterpret_cast<uint8*>(&PointSize), sizeof(int32));
				DataToSend.Append(SerializedPoint.GetData(), PointSize);
			}
			
			// Reflected strengths
			DataToSend.Append(reinterpret_cast<uint8*>(SonoTraceUEOutputToSend.SpecularSubOutput.ReflectedStrengths.GetData()), sizeof(float) * SpecularSubResultReflectedPointsCount);
		}				
	}

	// Diffraction sub result
	bool DiffractionResultIncluded = false;
	if (SonoTraceUEOutputToSend.DiffractionSubOutput.Timestamp == 0 || !InterfaceSettings->EnableSubOutput)
	{
		DataToSend.Append(reinterpret_cast<uint8*>(&DiffractionResultIncluded), sizeof(bool));
	}else
	{
		DiffractionResultIncluded = true;
		DataToSend.Append(reinterpret_cast<uint8*>(&DiffractionResultIncluded), sizeof(bool));

		// Basics
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DiffractionSubOutput.Timestamp), sizeof(double));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DiffractionSubOutput.MaximumStrength), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DiffractionSubOutput.MaximumCurvature), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DiffractionSubOutput.MaximumTotalDistance), sizeof(float));

		// Reflected points data
		int32 DiffractionSubResultReflectedPointsCount = 0;
		if (SonoTraceUEOutputToSend.DiffractionSubOutput.ReflectedPoints.IsEmpty())
		{
			DataToSend.Append(reinterpret_cast<uint8*>(&DiffractionSubResultReflectedPointsCount), sizeof(int32));

		}else
		{
			DiffractionSubResultReflectedPointsCount = SonoTraceUEOutputToSend.DiffractionSubOutput.ReflectedPoints.Num();
			DataToSend.Append(reinterpret_cast<uint8*>(&DiffractionSubResultReflectedPointsCount), sizeof(int32));
			for (FSonoTraceUEPointStruct& Point : SonoTraceUEOutputToSend.DiffractionSubOutput.ReflectedPoints)
			{
				TArray<uint8> SerializedPoint = SerializePointStruct(&Point);
				int32 PointSize = SerializedPoint.Num();
				DataToSend.Append(reinterpret_cast<uint8*>(&PointSize), sizeof(int32));
				DataToSend.Append(SerializedPoint.GetData(), PointSize);
			}
			
			// Reflected strengths
			DataToSend.Append(reinterpret_cast<uint8*>(SonoTraceUEOutputToSend.DiffractionSubOutput.ReflectedStrengths.GetData()), sizeof(float) * DiffractionSubResultReflectedPointsCount);
		}				
	}

	// Direct path sub result
	bool DirectPathResultIncluded = false;
	if (SonoTraceUEOutputToSend.DirectPathSubOutput.Timestamp == 0 || !InterfaceSettings->EnableSubOutput)
	{
		DataToSend.Append(reinterpret_cast<uint8*>(&DirectPathResultIncluded), sizeof(bool));
	}else
	{
		DirectPathResultIncluded = true;
		DataToSend.Append(reinterpret_cast<uint8*>(&DirectPathResultIncluded), sizeof(bool));

		// Basics
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DirectPathSubOutput.Timestamp), sizeof(double));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DirectPathSubOutput.MaximumStrength), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DirectPathSubOutput.MaximumCurvature), sizeof(float));
		DataToSend.Append(reinterpret_cast<uint8*>(&SonoTraceUEOutputToSend.DirectPathSubOutput.MaximumTotalDistance), sizeof(float));

		// Reflected points data
		int32 DirectPathSubResultReflectedPointsCount = 0;
		if (SonoTraceUEOutputToSend.DirectPathSubOutput.ReflectedPoints.IsEmpty())
		{
			DataToSend.Append(reinterpret_cast<uint8*>(&DirectPathSubResultReflectedPointsCount), sizeof(int32));

		}else
		{
			DirectPathSubResultReflectedPointsCount = SonoTraceUEOutputToSend.DirectPathSubOutput.ReflectedPoints.Num();
			DataToSend.Append(reinterpret_cast<uint8*>(&DirectPathSubResultReflectedPointsCount), sizeof(int32));
			for (FSonoTraceUEPointStruct& Point : SonoTraceUEOutputToSend.DirectPathSubOutput.ReflectedPoints)
			{
				TArray<uint8> SerializedPoint = SerializePointStruct(&Point);
				int32 PointSize = SerializedPoint.Num();
				DataToSend.Append(reinterpret_cast<uint8*>(&PointSize), sizeof(int32));
				DataToSend.Append(SerializedPoint.GetData(), PointSize);
			}
			
			// Reflected strengths
			DataToSend.Append(reinterpret_cast<uint8*>(SonoTraceUEOutputToSend.DirectPathSubOutput.ReflectedStrengths.GetData()), sizeof(float) * DirectPathSubResultReflectedPointsCount);
		}				
	}

	DataToSend.Shrink();
	TArray<uint8> DataSizeToSend;
	DataSizeToSend.Empty();
	int32 DataSize = DataToSend.Num();
	DataSizeToSend.Append(reinterpret_cast<uint8*>(&DataSize), sizeof(int32));
	DataSizeToSend.Shrink();
	ObjectDelivererManager->Send(DataSizeToSend);
	ObjectDelivererManager->Send(DataToSend);
	InterfaceMeasurementMessageAnnouncementAck = false;
	UE_LOG(SonoTraceUE, Log, TEXT("Sent measurement #%i (%i bytes). Queue size: %i."), SonoTraceUEOutputToSend.Index, DataSize, InterfaceMeasurementDataBuffer.Num());
	if (InputSettings->EnableDebugLogExecutionTimes)
		UE_LOG(SonoTraceUE, Log, TEXT("Interface measurement message generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
	if (!InterfaceMeasurementDataBuffer.IsEmpty())
	{
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_measurement\n"));
	}else
	{
		InterfaceMeasurementMessageAnnouncementSent = false;
	}
}

void ASonoTraceUEActor::UpdateShaderParameters()
{
	FSonoTraceParameters Parameters;
	Parameters.Scene = GetWorld()->Scene->GetRenderScene();
	Parameters.GPUReadback = GPUReadback;
	Parameters.SensorPosition = GetActorLocation(); 
	Parameters.SensorRotation = GetActorRotation();  
	Parameters.DistributionAzimuthAngles = GeneratedSettings.AzimuthAngles;
	Parameters.DistributionElevationAngles = GeneratedSettings.ElevationAngles;
	Parameters.MaxTraceDistance = InputSettings->MaximumRayDistance;
	Parameters.NumDistributionRays = GeneratedSettings.AzimuthAngles.Num();
	Parameters.MaxBounces = static_cast<uint32>(InputSettings->MaximumBounces);
	TArray<FVector> EmitterPositions;
	for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); EmitterIndex++)
	{
		EmitterPositions.Add(EmitterPoses[EmitterIndex].GetLocation());
	}
	Parameters.EmitterPositions = EmitterPositions;
	Parameters.EmitterCount = EmitterPoses.Num();	
	Parameters.EnableDirectPath = InputSettings->EnableDirectPathComponentCalculation;
	if (Parameters.EnableDirectPath)
	{
		Parameters.DirectPathAzimuthAngles = DirectPathAzimuthAngles;
		Parameters.DirectPathElevationAngles = DirectPathElevationAngles;
	}
	SonoTrace.UpdateParameters(Parameters);
}

bool ASonoTraceUEActor::ExecuteRayTracingOnce(const TArray<int32> OverrideEmitterSignalIndexes)
{
	if (InputSettings->EnableRaytracing && ((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation)) && Initialized)
	{
		if (SonoTrace.RunState != 1 && !AwaitingRayTracingResult)
		{
			if (!OverrideEmitterSignalIndexes.IsEmpty()){
				TriggerTemporaryEmitterSignalIndexes = OverrideEmitterSignalIndexes;
			} else {
				TriggerTemporaryEmitterSignalIndexes.Empty();
			}
			SonoTrace.RunState = 1;
			AwaitingRayTracingResult = true;
			return true;
		}	
	}
	return false;
}

void ASonoTraceUEActor::Tick(float DeltaTime)
{
	if (!Initialized)
		TranscurredTime += DeltaTime;
	
	Super::Tick(DeltaTime);

	if (InputSettings->DebugDisableInitialization)
	{
		DebugRaytracingShaderTestTick(DeltaTime);
	}else
	{
		UpdateTransformations();
		
		if (InterfaceConnected)
		{
			UpdateInterface();
		}	

		if ((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation))
		{
			if (Initialized && !StaticMeshComponentsToLoad.IsEmpty())
			{
				for (int i = StaticMeshComponentsToLoad.Num() - 1; i >= 0; i--)
				{
					TTuple<FString, UStaticMeshComponent*, int32> ObjectNameAndStaticMeshComponentAndAttempts = StaticMeshComponentsToLoad[i];
					FString ObjectName = ObjectNameAndStaticMeshComponentAndAttempts.Get<0>();
					UStaticMeshComponent* StaticMeshComponent = ObjectNameAndStaticMeshComponentAndAttempts.Get<1>();
					int32 Attempts = ObjectNameAndStaticMeshComponentAndAttempts.Get<2>();				
					StaticMeshComponentsToLoad.RemoveAt(i);
					AddStaticMeshComponent(StaticMeshComponent, ObjectName, false, false, Attempts == InputSettings->MeshDataGenerationAttempts, Attempts);					
				}
			}
			if (Initialized && !SkeletalMeshComponentsToLoad.IsEmpty())
			{
				for (int i = SkeletalMeshComponentsToLoad.Num() - 1; i >= 0; i--)
				{
					TTuple<FString, USkeletalMeshComponent*, int32> ObjectNameAndSkeletalMeshComponentAndAttempts = SkeletalMeshComponentsToLoad[i];
					FString ObjectName = ObjectNameAndSkeletalMeshComponentAndAttempts.Get<0>();
					USkeletalMeshComponent* SkeletalMeshComponent = ObjectNameAndSkeletalMeshComponentAndAttempts.Get<1>();
					int32 Attempts = ObjectNameAndSkeletalMeshComponentAndAttempts.Get<2>();
					SkeletalMeshComponentsToLoad.RemoveAt(i);
					AddSkeletalMeshComponent(SkeletalMeshComponent, ObjectName, false, false, Attempts == InputSettings->MeshDataGenerationAttempts, Attempts);

				}
			}
			if (InputSettings->EnableRaytracing)
			{
				if (GPUReadback != nullptr && TranscurredTime > 3.0f)
				{
					UpdateShaderParameters();		

					if (!Initialized)
					{
						UE_LOG(SonoTraceUE, Log, TEXT("Starting initialization..."));
						double CurrentTime = FPlatformTime::Seconds();
						GenerateAllInitialMeshData();
						if (InputSettings->EnableDebugLogExecutionTimes)
							UE_LOG(SonoTraceUE, Log, TEXT("Initial mesh data generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
						ENQUEUE_RENDER_COMMAND(FSonoTrace) (
							[this](FRHICommandListImmediate&)
							{
								SonoTrace.BeginRendering();
								return true;
							});
						Initialized = true;

						UE_LOG(SonoTraceUE, Log, TEXT("Completed initialization."));
						
						if (!InputSettings->EnableRunSimulationOnlyOnTrigger)
							AwaitingRayTracingResult = true;
					}
				}
				
				if (AwaitingRayTracingResult)
				{
					ParseRayTracing();
				}
				
				if (ReadyToUseRayTracingResult)
				{
					double CurrentTime = FPlatformTime::Seconds();
					RunSimulation(TriggerTemporaryEmitterSignalIndexes);
					if (InputSettings->EnableDebugLogExecutionTimes)
						UE_LOG(SonoTraceUE, Log, TEXT("Complete simulation generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
					ReadyToUseRayTracingResult = false;
					TriggerTemporaryEmitterSignalIndexes.Empty();
					if (!InputSettings->EnableRunSimulationOnlyOnTrigger)
						AwaitingRayTracingResult = true;
				}			
			}else
			{
				if (TranscurredTime > 3.0f){
					if (!Initialized)
					{
						UE_LOG(SonoTraceUE, Log, TEXT("Starting initialization..."));
						GenerateAllInitialMeshData();
						UE_LOG(SonoTraceUE, Log, TEXT("Completed initialization."));
						Initialized = true;
					}
					if (!InputSettings->EnableRunSimulationOnlyOnTrigger)
					{
						double CurrentTime = FPlatformTime::Seconds();
						RunSimulation(TArray<int32>());
						if (InputSettings->EnableDebugLogExecutionTimes)
							UE_LOG(SonoTraceUE, Log, TEXT("Complete simulation generation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
					}
				}
			}

			if (TranscurredTime > 3.0f)
			{
				if (InputSettings->EnableDrawDebug)
					DrawSimulationDebug();
				if (InputSettings->EnableDraw)
					DrawSimulationResult();
			}
		}
	}
}

void ASonoTraceUEActor::BeginDestroy()
{
	EndSimulation();
	Super::BeginDestroy();
}

void ASonoTraceUEActor::EndSimulation()
{
	SonoTrace.EndRendering();
	if (GPUReadback)
	{
		delete GPUReadback;
		GPUReadback = nullptr;
	}	
}

bool ASonoTraceUEActor::SendInterfaceDataMessage(const int32 Type, const TArray<int32> Order,
	const TArray<FString> Strings, const TArray<int32> Integers, const TArray<float> Floats)
{
	if (InterfaceReadyForMessages)
	{
		FSonoTraceUEDataMessage DataMessage;
		DataMessage.Type = Type;
		DataMessage.Order = Order;
		DataMessage.Strings = Strings;
		DataMessage.Integers = Integers;
		DataMessage.Floats = Floats;
		InterfaceDataMessageDataBuffer.Add(DataMessage);
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_data\n"));
		InterfaceDataMessageAnnouncementSent = true;
		InterfaceDataMessageAnnouncementAck = false;
		return true;	
	}
	return false;
}

bool ASonoTraceUEActor::TriggerSimulation()
{
	return TriggerSimulationOverrideEmitterSignals(TArray<int32>());
}

bool ASonoTraceUEActor::TriggerSimulationOverrideEmitterSignals(const TArray<int32> OverrideEmitterSignalIndexes)
{
	if ((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation))
	{
		if (InputSettings->EnableRunSimulationOnlyOnTrigger)
		{
			if (!OverrideEmitterSignalIndexes.IsEmpty() && OverrideEmitterSignalIndexes.Num() != EmitterPoses.Num())
			{
				UE_LOG(SonoTraceUE, Error, TEXT("Could not trigger measurement. Invalid override emitter signal indexes, array is not the same size as the amount of emitters!"));
				return false;
			}
			if (!OverrideEmitterSignalIndexes.IsEmpty())
			{
				for (int EmitterSignalArrayIndex = 0; EmitterSignalArrayIndex < OverrideEmitterSignalIndexes.Num(); EmitterSignalArrayIndex++)
				{
					if (OverrideEmitterSignalIndexes[EmitterSignalArrayIndex] >= InputSettings->EmitterSignals.Num() || OverrideEmitterSignalIndexes[EmitterSignalArrayIndex] < 0){				
						UE_LOG(SonoTraceUE, Error, TEXT("Could not trigger measurement. Invalid override emitter signal index %i for emitter #%i."), OverrideEmitterSignalIndexes[EmitterSignalArrayIndex], EmitterSignalArrayIndex);
						return false;
					}
				}
			}						
			if (InputSettings->EnableRaytracing)
				return ExecuteRayTracingOnce(OverrideEmitterSignalIndexes);
			RunSimulation(OverrideEmitterSignalIndexes);
			return true;
		}
	}
	return false;
}

bool ASonoTraceUEActor::SetCurrentEmitterSignalIndexes(TArray<int32> EmitterSignalIndexes)
{
	if (EmitterSignalIndexes.IsEmpty())
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Could not set emitter signal indexes. Invalid emitter signal indexes, array is empty!"));
		return false;
	}
	if (EmitterSignalIndexes.Num() != EmitterPoses.Num())
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Could not set emitter signal indexes. Invalid emitter signal indexes, array is not the same size as the amount of emitters!"));
		return false;
	}
	for (int EmitterSignalArrayIndex = 0; EmitterSignalArrayIndex < EmitterSignalIndexes.Num(); EmitterSignalArrayIndex++)
	{
		if (EmitterSignalIndexes[EmitterSignalArrayIndex] < InputSettings->EmitterSignals.Num() && EmitterSignalIndexes[EmitterSignalArrayIndex] >= 0)
		{
			CurrentEmitterSignalIndexes[EmitterSignalArrayIndex] = EmitterSignalIndexes[EmitterSignalArrayIndex];
		}else
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Invalid emitter signal index %i for emitter #%i. Skipping."), EmitterSignalIndexes[EmitterSignalArrayIndex], EmitterSignalArrayIndex);
			return false;
		}
	}		
	return true;
}

TArray<int32> ASonoTraceUEActor::GetCurrentEmitterSignalIndexes()
{
	return CurrentEmitterSignalIndexes;
}

bool ASonoTraceUEActor::SetCurrentEmitterSignalIndexForSpecificEmitter(const int32 EmitterIndex, const int32 EmitterSignalIndex)
{
	if (EmitterIndex >= EmitterPoses.Num() || EmitterIndex < 0)
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Could not set emitter signal index for emitter #%i. Invalid emitter index provided."), EmitterIndex);
		return false;
	}
	if (EmitterSignalIndex < InputSettings->EmitterSignals.Num() && EmitterSignalIndex >= 0)
	{
		CurrentEmitterSignalIndexes[EmitterIndex] = EmitterSignalIndex;
		return true;
	}
	UE_LOG(SonoTraceUE, Error, TEXT("Invalid emitter signal index %i for emitter #%i."), EmitterSignalIndex, EmitterIndex);
	return false;
}

int32 ASonoTraceUEActor::GetCurrentEmitterSignalIndexForSpecificEmitter(const int32 EmitterIndex)
{
	if (EmitterIndex < EmitterPoses.Num() && EmitterIndex >= 0)
	{
		return CurrentEmitterSignalIndexes[EmitterIndex];
	}
	UE_LOG(SonoTraceUE, Error, TEXT("Could not get emitter signal index for emitter #%i. Invalid emitter index provided."), EmitterIndex);
	return -1;
}

int32 ASonoTraceUEActor::GetEmitterSignalCount() const
{
	return InputSettings->EmitterSignals.Num();
}

void ASonoTraceUEActor::ParseRayTracing()
{
	if (GPUReadback != nullptr && TranscurredTime > 3.0f && Initialized && SonoTrace.RunState == 2 && SonoTrace.ExecutionCounter > SonoTracePreviousIndex && !CurrentlyParsingRaytracing)
	{	
		ENQUEUE_RENDER_COMMAND(FSonoTrace) (
		[this](FRHICommandListImmediate& RHICmdList)
		{
			CurrentlyParsingRaytracing = true;
			if (GPUReadback->IsReady())
			{

				const uint32 AllocatedSize = GPUReadback->GetGPUSizeBytes();
				
				if (const uint32 ExpectedSize = (GeneratedSettings.AzimuthAngles.Num() + DirectPathAzimuthAngles.Num()) * static_cast<uint32>(InputSettings->MaximumBounces) * sizeof(FStructuredOutputBufferElem); AllocatedSize != ExpectedSize)
				{
					UE_LOG(SonoTraceUE, Error, TEXT("GPU readback size mismatch. Expected: %i, Actual: %i"), ExpectedSize, AllocatedSize);
				}

				
				double CurrentTime = FPlatformTime::Seconds();
				RayTracingSubOutput.ReflectedPoints.Init(FSonoTraceUEPointStruct(), GeneratedSettings.AzimuthAngles.Num() * InputSettings->MaximumBounces);
				RayTracingSubOutput.HitPersistentPrimitiveIndexes.Empty();
				SonoTracePreviousIndex = SonoTrace.ExecutionCounter;

				// Lock the data to access it on the CPU
				RayTracingRawOutput = static_cast<FStructuredOutputBufferElem*>(
					GPUReadback->Lock((GeneratedSettings.AzimuthAngles.Num() + DirectPathAzimuthAngles.Num()) * static_cast<uint32>(InputSettings->MaximumBounces) * sizeof(FStructuredOutputBufferElem))); 

				// Validate that the saved data is valid
				if (RayTracingRawOutput)
				{
					RayTracingSubOutput.Timestamp = SonoTrace.CurrentTimestamp;
					RayTracingSubOutput.MaximumCurvature = 0.0f;
					RayTracingSubOutput.MaximumStrength = 0.0f;
					RayTracingSubOutput.MaximumTotalDistance = 0.0f;

					// Get the render scene to later find unknown objects
					FScene* RenderScene = GetWorld()->Scene->GetRenderScene();
				
					// Loop the rays
					int32 SavedPointIndex = 0;					
					for (int32 RayIndex = 0; RayIndex < GeneratedSettings.AzimuthAngles.Num(); RayIndex++)
					{
						// Start multi-bounce loop
						bool CurrentRayIsHitting = false;						
						TArray<float> CachedSourceDirectivities;
						CachedSourceDirectivities.Init(1.0f, EmitterPoses.Num());						
						for (int32 BounceIndex = 0; BounceIndex < InputSettings->MaximumBounces; BounceIndex++)
						{
							// Compute the output index for this ray and this bounce
							const int32 OutputIndex = RayIndex * InputSettings->MaximumBounces + BounceIndex;

							if (OutputIndex < 0 || OutputIndex >= GeneratedSettings.AzimuthAngles.Num() * InputSettings->MaximumBounces)
							{
								UE_LOG(SonoTraceUE, Error, TEXT("OutputIndex [%d] is out of bounds!"), OutputIndex);
								continue; // Skip invalid accesses
							}

							// Check if this ray was a hit			
							if (const FStructuredOutputBufferElem& CurrentRayTracingOutput = RayTracingRawOutput[OutputIndex]; CurrentRayTracingOutput.IsHit)
							{
								CurrentRayIsHitting = true;

								// Check if it has line-of-sight to the sensor
								if (CurrentRayTracingOutput.HitLineOfSightToSensor)
								{
									const int32 CurrentScenePrimitiveIndex = CurrentRayTracingOutput.HitScenePrimitiveIndex;
									const int32 CurrentTriangleIndex = CurrentRayTracingOutput.HitTriangleIndex;
									int32 CurrentPersistentPrimitiveIndex = -1;
									if (ScenePrimitiveIndexToPersistentPrimitiveIndex.Find(CurrentScenePrimitiveIndex))
										CurrentPersistentPrimitiveIndex = ScenePrimitiveIndexToPersistentPrimitiveIndex[CurrentScenePrimitiveIndex];										

									// Compute the hit location in world space
									FVector HitLocation = FVector(CurrentRayTracingOutput.HitPosX,
																  CurrentRayTracingOutput.HitPosY,
																  CurrentRayTracingOutput.HitPosZ);

									FVector HitReflectionDirection(CurrentRayTracingOutput.HitReflectionX,
																   CurrentRayTracingOutput.HitReflectionY,
																   CurrentRayTracingOutput.HitReflectionZ);
									
									// If this is the original transmission, calculate the angle for source directivity and cache it
									if (InputSettings->EnableEmitterDirectivity){
										if (BounceIndex == 0){
											for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); ++EmitterIndex){
												FVector LaunchVector = (HitLocation - EmitterPoses[EmitterIndex].GetLocation()).GetSafeNormal();
												FVector EmitterForward = EmitterPoses[EmitterIndex].GetUnitAxis(EAxis::X);
												float Dot = FVector::DotProduct(LaunchVector, EmitterForward);																					
												float Directivity = (1.0f - GeneratedSettings.FinalEmitterDirectivities[EmitterIndex]) + (GeneratedSettings.FinalEmitterDirectivities[EmitterIndex] * Dot);
												CachedSourceDirectivities[EmitterIndex] = FMath::Max(0.0f, Directivity);
											}
										}										
									}

									const float DistanceToSensor = FVector::Distance(HitLocation, SensorLocation);
									FName ObjectName = "UNKNOWN";
									int32 ObjectTypeIndex = 0;
									const float RayDistanceTotal = CurrentRayTracingOutput.RayDistanceTotal;
									TArray<float> RayDistancesTotalFromEmitters;
									RayDistancesTotalFromEmitters.Append(CurrentRayTracingOutput.DistancesFromEmitterTotal, EmitterPoses.Num());

									float CurvatureMagnitude = 0;
									TArray<float>* SurfaceBRDF;
									TArray<float>* SurfaceMaterial;

									// Retrieve the object that was hit or add it first if not in tables yet
									bool AddNewObject = true;
									if (CurrentPersistentPrimitiveIndex != -1)
									{
										if(PersistentPrimitiveIndexToLabelsAndObjectTypes.Contains(CurrentPersistentPrimitiveIndex))
											AddNewObject = false;										
									}
									if(!AddNewObject){
										TTuple<FName, int32> ObjectNameAndTypeIndex = PersistentPrimitiveIndexToLabelsAndObjectTypes.FindChecked(CurrentPersistentPrimitiveIndex);
										ObjectName = ObjectNameAndTypeIndex.Get<0>();
										ObjectTypeIndex = ObjectNameAndTypeIndex.Get<1>();
									}else
									{
										if (RenderScene && CurrentScenePrimitiveIndex >= 0 && CurrentScenePrimitiveIndex < RenderScene->Primitives.Num())
										{
											if (const FPrimitiveSceneInfo* PrimitiveSceneInfo = RenderScene->Primitives[CurrentScenePrimitiveIndex])
											{
												if (const FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy)
												{
													CurrentPersistentPrimitiveIndex = PrimitiveSceneInfo->GetPersistentIndex().Index;
													FName NewObjectName = FName(PrimitiveSceneProxy->GetOwnerName().ToString() + TEXT("_") + PrimitiveSceneProxy->GetResourceName().ToString());
													PersistentPrimitiveIndexToLabelsAndObjectTypes.Add(CurrentPersistentPrimitiveIndex, TTuple<FName, int32>(NewObjectName, 0));
													ScenePrimitiveIndexToPersistentPrimitiveIndex.Add(CurrentScenePrimitiveIndex, CurrentPersistentPrimitiveIndex);
													ObjectName = NewObjectName;
													UE_LOG(SonoTraceUE, Log, TEXT("Added unknown object with PPI #%d, SPI #%d and label '%s' using resource '%s' and object type 'default (#0)'."),
																					CurrentPersistentPrimitiveIndex, CurrentScenePrimitiveIndex, *NewObjectName.ToString(), *PrimitiveSceneProxy->GetResourceName().ToString());
												}
											}		
										}
									}
									RayTracingSubOutput.HitPersistentPrimitiveIndexes.AddUnique(CurrentPersistentPrimitiveIndex);
									if (PersistentPrimitiveIndexToMeshDataIndex.Contains(CurrentPersistentPrimitiveIndex))
									{
										const int32 MeshDataIndex = PersistentPrimitiveIndexToMeshDataIndex[CurrentPersistentPrimitiveIndex];
										FSonoTraceUEMeshDataStruct* CurrentMeshData = &MeshData[MeshDataIndex];
										if (CurrentMeshData->TriangleCurvatureMagnitude.Num() > CurrentTriangleIndex)
										{
											CurvatureMagnitude = CurrentMeshData->TriangleCurvatureMagnitude[CurrentTriangleIndex];
											SurfaceBRDF = &CurrentMeshData->TriangleBRDF[CurrentTriangleIndex];
											SurfaceMaterial = &CurrentMeshData->TriangleMaterial[CurrentTriangleIndex];
										}else
										{
											// TODO: Fix why this happens
										    UE_LOG(SonoTraceUE, Warning, TEXT("Mesh data triangle index out of bounds. Object name: %s, PPI: %i, SPI: %i and triangle Index: %i"), 
											                          *ObjectName.ToString(), CurrentPersistentPrimitiveIndex, CurrentScenePrimitiveIndex, CurrentTriangleIndex);
											SurfaceBRDF = &GeneratedSettings.ObjectSettings[0].DefaultTriangleBRDF;
											SurfaceMaterial = &GeneratedSettings.ObjectSettings[0].DefaultTriangleMaterial;
										}
									}else
									{
										SurfaceBRDF = &GeneratedSettings.ObjectSettings[0].DefaultTriangleBRDF;
										SurfaceMaterial = &GeneratedSettings.ObjectSettings[0].DefaultTriangleMaterial;
									}

									RayTracingSubOutput.ReflectedPoints[SavedPointIndex] = FSonoTraceUEPointStruct(HitLocation,
																													HitReflectionDirection,
																													ObjectName,
																													OutputIndex,
																													RayDistanceTotal,
																													RayDistancesTotalFromEmitters,
																													DistanceToSensor,
																													ObjectTypeIndex,
																													CurvatureMagnitude,
																													SurfaceBRDF,
																													SurfaceMaterial,
																													RayIndex,
																													BounceIndex,
																													CachedSourceDirectivities);
									SavedPointIndex++;
									if (CurvatureMagnitude > RayTracingSubOutput.MaximumCurvature)
										RayTracingSubOutput.MaximumCurvature = CurvatureMagnitude;
									if (RayDistanceTotal > RayTracingSubOutput.MaximumTotalDistance)
										RayTracingSubOutput.MaximumTotalDistance = RayDistanceTotal;
								}
							}
							else{
								if (CurrentRayIsHitting)
									RayTracingSubOutput.ReflectedPoints[SavedPointIndex - 1].IsLastHit = true;
								break;
							}
						}
					}
					if (InputSettings->EnableDirectPathComponentCalculation)
					{
						for (int32 ReceiverIndex = 0; ReceiverIndex < DirectPathAzimuthAngles.Num(); ReceiverIndex++)
						{
							int32 DataIndex = GeneratedSettings.AzimuthAngles.Num() * InputSettings->MaximumBounces + ReceiverIndex * InputSettings->MaximumBounces;
							const FStructuredOutputBufferElem& CurrentRayTracingOutput = RayTracingRawOutput[DataIndex];
							
							if (CurrentRayTracingOutput.DirectPath == 1 && CurrentRayTracingOutput.IsHit)
							{
								float DistanceSensorToReceiver = FVector::Distance(ReceiverPoses[ReceiverIndex].GetLocation(), SensorLocation);
								if (DistanceSensorToReceiver < InputSettings->MaximumRayDistance * 2)
								{
									DirectPathReceiverOutput[ReceiverIndex] = TTuple<bool, FVector>(true, ReceiverPoses[ReceiverIndex].GetLocation());
								}else
								{
									DirectPathReceiverOutput[ReceiverIndex] = TTuple<bool, FVector>(false, FVector(NAN, NAN,NAN));
								}
							}else
							{
								FVector HitLocation = FVector(CurrentRayTracingOutput.HitPosX, CurrentRayTracingOutput.HitPosY, CurrentRayTracingOutput.HitPosZ);
								float DistanceSensorToReceiver = FVector::Distance(ReceiverPoses[ReceiverIndex].GetLocation(), SensorLocation);
								if (DistanceSensorToReceiver < CurrentRayTracingOutput.RayDistanceTotal)
								{
									DirectPathReceiverOutput[ReceiverIndex] = TTuple<bool, FVector>(true, ReceiverPoses[ReceiverIndex].GetLocation());
								}else
								{
									DirectPathReceiverOutput[ReceiverIndex] = TTuple<bool, FVector>(false, HitLocation);
								}								
							}
						}
					}
					// Remove the remaining part of the struct.
					RayTracingSubOutput.ReflectedPoints.RemoveAt(SavedPointIndex, RayTracingSubOutput.ReflectedPoints.Num() - SavedPointIndex);
				}				
				// Unlock the buffer when done
				GPUReadback->Unlock();

				RayTracingExecutionCount++;

				AwaitingRayTracingResult = false;
				ReadyToUseRayTracingResult = true;
				if (InputSettings->EnableDebugLogExecutionTimes)
					UE_LOG(SonoTraceUE, Log, TEXT("Raytracing parsing: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
				
				if (!InputSettings->EnableRunSimulationOnlyOnTrigger)
				{
					double CurrentTimeNow = FPlatformTime::Seconds(); // Get the current time in seconds
					if (CurrentTimeNow - RayTracingLastLoggedTime >= 5.0)       // Check if 5 seconds have passed
					{
						double Frequency = RayTracingExecutionCount / (CurrentTimeNow - RayTracingLastLoggedTime); // Calculate frequency
						if (InputSettings->EnableDebugLogExecutionTimes)
							UE_LOG(SonoTraceUE, Log, TEXT("Raytracing Frequency: %.2f Hz"), Frequency);

						// Reset counters
						RayTracingLastLoggedTime = CurrentTimeNow;
						RayTracingExecutionCount = 0;
					}
				}
			}
			CurrentlyParsingRaytracing = false;
		});
	}
}

void ASonoTraceUEActor::RunSimulation(const TArray<int32> OverrideEmitterSignalIndexes)
{
	int32 NewIndex =  CurrentOutput.Index + 1;
	CurrentOutput = FSonoTraceUEOutputStruct();
	CurrentOutput.Index = NewIndex;
	CurrentOutput.Timestamp = FDateTime::Now().ToUnixTimestamp();
	CurrentOutput.SensorLocation = SensorLocation;
	CurrentOutput.SensorRotation = SensorRotation;
	CurrentOutput.SensorToOwnerTranslation = SensorToOwnerTranslation;
	CurrentOutput.SensorToOwnerRotation = SensorToOwnerRotation;
	CurrentOutput.OwnerLocation = OwnerLocation;
	CurrentOutput.OwnerRotation = OwnerRotation;
	CurrentOutput.EmitterPoses = EmitterPoses;
	CurrentOutput.ReceiverPoses = ReceiverPoses;
	if (!OverrideEmitterSignalIndexes.IsEmpty())
	{
		CurrentOutput.EmitterSignalIndexes = OverrideEmitterSignalIndexes;
	}else
	{
		CurrentOutput.EmitterSignalIndexes = CurrentEmitterSignalIndexes;
	}

	FTransform SensorTransform(SensorRotation, SensorLocation);    
	FTransform WorldToSensorTransform = SensorTransform.Inverse();
	
	FSonoTraceUESubOutputStruct DirectPathSubOutput = FSonoTraceUESubOutputStruct();
	if (InputSettings->EnableDirectPathComponentCalculation)
	{
		double CurrentTime = FPlatformTime::Seconds();
		DirectPathSubOutput.MaximumCurvature = 0.0f;
		DirectPathSubOutput.MaximumStrength = 0.0f;
		DirectPathSubOutput.MaximumTotalDistance = 0.0f;
		DirectPathSubOutput.Timestamp = RayTracingSubOutput.Timestamp;
		CurrentOutput.DirectPathLOS.Init(false, ReceiverPoses.Num());

		for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); ++EmitterIndex)
		{
			TArray<TArray<TArray<float>>> Strengths; // Emitter // Receiver // Frequency
			TArray<TArray<float>> TotalDistancesToReceivers; // Emitter // Receiver
			float SummedStrength = 0.0f;

			TotalDistancesToReceivers.Init(TArray<float>(), EmitterPoses.Num());
			Strengths.SetNum(EmitterPoses.Num());
			for (int32 EmitterIndex2 = 0; EmitterIndex2 < EmitterPoses.Num(); ++EmitterIndex2)
			{
				Strengths[EmitterIndex2].Init(TArray<float>(), ReceiverPoses.Num());
				TotalDistancesToReceivers[EmitterIndex2].Init(0, ReceiverPoses.Num());
				for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
				{
					Strengths[EmitterIndex2][ReceiverIndex].Init(0, InputSettings->NumberOfSimFrequencies);
					TotalDistancesToReceivers[EmitterIndex2][ReceiverIndex] = FVector::Distance(EmitterPoses[EmitterIndex2].GetLocation(), ReceiverPoses[ReceiverIndex].GetLocation());
				}				
			}

			for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
			{

				TTuple<bool, FVector> LOSFoundAndTransformResult = DirectPathReceiverOutput[ReceiverIndex];

				if (LOSFoundAndTransformResult.Get<0>())
				{
					if (EmitterIndex == 0)
						CurrentOutput.DirectPathLOS[ReceiverIndex] = true;
					
					float EmitterDirectivity = 1.0f;
					if (InputSettings->EnableEmitterDirectivity)
					{
						const FVector EmitterToReceiver = (ReceiverPoses[ReceiverIndex].GetLocation() - EmitterPoses[EmitterIndex].GetLocation()).GetSafeNormal();
						const float EmDot = FVector::DotProduct(EmitterToReceiver, EmitterPoses[EmitterIndex].GetUnitAxis(EAxis::X));
						EmitterDirectivity = (1.0f - GeneratedSettings.FinalEmitterDirectivities[EmitterIndex]) + (GeneratedSettings.FinalEmitterDirectivities[EmitterIndex] * EmDot);
						EmitterDirectivity = FMath::Max(0.0f, EmitterDirectivity);
					}				
					
					float ReceiverDirectivity = 1.0f;
					if (InputSettings->EnableReceiverDirectivity)
					{
						const FVector ReceiverToEmitter = (EmitterPoses[EmitterIndex].GetLocation() - ReceiverPoses[ReceiverIndex].GetLocation()).GetSafeNormal();
						const float RecDot = FVector::DotProduct(ReceiverToEmitter, ReceiverPoses[ReceiverIndex].GetUnitAxis(EAxis::X));
						ReceiverDirectivity = (1.0f - GeneratedSettings.FinalReceiverDirectivities[ReceiverIndex]) +
							(GeneratedSettings.FinalReceiverDirectivities[ReceiverIndex] * RecDot);
						ReceiverDirectivity = FMath::Max(0.0f, ReceiverDirectivity);
					}

					// Path loss (geometrical spreading loss) in meters
					const float ReflectionStrengthPathLoss = 1.0f / FMath::Square(TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] / 100.0f);

					// Loop the simulation frequencies and calculate the specular reflection strength with the BRDF
					Strengths[EmitterIndex][ReceiverIndex].Init(0, InputSettings->NumberOfSimFrequencies);
					for (int32 FrequencyIndex = 0; FrequencyIndex < InputSettings->NumberOfSimFrequencies; FrequencyIndex++)
					{					
						const float AlphaAbsorption = 0.038 * (GeneratedSettings.Frequencies[FrequencyIndex] / 1000) - 0.3;
						const float PathlossAbsorption = FMath::Pow(10.0f, -(AlphaAbsorption * TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] / 100) / 20);
						const float Strength = InputSettings->DirectPathStrength * ReflectionStrengthPathLoss * PathlossAbsorption * EmitterDirectivity * ReceiverDirectivity;
						Strengths[EmitterIndex][ReceiverIndex][FrequencyIndex] = Strength;
						SummedStrength += Strength * Strength;
					}
				}				
			}
			SummedStrength = SummedStrength / ReceiverPoses.Num() / EmitterPoses.Num() / InputSettings->NumberOfSimFrequencies;
			if (SummedStrength > DirectPathSubOutput.MaximumStrength)
				DirectPathSubOutput.MaximumStrength = SummedStrength;

			const FName Label = FName(*(FString::Printf(TEXT("DIRECT_EMITTER_%d"), EmitterIndex)));
			const float SensorDistance = FVector::Distance(EmitterPoses[EmitterIndex].GetLocation(), SensorLocation);
			FSonoTraceUEPointStruct DirectPathPoint = FSonoTraceUEPointStruct(EmitterPoses[EmitterIndex].GetLocation(), SensorRotation.Vector(), Label, EmitterIndex,
																			  SensorDistance, SensorDistance, SummedStrength, TotalDistancesToReceivers, Strengths);
			if (InputSettings->PointsInSensorFrame)
			{    
				DirectPathPoint.Location = WorldToSensorTransform.TransformPosition(DirectPathPoint.Location);
				DirectPathPoint.ReflectionDirection = WorldToSensorTransform.TransformVector(DirectPathPoint.ReflectionDirection);
			}
			DirectPathSubOutput.ReflectedPoints.Add(DirectPathPoint);
			DirectPathSubOutput.ReflectedStrengths.Add(SummedStrength);
		}
		if (InputSettings->EnableSimulationSubOutput)
		{
			CurrentOutput.DirectPathSubOutput = DirectPathSubOutput;
		}		
		CurrentOutput.ReflectedPoints.Append(DirectPathSubOutput.ReflectedPoints);
		CurrentOutput.Timestamp = DirectPathSubOutput.Timestamp;
		if (InputSettings->EnableDebugLogExecutionTimes)
			UE_LOG(SonoTraceUE, Log, TEXT("Direct path component calculation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
	}
	if (InputSettings->EnableSpecularComponentCalculation)
	{
		double CurrentTime = FPlatformTime::Seconds();
		
		RunSpecularComponentSimulation(&RayTracingSubOutput, InputSettings, &GeneratedSettings, WorldToSensorTransform, EmitterPoses, ReceiverPoses);
		
		if (InputSettings->EnableSpecularComponentCalculation)
		{
			CurrentOutput.SpecularSubOutput = RayTracingSubOutput;
		}
		CurrentOutput.Timestamp = RayTracingSubOutput.Timestamp;		
		CurrentOutput.ReflectedPoints.Append(RayTracingSubOutput.ReflectedPoints);
		
		if (InputSettings->EnableDebugLogExecutionTimes)
			UE_LOG(SonoTraceUE, Log, TEXT("Specular component calculation: %.5fs"), FPlatformTime::Seconds() - CurrentTime);
	}

	FSonoTraceUESubOutputStruct DiffractionSubOutput = FSonoTraceUESubOutputStruct();
	if (InputSettings->EnableDiffractionComponentCalculation)
	{
		double CurrentTime = FPlatformTime::Seconds();
		DiffractionSubOutput.MaximumCurvature = 0.0f;
		DiffractionSubOutput.MaximumStrength = 0.0f;
		DiffractionSubOutput.MaximumTotalDistance = 0.0f;
		DiffractionSubOutput.Timestamp = CurrentOutput.Timestamp;
		
		TArray<int32> HitObjectsPersistentPrimitiveIndexes;
		TArray<int32> HitObjectTypes;
		TArray<FName> HitObjectLabels;
		TArray<FTransform> HitObjectTransforms;
		if (InputSettings->EnableRaytracing)
		{
			for (int32 PersistentPrimitiveIndex : RayTracingSubOutput.HitPersistentPrimitiveIndexes)
			{
				if (PersistentPrimitiveIndexToMeshDataIndex.Contains(PersistentPrimitiveIndex))
				{
					TTuple<FName, int32> ObjectNameAndTypeIndex = PersistentPrimitiveIndexToLabelsAndObjectTypes.FindChecked(PersistentPrimitiveIndex);
					FName ObjectName = ObjectNameAndTypeIndex.Get<0>();
					int32 ObjectTypeIndex = ObjectNameAndTypeIndex.Get<1>();
					// UE_LOG(SonoTraceUE, Log, TEXT("Object with label '%s' and object type #%d detected for diffraction."), *ObjectName.ToString(), ObjectTypeIndex);
					HitObjectsPersistentPrimitiveIndexes.Add(PersistentPrimitiveIndex);
					HitObjectTypes.Add(ObjectTypeIndex);
					HitObjectLabels.Add(ObjectName);
					HitObjectTransforms.Add(PersistentPrimitiveIndexToPrimitiveComponent.FindChecked(PersistentPrimitiveIndex)->GetComponentTransform());
				}
			}						
		}
		
		int32 NumDiffractionPoints = FMath::CeilToInt32(static_cast<float>(InputSettings->NumberOfInitialRays) / static_cast<float>(InputSettings->DiffractionSimDivisionFactor));
		DiffractionSubOutput.HitPersistentPrimitiveIndexes = HitObjectsPersistentPrimitiveIndexes;
		for (int32 HitIndex = 0; HitIndex < HitObjectsPersistentPrimitiveIndexes.Num(); ++HitIndex)
		{
			int32 PersistentPrimitiveIndex = HitObjectsPersistentPrimitiveIndexes[HitIndex];
			int32 MeshDataIndex = PersistentPrimitiveIndexToMeshDataIndex.FindChecked(PersistentPrimitiveIndex);
			FSonoTraceUEMeshDataStruct* CurrentMeshData = &MeshData[MeshDataIndex];

			TArray<float> ImportanceVertexCDF;
			ImportanceVertexCDF.Reserve(CurrentMeshData->ImportanceVertexOrderedBRDFValue.Num());
			float CumulativeSum = 0.0f;
			for (int32 i = 0; i < CurrentMeshData->ImportanceVertexOrderedBRDFValue.Num(); ++i)
			{
				const float Noise = 0.00001f * FMath::FRand(); 
				const float NoisyValue = CurrentMeshData->ImportanceVertexOrderedBRDFValue[i] + Noise;
				CumulativeSum += NoisyValue;
				ImportanceVertexCDF.Add(CumulativeSum);
			}
			
			float MaxCDFValue = TNumericLimits<float>::Min();
			for (float Value : ImportanceVertexCDF)
			{
				MaxCDFValue = FMath::Max(MaxCDFValue, Value);
			}
			if (MaxCDFValue > 0.0f) 
			{
				for (float& Value : ImportanceVertexCDF)
				{
					Value /= MaxCDFValue;
				}
			}

			TArray<float> SamplesRandomUniform;
			SamplesRandomUniform.Reserve(NumDiffractionPoints);

			for (int32 i = 0; i < NumDiffractionPoints; ++i)
			{
				SamplesRandomUniform.Add(FMath::FRand()); 
			}
			TArray<float> Indices;
			for (int32 i = 0; i < ImportanceVertexCDF.Num(); ++i)
			{
				Indices.Add(static_cast<float>(i));
			}
			TArray<float> InterpolatedResults = Interpolate(ImportanceVertexCDF, Indices, SamplesRandomUniform);
			
			TArray<int32> SamplesRandomImportance;
			for (float Value : InterpolatedResults)
			{
				SamplesRandomImportance.Add(FMath::RoundToInt32(Value));
			}

			TArray<FVector> DiffractionPositions;
			TArray<FVector> DiffractionNormals;
			TArray<int32> DiffractionTriangleIndexes;
			float MaxDistanceSquared = FMath::Square(InputSettings->MaximumRayDistance);
			for (int32 Index : SamplesRandomImportance)
			{

				int32 SortedTriangleIndex = CurrentMeshData->ImportanceVertexOrderedIndex[Index];
				FVector LocalPosition = CurrentMeshData->TrianglePosition[SortedTriangleIndex];
				FVector WorldPosition = HitObjectTransforms[HitIndex].TransformPosition(LocalPosition);
				FVector DirectionToPoint = WorldPosition - SensorLocation;	
				if (float DistanceSquared = DirectionToPoint.SizeSquared(); DistanceSquared <= MaxDistanceSquared && DistanceSquared > KINDA_SMALL_NUMBER && CurrentMeshData->TriangleSize[SortedTriangleIndex] < InputSettings->DiffractionTriangleSizeThreshold)
				{
					DirectionToPoint.Normalize();
					FVector LocalDirection = SensorRotation.UnrotateVector(DirectionToPoint);
					float ElevationAngle = FMath::RadiansToDegrees(FMath::Asin(LocalDirection.Z));
					float AzimuthAngle = FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y, LocalDirection.X));

					if (ElevationAngle >= InputSettings->SensorLowerElevationLimit
						&& ElevationAngle <= InputSettings->SensorUpperElevationLimit
						&& AzimuthAngle >=  InputSettings->SensorLowerAzimuthLimit
						&& AzimuthAngle <=  InputSettings->SensorUpperAzimuthLimit)
					{
						DiffractionPositions.Add(WorldPosition);
						DiffractionTriangleIndexes.Add(SortedTriangleIndex);
						FVector LocalNormal = CurrentMeshData->TriangleNormal[SortedTriangleIndex];
						FVector WorldNormal = HitObjectTransforms[HitIndex].TransformVectorNoScale(LocalNormal);
						DiffractionNormals.Add(WorldNormal);
					}
				}
			}
			
			TArray<TArray<FVector>> VectorEmitterToDiffractionNormed;
			VectorEmitterToDiffractionNormed.Init(TArray<FVector>(), EmitterPoses.Num());
			for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); ++EmitterIndex)
			{
				VectorEmitterToDiffractionNormed[EmitterIndex].Reserve(DiffractionPositions.Num());
				FVector EmitterLocation = EmitterPoses[EmitterIndex].GetLocation();
				for (const FVector& DiffractionPoint : DiffractionPositions)
				{
					FVector DiffractionVector = DiffractionPoint - EmitterLocation;
					if (float Norm = DiffractionVector.Size(); Norm > KINDA_SMALL_NUMBER)
					{
						VectorEmitterToDiffractionNormed[EmitterIndex].Add(DiffractionVector / Norm);
					}
					else
					{
						VectorEmitterToDiffractionNormed[EmitterIndex].Add(FVector::ZeroVector);
					}
				}
			}			

			UPrimitiveComponent* CurrentPrimitiveComponent = PersistentPrimitiveIndexToPrimitiveComponent.FindChecked(PersistentPrimitiveIndex);
			TArray<AActor*> IgnoreActors;
			IgnoreActors.Add(CurrentPrimitiveComponent->GetOwner());
			IgnoreActors.Add(this);
			FCollisionQueryParams TraceParams(FName(TEXT("DiffractionTrace")), true);
			TraceParams.AddIgnoredActors(IgnoreActors);

			int32 NumReceivers = GeneratedSettings.FinalReceiverPositions.Num();
			
			// ---------------------------------------------------------
			// PARALLELIZED DIFFRACTION PROCESSING
			// ---------------------------------------------------------
			
			// Struct to hold candidate data for parallel processing
			struct FDiffractionCandidate
			{
				int32 SampleIndex;
				int32 TriangleIndex;
				FVector WorldPosition;
				FVector WorldNormal;
			};
			
			// STEP 1: Gather valid candidates (single-threaded with line trace filtering)
			TArray<FDiffractionCandidate> ValidCandidates;
			ValidCandidates.Reserve(DiffractionNormals.Num());
			
			UWorld* CachedWorld = GetWorld();
			for (int32 SampleIndex = 0; SampleIndex < DiffractionNormals.Num(); ++SampleIndex)
			{
				bool ValidLOS = true;
				if (InputSettings->EnableDiffractionLineOfSightRequired)
				{								
					FHitResult HitResult;			
					bool bHit = CachedWorld->LineTraceSingleByChannel(
						HitResult,
						SensorLocation,
						DiffractionPositions[SampleIndex],
						ECC_Visibility,
						TraceParams
					);
					if (bHit)
						ValidLOS = false;
				}
				
				if (ValidLOS)
				{
					// Check angle validity for at least one emitter
					bool PointIsValidOnce = false;
					for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); ++EmitterIndex)
					{
						float DotProduct = FVector::DotProduct(DiffractionNormals[SampleIndex], VectorEmitterToDiffractionNormed[EmitterIndex][SampleIndex]);
						DotProduct = FMath::Clamp(DotProduct, -1.0f, 1.0f);
						float Angle = FMath::RadiansToDegrees(FMath::Acos(DotProduct));
						if (Angle > 90 && Angle < 270)
						{
							PointIsValidOnce = true;
							break;
						}
					}
					
					if (PointIsValidOnce)
					{
						FDiffractionCandidate Cand;
						Cand.SampleIndex = SampleIndex;
						Cand.TriangleIndex = DiffractionTriangleIndexes[SampleIndex];
						Cand.WorldPosition = DiffractionPositions[SampleIndex];
						Cand.WorldNormal = DiffractionNormals[SampleIndex];
						ValidCandidates.Add(Cand);
					}
				}
			}
			
			// STEP 2: Process valid candidates in parallel
			if (ValidCandidates.Num() > 0)
			{
				// Pre-allocate output arrays to worst case size
				TArray<FSonoTraceUEPointStruct> TempResults;
				TempResults.SetNum(ValidCandidates.Num());
				
				TArray<float> TempStrengths;
				TempStrengths.SetNum(ValidCandidates.Num());
				
				// Atomic counter for thread-safe indexing
				volatile int32 ValidPointCount = 0;
				
				// Cache read-only data for thread safety
				const int32 CachedNumEmitters = EmitterPoses.Num();
				const int32 CachedNumReceivers = NumReceivers;
				const int32 CachedNumFrequencies = InputSettings->NumberOfSimFrequencies;
				const float CachedMinStrength = InputSettings->DiffractionMinimumStrength;
				const bool CachedPointsInSensorFrame = InputSettings->PointsInSensorFrame;
				const int32 CachedHitObjectType = HitObjectTypes[HitIndex];
				const FName CachedHitObjectLabel = HitObjectLabels[HitIndex];
				const TArray<float>& CachedFrequencies = GeneratedSettings.Frequencies;
				const TArray<float>& CachedMaterialStrengthsDiffraction = GeneratedSettings.ObjectSettings[CachedHitObjectType].MaterialStrengthsDiffraction;
				
				ParallelFor(ValidCandidates.Num(), [&](int32 Idx)
				{
					const FDiffractionCandidate& Cand = ValidCandidates[Idx];
					const FVector& PointLocation = Cand.WorldPosition;
					const int32 TriangleIndex = Cand.TriangleIndex;
					
					// Calculate emitter distances and angle validity
					TArray<float> VectorEmitterToDiffractionDistance;
					TArray<bool> VectorEmitterValidAngle;
					VectorEmitterToDiffractionDistance.Init(0, CachedNumEmitters);
					VectorEmitterValidAngle.Init(false, CachedNumEmitters);
					
					for (int32 EmitterIndex = 0; EmitterIndex < CachedNumEmitters; ++EmitterIndex)
					{
						float DotProduct = FVector::DotProduct(DiffractionNormals[Cand.SampleIndex], VectorEmitterToDiffractionNormed[EmitterIndex][Cand.SampleIndex]);
						DotProduct = FMath::Clamp(DotProduct, -1.0f, 1.0f);
						float Angle = FMath::RadiansToDegrees(FMath::Acos(DotProduct));
						if (Angle > 90 && Angle < 270)
						{
							VectorEmitterValidAngle[EmitterIndex] = true;
							VectorEmitterToDiffractionDistance[EmitterIndex] = FVector::Dist(PointLocation, EmitterPoses[EmitterIndex].GetLocation());
						}
					}
					
					float DistancePointToSensor = FVector::Dist(PointLocation, SensorLocation);
					
					FSonoTraceUEPointStruct NewPoint;
					NewPoint.TotalDistancesToReceivers.Init(TArray<float>(), CachedNumEmitters);
					NewPoint.Strengths.SetNum(CachedNumEmitters);
					
					float SummedStrength = 0.0f;
					for (int32 EmitterIndex = 0; EmitterIndex < CachedNumEmitters; ++EmitterIndex)
					{
						NewPoint.Strengths[EmitterIndex].Init(TArray<float>(), CachedNumReceivers);
						NewPoint.TotalDistancesToReceivers[EmitterIndex].Init(0, CachedNumReceivers);
						for (int32 ReceiverIndex = 0; ReceiverIndex < CachedNumReceivers; ++ReceiverIndex)
						{
							NewPoint.Strengths[EmitterIndex][ReceiverIndex].Init(0, CachedNumFrequencies);
							for (int32 FreqIndex = 0; FreqIndex < CachedNumFrequencies; ++FreqIndex)
							{
								FVector ReceiverLocation = ReceiverPoses[ReceiverIndex].GetLocation();
								float DistanceEmitterToPoint = (EmitterPoses[EmitterIndex].GetLocation() - PointLocation).Size();
								float DistanceMicToPoint = (ReceiverLocation - PointLocation).Size();
								float FullDistance = DistanceEmitterToPoint + DistanceMicToPoint;
								NewPoint.TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] = FullDistance;
								float FullDistanceMeters = FullDistance / 100.0f;
								float PathLossDiff = 1.0f / FMath::Square(FullDistanceMeters);
								float AlphaAbsorption = 0.038f * (CachedFrequencies[FreqIndex] / 1000.0f) - 0.3f;
								float PathLossAbsorption = FMath::Pow(10.0f, -(AlphaAbsorption * FullDistanceMeters) / 20.0f);
								float Strength = CachedMaterialStrengthsDiffraction[FreqIndex] * PathLossDiff * PathLossAbsorption;
								NewPoint.Strengths[EmitterIndex][ReceiverIndex][FreqIndex] = Strength;
								SummedStrength += Strength * Strength;
							}
						}
					}
					SummedStrength = SummedStrength / CachedNumReceivers / CachedNumEmitters / CachedNumFrequencies;
					
					if (SummedStrength > CachedMinStrength)
					{
						NewPoint.Location = PointLocation;
						NewPoint.ReflectionDirection = VectorEmitterToDiffractionNormed[0][Cand.SampleIndex];
						NewPoint.Label = CachedHitObjectLabel;
						NewPoint.Index = Cand.SampleIndex;
						NewPoint.SummedStrength = SummedStrength;
						NewPoint.TotalDistance = DistancePointToSensor;
						NewPoint.TotalDistancesFromEmitters = MoveTemp(VectorEmitterToDiffractionDistance);
						NewPoint.DistanceToSensor = DistancePointToSensor;
						NewPoint.ObjectTypeIndex = CachedHitObjectType;
						NewPoint.IsHit = true;
						NewPoint.IsLastHit = true;
						NewPoint.CurvatureMagnitude = CurrentMeshData->TriangleCurvatureMagnitude[TriangleIndex];
						NewPoint.SurfaceBRDF = &CurrentMeshData->TriangleBRDF[TriangleIndex];
						NewPoint.SurfaceMaterial = &CurrentMeshData->TriangleMaterial[TriangleIndex];
						NewPoint.IsSpecular = false;
						NewPoint.IsDiffraction = true;
						
						if (CachedPointsInSensorFrame)
						{
							NewPoint.Location = WorldToSensorTransform.TransformPosition(NewPoint.Location);
							NewPoint.ReflectionDirection = WorldToSensorTransform.TransformVector(NewPoint.ReflectionDirection);
						}
						
						// Thread-safe add using atomic counter
						int32 WriteIndex = FPlatformAtomics::InterlockedIncrement(&ValidPointCount) - 1;
						TempResults[WriteIndex] = MoveTemp(NewPoint);
						TempStrengths[WriteIndex] = SummedStrength;
					}
				});
				
				// STEP 3: Finalize - shrink arrays and calculate max stats
				TempResults.SetNum(ValidPointCount);
				TempStrengths.SetNum(ValidPointCount);
				
				// Add results to output (single-threaded)
				for (int32 i = 0; i < ValidPointCount; ++i)
				{
					FSonoTraceUEPointStruct& Point = TempResults[i];
					float PointStrength = TempStrengths[i];
					
					DiffractionSubOutput.ReflectedPoints.Add(MoveTemp(Point));
					DiffractionSubOutput.ReflectedStrengths.Add(PointStrength);
					
					if (PointStrength > DiffractionSubOutput.MaximumStrength)
						DiffractionSubOutput.MaximumStrength = PointStrength;
					if (TempResults[i].CurvatureMagnitude > DiffractionSubOutput.MaximumCurvature)
						DiffractionSubOutput.MaximumCurvature = TempResults[i].CurvatureMagnitude;
					if (TempResults[i].TotalDistance > DiffractionSubOutput.MaximumTotalDistance)
						DiffractionSubOutput.MaximumTotalDistance = TempResults[i].TotalDistance;
				}
			}
		}
		if (InputSettings->EnableSimulationSubOutput)
		{
			CurrentOutput.DiffractionSubOutput = DiffractionSubOutput;
		}
		CurrentOutput.ReflectedPoints.Append(DiffractionSubOutput.ReflectedPoints);
		if (InputSettings->EnableDebugLogExecutionTimes)
			UE_LOG(SonoTraceUE, Log, TEXT("Diffraction component calculation:%.5fs"), FPlatformTime::Seconds() - CurrentTime);
	}	

	CurrentOutput.MaximumCurvature = FMath::Max(DirectPathSubOutput.MaximumCurvature,FMath::Max(RayTracingSubOutput.MaximumCurvature, DiffractionSubOutput.MaximumCurvature));
	CurrentOutput.MaximumStrength = FMath::Max(DirectPathSubOutput.MaximumStrength,FMath::Max(RayTracingSubOutput.MaximumStrength, DiffractionSubOutput.MaximumStrength));
	CurrentOutput.MaximumTotalDistance = FMath::Max(DirectPathSubOutput.MaximumTotalDistance,FMath::Max(RayTracingSubOutput.MaximumTotalDistance, DiffractionSubOutput.MaximumTotalDistance));
	
	CurrentOutput = CurrentOutput;

	if (InterfaceConnected)		
	{
		PrepareInterfaceMeasurementData(CurrentOutput);
	}	
}

void ASonoTraceUEActor::RunSpecularComponentSimulation(FSonoTraceUESubOutputStruct* CurrentRayTracingSubOutput, const USonoTraceUEInputSettingsData* CurrentInputSettings, const FSonoTraceUEGeneratedInputStruct* CurrentGeneratedSettings,
	                                                    FTransform CurrentWorldToSensorTransform, const TArray<FTransform> CurrentEmitterPoses, TArray<FTransform> CurrentReceiverPoses)
{
	CurrentRayTracingSubOutput->ReflectedStrengths.Init(0.0f, CurrentRayTracingSubOutput->ReflectedPoints.Num());
	ParallelFor(CurrentRayTracingSubOutput->ReflectedPoints.Num(), [&](int32 ReflectedPointIndex)
	// for (int32 ReflectedPointIndex = 0; ReflectedPointIndex < CurrentRayTracingSubOutput.ReflectedPoints.Num(); ++ReflectedPointIndex)
	{
		FSonoTraceUEPointStruct& ReflectedPoint = CurrentRayTracingSubOutput->ReflectedPoints[ReflectedPointIndex];	
		if ((ReflectedPoint.IsLastHit && CurrentInputSettings->EnableSpecularSimulationOnlyOnLastHits) || !CurrentInputSettings->EnableSpecularSimulationOnlyOnLastHits)
		{
			ReflectedPoint.TotalDistancesToReceivers.Init(TArray<float>(), CurrentEmitterPoses.Num());
			ReflectedPoint.Strengths.SetNum(CurrentEmitterPoses.Num());
			for (int32 EmitterIndex = 0; EmitterIndex < CurrentEmitterPoses.Num(); ++EmitterIndex)
			{
				ReflectedPoint.Strengths[EmitterIndex].Init(TArray<float>(), CurrentReceiverPoses.Num());
				ReflectedPoint.TotalDistancesToReceivers[EmitterIndex].Init(0, CurrentReceiverPoses.Num());
			}
			
			for (int32 ReceiverIndex = 0; ReceiverIndex < CurrentReceiverPoses.Num(); ++ReceiverIndex)
			{
				if (const FTransform& ReceiverPose = CurrentReceiverPoses[ReceiverIndex]; !ReceiverPose.GetLocation().ContainsNaN())
				{						
					// Calculate the normalized direction vector from the reflection point to the receiver
					FVector VecReceiverToReflection = (ReceiverPose.GetLocation() - ReflectedPoint.Location).GetSafeNormal();
					
					// Calculate receiver directivity strength (Weight = (1-P) + P*cos(theta))
					float ReceiverDirectivity = 1.0f;
					if (CurrentInputSettings->EnableReceiverDirectivity)
					{
						const float RecDot = FVector::DotProduct(-VecReceiverToReflection, ReceiverPose.GetUnitAxis(EAxis::X));							
						 ReceiverDirectivity = (1.0f - CurrentGeneratedSettings->FinalReceiverDirectivities[ReceiverIndex]) + (CurrentGeneratedSettings->FinalReceiverDirectivities[ReceiverIndex] * RecDot);
						 ReceiverDirectivity = FMath::Max(0.0f, ReceiverDirectivity);
					}
	
					// Calculate the angle of reflection (in degrees)
					const float AngleReflection = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ReflectedPoint.ReflectionDirection, VecReceiverToReflection)));
					
					for (int32 EmitterIndex = 0; EmitterIndex < CurrentEmitterPoses.Num(); ++EmitterIndex)
					{					
						
						// Source directivity retrieval
						float SourceDirectivity = 1.0f;
						if (CurrentInputSettings->EnableEmitterDirectivity)
						{
							if (ReflectedPoint.EmitterDirectivities.IsValidIndex(EmitterIndex)) 
							{
								SourceDirectivity = ReflectedPoint.EmitterDirectivities[EmitterIndex];
							}
						}
						
						// Calculate distance to receiver and add it to the total path length (in centimeters)
						const float TotalDistanceToSensor = ReflectedPoint.TotalDistancesFromEmitters[EmitterIndex] + FVector::Distance(ReflectedPoint.Location, ReceiverPose.GetLocation());
						ReflectedPoint.TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] = TotalDistanceToSensor;
	
						// Path loss (geometrical spreading loss) in meters
						const float ReflectionStrengthPathLoss = 1.0f / FMath::Square(TotalDistanceToSensor / 100.0f);
	
						// Loop the simulation frequencies and calculate the specular reflection strength with the BRDF
						ReflectedPoint.Strengths[EmitterIndex][ReceiverIndex].Init(0, CurrentInputSettings->NumberOfSimFrequencies);
						ReflectedPoint.SummedStrength = 0;
						for (int32 FrequencyIndex = 0; FrequencyIndex < CurrentInputSettings->NumberOfSimFrequencies; FrequencyIndex++)
						{
							const float SurfaceBRDF = (*ReflectedPoint.SurfaceBRDF)[FrequencyIndex];
							const float SurfaceBRDFExponent = -1/ (2 * SurfaceBRDF * SurfaceBRDF);
							const float SurfaceMaterial = (*ReflectedPoint.SurfaceMaterial)[FrequencyIndex];
							float AlphaAbsorption = 0.038 * (CurrentGeneratedSettings->Frequencies[FrequencyIndex] / 1000) - 0.3;
							const float PathlossAbsorption = FMath::Pow(10.0f, -(AlphaAbsorption * ReflectedPoint.TotalDistance / 100) / 20);
							const float ReflectionStrengthBRDF = exp(SurfaceBRDFExponent * (AngleReflection * AngleReflection));
							const float Strength = ReflectionStrengthBRDF * ReflectionStrengthPathLoss * SurfaceMaterial * PathlossAbsorption * ReceiverDirectivity * SourceDirectivity;								
							ReflectedPoint.Strengths[EmitterIndex][ReceiverIndex][FrequencyIndex] = Strength;
							ReflectedPoint.SummedStrength += Strength * Strength;
						}
					}
				}					
			}
			ReflectedPoint.SummedStrength = ReflectedPoint.SummedStrength / CurrentReceiverPoses.Num() / CurrentEmitterPoses.Num() / CurrentInputSettings->NumberOfSimFrequencies;
			CurrentRayTracingSubOutput->ReflectedStrengths[ReflectedPointIndex] = ReflectedPoint.SummedStrength;
			if (ReflectedPoint.SummedStrength > CurrentRayTracingSubOutput->MaximumStrength)
				CurrentRayTracingSubOutput->MaximumStrength = ReflectedPoint.SummedStrength;
			if (CurrentInputSettings->PointsInSensorFrame)
			{    
				ReflectedPoint.Location = CurrentWorldToSensorTransform.TransformPosition(ReflectedPoint.Location);
				ReflectedPoint.ReflectionDirection = CurrentWorldToSensorTransform.TransformVector(ReflectedPoint.ReflectionDirection);
			}
		}
	}
	);
	if (CurrentInputSettings->SpecularMinimumStrength > 0.0f)
	{
		for (int32 ReflectedPointIndex = CurrentRayTracingSubOutput->ReflectedPoints.Num() - 1; ReflectedPointIndex >= 0; --ReflectedPointIndex)
		{
			if (CurrentRayTracingSubOutput->ReflectedStrengths[ReflectedPointIndex] < CurrentInputSettings->SpecularMinimumStrength)
			{
				CurrentRayTracingSubOutput->ReflectedPoints.RemoveAt(ReflectedPointIndex);
				CurrentRayTracingSubOutput->ReflectedStrengths.RemoveAt(ReflectedPointIndex);
			}
		}
	}
}

void ASonoTraceUEActor::PrepareInterfaceMeasurementData(const FSonoTraceUEOutputStruct& Output)
{
	if (InterfaceReadyForMessages)
	{
		InterfaceMeasurementDataBuffer.Add(Output);
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_measurement\n"));
		InterfaceMeasurementMessageAnnouncementSent = true;
		InterfaceMeasurementMessageAnnouncementAck = false;
	}
}

void ASonoTraceUEActor::InterfaceOnConnect(const UObjectDelivererProtocol* ClientSocket)
{
	UE_LOG(SonoTraceUE, Log, TEXT("Connected to Interface TCP socket."));
	InterfaceConnected = true;
}

void ASonoTraceUEActor::InterfaceOnDisconnect(const UObjectDelivererProtocol* ClientSocket)
{
	// closed
	UE_LOG(SonoTraceUE, Log, TEXT("Disconnected from interface."));
}

void ASonoTraceUEActor::InterfaceOnReceive(const UObjectDelivererProtocol* ClientSocket, const TArray<uint8>& Buffer)
{
	UE_LOG(SonoTraceUE, Log, TEXT("Data received from interface."));
}

void ASonoTraceUEActor::InterfaceOnReceiveString(const FString& ReceivedString, const UObjectDelivererProtocol* FromObject)
{
	if (ReceivedString == TEXT("sonotraceue_start_no_settings\n"))
	{
		if (InterfaceConnected)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Connected to interface."));
		}else {
			UE_LOG(SonoTraceUE, Warning, TEXT("Connected to interface but this is not the correct connection order!"));
		}
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_start_ack\n"));
		InterfaceReadyForSettings = true;
	}else if (ReceivedString == TEXT("sonotraceue_start_settings\n")) {
		if (InterfaceConnected)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Connected to interface and waiting for input settings."));
		}else {
			UE_LOG(SonoTraceUE, Warning, TEXT("Connected to interface but this is not the correct connection order!"));
		}
		InterfaceSettingsEnabled = true;
		Utf8StringDeliveryBox->Send(TEXT("sonotraceue_start_ack\n"));
	} else if (ReceivedString == TEXT("sonotraceue_ready_settings\n")){		
		if (InterfaceSettingsMessageAnnouncementSent && !InterfaceSettingsMessageAnnouncementAck)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Interface server ready for receiving the settings."));
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Interface server ready for receiving the settings but this is not the correct connection order!"));
		}
		InterfaceSettingsMessageAnnouncementAck = true;
	} else if (ReceivedString == TEXT("sonotraceue_settings_parsed\n")){		
		if (InterfaceSettingsMessageAnnouncementAck)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Settings have been retrieved and parsed by interface server."));
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Interface server has retrieved and parsed the settings but this is not the correct connection order!"));
		}
		InterfaceReadyForMessages = true;
	}else if (ReceivedString == TEXT("sonotraceue_ready_measurement\n"))
	{
		if (InterfaceMeasurementMessageAnnouncementSent && !InterfaceMeasurementMessageAnnouncementAck)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Interface server ready for measurement."));
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Interface server ready for measurement but this is not the correct connection order!"));
		}
		InterfaceMeasurementMessageAnnouncementAck = true;	}else if (ReceivedString == TEXT("sonotraceue_ready_measurement\n"))
	{
		if (InterfaceMeasurementMessageAnnouncementSent && !InterfaceMeasurementMessageAnnouncementAck)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Interface server ready for measurement."));
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Interface server ready for measurement but this is not the correct connection order!"));
		}
		InterfaceMeasurementMessageAnnouncementAck = true;
	}else if (ReceivedString == TEXT("sonotraceue_ready_data\n"))
	{
		if (InterfaceDataMessageAnnouncementSent && !InterfaceDataMessageAnnouncementAck)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Interface server ready for data message."));
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Interface server ready for data message but this is not the correct connection order!"));
		}
		InterfaceDataMessageAnnouncementAck = true;
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_overridetriggeroverride_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_overridetriggeroverride_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);
		
		if (Components.Num() == EmitterPoses.Num())
		{
			TArray<int32> ParsedEmitterSignalIndexes;
			for (int EmitterSignalArrayIndex = 0; EmitterSignalArrayIndex < Components.Num(); EmitterSignalArrayIndex++)
			{
				int32 CurrentParsedEmitterSignalIndex = FCString::Atof(*Components[EmitterSignalArrayIndex]);
				if (CurrentParsedEmitterSignalIndex >= InputSettings->EmitterSignals.Num() || CurrentParsedEmitterSignalIndex < 0)
				{
					UE_LOG(SonoTraceUE, Error, TEXT("Could not trigger measurement with override emitter signal indexes. Invalid override emitter signal index %i for emitter #%i."), CurrentParsedEmitterSignalIndex, EmitterSignalArrayIndex);
					Utf8StringDeliveryBox->Send(TEXT("snok\n"));
					break;
				}
				ParsedEmitterSignalIndexes.Add(CurrentParsedEmitterSignalIndex);
			}
			if (InterfaceReadyForMessages)
			{
				UE_LOG(SonoTraceUE, Log, TEXT("Received trigger measurement with override emitter signal indexes."))
				if (TriggerSimulationOverrideEmitterSignals(ParsedEmitterSignalIndexes))
				{
					Utf8StringDeliveryBox->Send(TEXT("sok\n"));
				}else
				{
					Utf8StringDeliveryBox->Send(TEXT("snok\n"));
				}				
			}else
			{
				UE_LOG(SonoTraceUE, Warning, TEXT("Received trigger measurement with override emitter signal indexes but this is not the correct connection order!"));
				Utf8StringDeliveryBox->Send(TEXT("snok\n"));
			}
		}
		else
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Could not trigger measurement with override emitter signal indexes. Invalid override emitter signal indexes, array is not the same size as the amount of emitters!"));
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}		
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_trigger")))
	{
		if (InterfaceReadyForMessages)
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Received trigger measurement"))
			if (TriggerSimulation())
			{
				Utf8StringDeliveryBox->Send(TEXT("sok\n"));
			}else
			{
				Utf8StringDeliveryBox->Send(TEXT("snok\n"));
			}	
			
		}else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Received trigger measurement but this is not the correct connection order!"));
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_signal_indexes_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_signal_indexes_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);
		
		if (Components.Num() == EmitterPoses.Num())
		{
			TArray<int32> ParsedEmitterSignalIndexes;
			for (int EmitterSignalArrayIndex = 0; EmitterSignalArrayIndex < Components.Num(); EmitterSignalArrayIndex++)
			{
				int32 CurrentParsedEmitterSignalIndex = FCString::Atof(*Components[EmitterSignalArrayIndex]);
				if (CurrentParsedEmitterSignalIndex >= InputSettings->EmitterSignals.Num() || CurrentParsedEmitterSignalIndex < 0)
				{
					UE_LOG(SonoTraceUE, Error, TEXT("Could not set the emitter signal indexes. Invalid override emitter signal index %i for emitter #%i."), CurrentParsedEmitterSignalIndex, EmitterSignalArrayIndex);
					Utf8StringDeliveryBox->Send(TEXT("snok\n"));
					break;
				}
				ParsedEmitterSignalIndexes.Add(CurrentParsedEmitterSignalIndex);
			}
			if (SetCurrentEmitterSignalIndexes(ParsedEmitterSignalIndexes))
			{
				UE_LOG(SonoTraceUE, Log, TEXT("Received new emitter signal indexes for all emitters."))
				Utf8StringDeliveryBox->Send(TEXT("sok\n"));
			}else
			{
				UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new emitter signal indexes for all emitters."))
				Utf8StringDeliveryBox->Send(TEXT("snok\n"));
			}
		}
		else
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Could not set the emitter signal indexes. Invalid override emitter signal indexes, array is not the same size as the amount of emitters!"));
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_specific_emitter_signal_index_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_specific_emitter_signal_index_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);
		
		if (Components.Num() == 2)
		{
			const int32 EmitterIndex = FCString::Atof(*Components[0]);
			const int32 EmitterSignalIndex = FCString::Atof(*Components[1]);
			if (SetCurrentEmitterSignalIndexForSpecificEmitter(EmitterIndex, EmitterSignalIndex)){
				UE_LOG(SonoTraceUE, Log, TEXT("Received new emitter signal index for emitter #%i."), EmitterIndex);
				Utf8StringDeliveryBox->Send(TEXT("sok\n"));
			}else
			{
				UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new emitter signal index for emitter #%i."), EmitterIndex);
				Utf8StringDeliveryBox->Send(TEXT("snok\n"));
			}
		}
		else
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Could not set the emitter signal index for a specific emitter. Expected an emitter index and a emitter signal index."));
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_get_specific_emitter_signal_index_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_get_specific_emitter_signal_index_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);
		
		if (Components.Num() == 1)
		{
			const int32 EmitterIndex = FCString::Atof(*Components[0]);
			int32 EmitterSignalIndex = GetCurrentEmitterSignalIndexForSpecificEmitter(EmitterIndex);
			if (EmitterSignalIndex == -1){
				UE_LOG(SonoTraceUE, Log, TEXT("Failed to get emitter signal index for emitter #%i."), EmitterIndex);
				Utf8StringDeliveryBox->Send(TEXT("snok\n"));
			}else
			{
				UE_LOG(SonoTraceUE, Log, TEXT("Received request for the emitter signal index for emitter #%i."), EmitterIndex);
				Utf8StringDeliveryBox->Send(*FString::Printf(TEXT("sok_%i\n"), EmitterSignalIndex));
			}
		}
		else
		{
			UE_LOG(SonoTraceUE, Error, TEXT("Could not get the emitter signal index for a specific emitter. Expected an emitter index."));
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_get_signal_indexes")))
	{
		TArray<int32> EmitterSignalIndexes = GetCurrentEmitterSignalIndexes();
		FString Result = TEXT("sok");
		for (int32 Index : EmitterSignalIndexes)
		{
			Result += TEXT("_") + FString::FromInt(Index);
		}
		UE_LOG(SonoTraceUE, Log, TEXT("Received request for the emitter signal indexes for all emitters."));
		Result += TEXT("\n");
		Utf8StringDeliveryBox->Send(Result);
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_emitter_positions_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_emitter_positions_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);

		int32 EmitterCount = FCString::Atoi(*Components[0]);
		bool RelativeTransform = FCString::Atoi(*Components[1]) != 0;
		bool ReApplyOffset = FCString::Atoi(*Components[2])!= 0;

		TArray<int32> EmitterIndexes;
		TArray<FVector> NewEmitterPositions;
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterCount; ++EmitterIndex)
		{
			int32 CurrentParsedEmitterIndex = FCString::Atoi(*Components[3 + EmitterIndex]);
			EmitterIndexes.Add(CurrentParsedEmitterIndex);
		}
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterCount; ++EmitterIndex)
		{
			float CurrentParsedPositionX = FCString::Atof(*Components[3 + EmitterCount + (EmitterIndex * 3)]);
			float CurrentParsedPositionY = FCString::Atof(*Components[3 + EmitterCount + 1 + (EmitterIndex * 3)]);
			float CurrentParsedPositionZ = FCString::Atof(*Components[3 + EmitterCount + 2 + (EmitterIndex * 3)]);
			NewEmitterPositions.Add(FVector(CurrentParsedPositionX, CurrentParsedPositionY, CurrentParsedPositionZ));
		}

		if (SetNewEmitterPositions(EmitterIndexes, NewEmitterPositions, RelativeTransform, ReApplyOffset))
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Set new emitter positions for %i emitters."), EmitterCount);
			Utf8StringDeliveryBox->Send(TEXT("sok\n"));
		}
		else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new emitter position for %i emitters."), EmitterCount);
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}	
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_receiver_positions_")))
	{
		FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_receiver_positions_")).Len());
		TArray<FString> Components;
		TransformDataString.ParseIntoArray(Components, TEXT("_"), true);

		int32 ReceiverCount = FCString::Atoi(*Components[0]);
		bool RelativeTransform = FCString::Atoi(*Components[1]) != 0;
		bool ReApplyOffset = FCString::Atoi(*Components[2])!= 0;

		TArray<int32> ReceiverIndexes;
		TArray<FVector> NewReceiverPositions;
		for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverCount; ++ReceiverIndex)
		{
			int32 CurrentParsedReceiverIndex = FCString::Atoi(*Components[3 + ReceiverIndex]);
			ReceiverIndexes.Add(CurrentParsedReceiverIndex);
		}
		for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverCount; ++ReceiverIndex)
		{
			float CurrentParsedPositionX = FCString::Atof(*Components[3 + ReceiverCount + (ReceiverIndex * 3)]);
			float CurrentParsedPositionY = FCString::Atof(*Components[3 + ReceiverCount + 1 + (ReceiverIndex * 3)]);
			float CurrentParsedPositionZ = FCString::Atof(*Components[3 + ReceiverCount + 2 + (ReceiverIndex * 3)]);
			NewReceiverPositions.Add(FVector(CurrentParsedPositionX, CurrentParsedPositionY, CurrentParsedPositionZ));
		}

		if (SetNewReceiverPositions(ReceiverIndexes, NewReceiverPositions, RelativeTransform, ReApplyOffset))
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Set new receiver positions for %i receivers."), ReceiverCount);
			Utf8StringDeliveryBox->Send(TEXT("sok\n"));
		}
		else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new receiver position for %i receivers."), ReceiverCount);
			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
		}			
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_relative_transform_"))){
	    const FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_relative_transform_")).Len());
	    TArray<FString> Components;
	    TransformDataString.ParseIntoArray(Components, TEXT("_"), true);

	    if (Components.Num() == 7)
	    {
	        const float X = FCString::Atof(*Components[0]);
	        const float Y = FCString::Atof(*Components[1]);
	        const float Z = FCString::Atof(*Components[2]);
	        const float Xq = FCString::Atof(*Components[3]);
	        const float Yq = FCString::Atof(*Components[4]);
	        const float Zq = FCString::Atof(*Components[5]);
	        const float Wq = FCString::Atof(*Components[6]);

	        const FRotator NewRotation = FRotator(FQuat(Xq, Yq, Zq, Wq));
	    	const FVector NewTranslation = FVector(X, Y, Z);
	    	
	        if (SetNewSensorRelativeTransform(NewTranslation, NewRotation))
	        {
	            UE_LOG(SonoTraceUE, Log, TEXT("Set new relative sensor transform: Location (%f, %f, %f), Rotation (%f, %f, %f, %f)"), X, Y, Z, Xq, Yq, Zq, Wq);
	        	Utf8StringDeliveryBox->Send(TEXT("sok\n"));
	        }
	        else
	        {
	            UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new relative sensor transform."));
	        	Utf8StringDeliveryBox->Send(TEXT("snok\n"));
	        }
	    }
	    else
	    {
	        UE_LOG(SonoTraceUE, Warning, TEXT("Received invalid relative transform data: %s"), *TransformDataString);
	    	Utf8StringDeliveryBox->Send(TEXT("snok\n"));
	    }
	}else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_owner_transform_"))){
	    const FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_owner_transform_")).Len());
	    TArray<FString> Components;
	    TransformDataString.ParseIntoArray(Components, TEXT("_"), true);

	    if (Components.Num() == 8)
	    {
	    	const float X = FCString::Atof(*Components[0]);
	    	const float Y = FCString::Atof(*Components[1]);
	    	const float Z = FCString::Atof(*Components[2]);
	    	const float Xq = FCString::Atof(*Components[3]);
	    	const float Yq = FCString::Atof(*Components[4]);
	    	const float Zq = FCString::Atof(*Components[5]);
	    	const float Wq = FCString::Atof(*Components[6]);

	    	ETeleportType Teleport = ETeleportType::None;
	    	int32 TeleportValue = FCString::Atoi(*Components[7]);
	    	Teleport = static_cast<ETeleportType>(TeleportValue);

	    	const FRotator NewRotation = FRotator(FQuat(Xq, Yq, Zq, Wq));
	    	const FVector NewTranslation = FVector(X, Y, Z);

	    	if (SetNewSensorOwnerWorldTransform(NewTranslation, NewRotation, Teleport))
	    	{
	    		UE_LOG(SonoTraceUE, Log, TEXT("Set new owner transform: Location (%f, %f, %f), Rotation (%f, %f, %f, %f), TeleportMode: %d"), X, Y, Z, Xq, Yq, Zq, Wq, static_cast<int32>(Teleport));
	    		Utf8StringDeliveryBox->Send(TEXT("sok\n"));
	    	}
	    	else
	    	{
	    		UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new owner transform."));
	    		Utf8StringDeliveryBox->Send(TEXT("snok\n"));
	    	}
	    }
	    else
	    {
	    	UE_LOG(SonoTraceUE, Warning, TEXT("Received invalid owner transform data: %s"), *TransformDataString);
	    	Utf8StringDeliveryBox->Send(TEXT("snok\n"));
	    }
    }else if (ReceivedString.StartsWith(TEXT("sonotraceue_set_sensor_transform_"))){
    	const FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_set_sensor_transform_")).Len());
    	TArray<FString> Components;
    	TransformDataString.ParseIntoArray(Components, TEXT("_"), true);

    	if (Components.Num() == 8)
    	{
    		const float X = FCString::Atof(*Components[0]);
    		const float Y = FCString::Atof(*Components[1]);
    		const float Z = FCString::Atof(*Components[2]);
    		const float Xq = FCString::Atof(*Components[3]);
    		const float Yq = FCString::Atof(*Components[4]);
    		const float Zq = FCString::Atof(*Components[5]);
    		const float Wq = FCString::Atof(*Components[6]);

    		ETeleportType Teleport = ETeleportType::None;
    		int32 TeleportValue = FCString::Atoi(*Components[7]);
    		Teleport = static_cast<ETeleportType>(TeleportValue);

    		const FRotator NewRotation = FRotator(FQuat(Xq, Yq, Zq, Wq));
    		const FVector NewTranslation = FVector(X, Y, Z);


    		if (SetNewSensorWorldTransform(NewTranslation, NewRotation, Teleport))
    		{
    			UE_LOG(SonoTraceUE, Log, TEXT("Set new sensor transform: Location (%f, %f, %f), Rotation (%f, %f, %f, %f), TeleportMode: %d"), X, Y, Z, Xq, Yq, Zq, Wq, static_cast<int32>(Teleport));
    			Utf8StringDeliveryBox->Send(TEXT("sok\n"));
    		}
    		else
    		{
    			UE_LOG(SonoTraceUE, Warning, TEXT("Failed to set new sensor transform."));
    			Utf8StringDeliveryBox->Send(TEXT("snok\n"));
    		}
    	}
    	else
    	{
    		UE_LOG(SonoTraceUE, Warning, TEXT("Received invalid sensor transform data: %s"), *TransformDataString);
    		Utf8StringDeliveryBox->Send(TEXT("snok\n"));
    	}
    }else if (ReceivedString.StartsWith(TEXT("sonotraceue_data_"))){
    	const FString TransformDataString = ReceivedString.RightChop(FString(TEXT("sonotraceue_data_")).Len());
    	TArray<FString> Components;
    	TransformDataString.ParseIntoArray(Components, TEXT("_"), true);
	    if (int32 ParsedInt; !LexTryParseString(ParsedInt, *Components[0]))
    	{
    		UE_LOG(SonoTraceUE, Warning, TEXT("Failed to parse Type"));
	    	Utf8StringDeliveryBox->Send(TEXT("snok0\n"));
    	}
    	else
    	{
    		LatestInterfaceMessageDataType = ParsedInt;   		
    		for (int32 i = 1; i < Components.Num(); i += 2)
    		{
    			if (i + 1 >= Components.Num()) 
    			{
    				UE_LOG(SonoTraceUE, Warning, TEXT("Invalid data format: Type without value at position %d"), i);
    				Utf8StringDeliveryBox->Send(TEXT("snok1\n"));
    				continue;
    			}
    			const FString& DataType = Components[i];
    			const FString& DataValue = Components[i + 1];        
    			if (DataType.Equals(TEXT("S"), ESearchCase::IgnoreCase))
    			{
    				LatestInterfaceMessageDataStrings.Add(DataValue);
    				LatestInterfaceMessageDataOrder.Add(0);
    			}
    			else if (DataType.Equals(TEXT("I"), ESearchCase::IgnoreCase))  
    			{
    				int32 ParsedIntData;
    				if (LexTryParseString(ParsedIntData, *DataValue))
    				{
    					LatestInterfaceMessageDataIntegers.Add(ParsedIntData);
    					LatestInterfaceMessageDataOrder.Add(1); 
    				}
    				else
    				{
    					UE_LOG(SonoTraceUE, Warning, TEXT("Failed to parse Integer: %s"), *DataValue);
    					Utf8StringDeliveryBox->Send(TEXT("snok3\n"));
    				}
    			}
    			else if (DataType.Equals(TEXT("F"), ESearchCase::IgnoreCase))  
    			{
    				float ParsedFloat;
    				if (LexTryParseString(ParsedFloat, *DataValue))
    				{
    					LatestInterfaceMessageDataFloats.Add(ParsedFloat);
    					LatestInterfaceMessageDataOrder.Add(2);
    				}
    				else
    				{
    					UE_LOG(SonoTraceUE, Warning, TEXT("Failed to parse Float: %s"), *DataValue);
    					Utf8StringDeliveryBox->Send(TEXT("snok3\n"));
    				}
    			}
    			else
    			{
    				UE_LOG(SonoTraceUE, Warning, TEXT("Unknown data type identifier: %s"), *DataType);
    				Utf8StringDeliveryBox->Send(TEXT("snok2\n"));
    			}
    		}
    		Utf8StringDeliveryBox->Send(TEXT("sok\n"));
    		UE_LOG(SonoTraceUE, Log, TEXT("Received data message with of type %i, with %i Strings, %i Integers and %i floats."), LatestInterfaceMessageDataType, LatestInterfaceMessageDataStrings.Num(), LatestInterfaceMessageDataIntegers.Num(), LatestInterfaceMessageDataFloats.Num());
    		InterfaceDataMessageReceivedEvent.Broadcast(LatestInterfaceMessageDataType, LatestInterfaceMessageDataOrder, LatestInterfaceMessageDataStrings, LatestInterfaceMessageDataIntegers, LatestInterfaceMessageDataFloats);

    	}   	
	}else
	{
		UE_LOG(SonoTraceUE, Warning, TEXT("Received unknown string from interface: %s"), *ReceivedString);
	}
}

void ASonoTraceUEActor::DrawSimulationResult()
{
	if (CurrentOutput.Index != -1)
	{

		const FTransform SensorToWorldTransform(CurrentOutput.SensorRotation, CurrentOutput.SensorLocation);    
		
		// Draw a coordinate system where the sensor origin is located
		if (InputSettings->EnableDrawSensorPose){
			DrawDebugCoordinateSystem(
				GetWorld(),
				CurrentOutput.SensorLocation,
				CurrentOutput.SensorRotation,
				40.0f,
				false,
				-1,
				0);
		}

		// Draw sensor frustum with white lines
		if (InputSettings->EnableDrawSensorFrustum){
			DrawDebugNonSymmetricalFrustum(GetWorld(), FTransform(CurrentOutput.SensorRotation, CurrentOutput.SensorLocation),
										   InputSettings->SensorLowerAzimuthLimit,
										   InputSettings->SensorUpperAzimuthLimit,
										   InputSettings->SensorLowerElevationLimit,
										   InputSettings->SensorUpperElevationLimit,
										   InputSettings->MaximumRayDistance,
										   FColor::White, false, -1, 0, 1.0f);
		}
		
		// Draw purple points for all the used emitter locations
		if (InputSettings->EnableDrawAllEmitters){
			for (int i = 0; i < EmitterPoses.Num(); i++)
			{
				DrawDebugPoint(
					GetWorld(),
					CurrentOutput.EmitterPoses[i].GetLocation(),
					15.0f,                   
					FColor::Purple,       
					false,                
					-1,                    
					0                   
				);	
			}
		}

		// Calculate originally loaded receiver locations and draw orange points there
		if (InputSettings->EnableDrawLoadedReceivers){		
			for (int i = 0; i < GeneratedSettings.LoadedReceiverPositions.Num(); i++)
			{
				FVector LoadedReceiverPosition;
				if (InputSettings->EnableStaticReceivers)
				{
					if (InputSettings->EnableUseWorldCoordinatesReceivers)
					{
						LoadedReceiverPosition = GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset;
					}else
					{
						LoadedReceiverPosition = StartingActorTransform.TransformPosition(GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset);
					}
				}else
				{
					const FTransform ActorTransform(CurrentOutput.SensorRotation, CurrentOutput.SensorLocation);
					LoadedReceiverPosition = ActorTransform.TransformPosition(GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset);
				}
				
				DrawDebugPoint(
					GetWorld(),
					LoadedReceiverPosition,
					15.0f,                    
					FColor::Orange,       
					false,                
					-1,                    
					0                   
				);	
			}		
		}

		// Draw yellow points for all the used receiver locations
		if (InputSettings->EnableDrawAllReceivers){
			for (int i = 0; i < ReceiverPoses.Num(); i++)
			{
				DrawDebugPoint(
					GetWorld(),
					CurrentOutput.ReceiverPoses[i].GetLocation(),
					10.0f,                   
					FColor::Yellow,       
					false,                
					-1,                    
					0                   
				);	
			}
		}

		if (((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation)) && InputSettings->EnableDrawDirectPathPoints && InputSettings->EnableDirectPathComponentCalculation)
		{
			if (InputSettings->DrawDirectPathPointsMode == ESonoTraceUESimulationDrawDirectPointModeEnum::LOS)
			{
				for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
				{							
					if (CurrentOutput.DirectPathLOS[ReceiverIndex])
					{
						DrawDebugPoint(
							GetWorld(),
							CurrentOutput.ReceiverPoses[ReceiverIndex].GetLocation(),
							20.0f,                   
							FColor::Green,       
							false,                
							-1,                    
							0                   
						);			
					}else
					{
						DrawDebugPoint(
						GetWorld(),
						CurrentOutput.ReceiverPoses[ReceiverIndex].GetLocation(),
						20.0f,                   
						FColor::Red,       
						false,                
						-1,                    
						0                   
						);
					}	
				}	
			}else if (InputSettings->DrawDirectPathPointsMode == ESonoTraceUESimulationDrawDirectPointModeEnum::Strength)
			{
				const TArray<FColor>& ColorMap = FColorMapSelector::GetColorMap(InputSettings->DrawPointsColorMap);

				for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
				{
					float Strength = CurrentOutput.ReflectedPoints[InputSettings->DrawDirectPathPointStrengthEmitterIndex].Strengths[InputSettings->DrawDirectPathPointStrengthEmitterIndex][ReceiverIndex][InputSettings->DrawDirectPathPointStrengthFrequencyIndex];
					
					int32 NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(Strength /  InputSettings->DrawPointsStrengthMaximumValue * 255.0f), 0, 254);
					FColor PointColor = ColorMap[NormalizedValueForColor];					
					DrawDebugPoint(
						GetWorld(),
						CurrentOutput.ReceiverPoses[ReceiverIndex].GetLocation(),
						20.0f,                   
						PointColor,       
						false,                
						-1,                    
						0                   
					);	
				}
			}			
		}
		
		// Plot the results
		if (((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation)) && InputSettings->EnableDrawPoints)
		{

			// Choose a colormap based on InputSettings
			const TArray<FColor>& ColorMap = FColorMapSelector::GetColorMap(InputSettings->DrawPointsColorMap);
			
			const int CurrentOutputSize = CurrentOutput.ReflectedPoints.Num();

			float DrawPointsStrengthMaximumValue = InputSettings->DrawPointsStrengthMaximumValue;
			if (InputSettings->DrawPointsStrengthMaximumAutoScale)
				DrawPointsStrengthMaximumValue = CurrentOutput.MaximumStrength;
			float DrawPointsCurvatureMaximumValue = InputSettings->DrawPointsCurvatureMaximumValue;
			if (InputSettings->DrawPointsCurvatureMaximumAutoScale)
				DrawPointsCurvatureMaximumValue = CurrentOutput.MaximumCurvature;
			float DrawPointsTotalDistanceMaximumValue = InputSettings->DrawPointsTotalDistanceMaximumValue;
			if (InputSettings->DrawPointsTotalDistanceMaximumAutoScale)
				DrawPointsTotalDistanceMaximumValue = CurrentOutput.MaximumTotalDistance;
			if (CurrentOutputSize > 0)
			{
				// Determine how many rays to draw
				const int32 PointsToDraw = FMath::Min<int32>(CurrentOutputSize, InputSettings->MaximumDrawNumber);

				// Index array to store the indices of rays to be drawn	
				TArray<int32> SelectedIndices;
				if (InputSettings->RandomizeDrawSelection)
				{
					FRandomIterator Iterator (PointsToDraw, 0, CurrentOutputSize - 2);
					while(Iterator.HasNext())
					{
						SelectedIndices.Add(Iterator.Next());
					}
				}
				else
				{					
					// Select rays with the calculated step size
					const int32 StepSize = CurrentOutputSize / PointsToDraw;
					for (int32 i = 0; i < CurrentOutputSize; i += StepSize)
					{
						SelectedIndices.Add(i);
						
						// Stop if it has selected the desired number of rays
						if (SelectedIndices.Num() >= PointsToDraw)
						{
							break;
						}
					}
				}		
				for (const int32 PointIndex : SelectedIndices)
				{

					if (PointIndex >= 0 && PointIndex < CurrentOutputSize)
					{						
						FSonoTraceUEPointStruct& Point = CurrentOutput.ReflectedPoints[PointIndex];
						
						if (!Point.IsDirectPath)
						{
								FColor PointColor = InputSettings->DrawDefaultPointsColor;
							float PointSize;		
							int32 NormalizedValueForColor;
							float NormalizedValueForSize; 

							
							switch (InputSettings->DrawPointsColorMode)
							{
							case ESonoTraceUESimulationDrawColorModeEnum::SensorDistance:
								NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(Point.DistanceToSensor / InputSettings->MaximumRayDistance * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							case ESonoTraceUESimulationDrawColorModeEnum::Curvature:
								NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(Point.CurvatureMagnitude / DrawPointsCurvatureMaximumValue * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							case ESonoTraceUESimulationDrawColorModeEnum::Strength:
								NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(Point.SummedStrength / DrawPointsStrengthMaximumValue * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							case ESonoTraceUESimulationDrawColorModeEnum::TotalDistance:{}
								NormalizedValueForColor = FMath::Clamp( FMath::RoundToInt(Point.TotalDistance / DrawPointsTotalDistanceMaximumValue * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							case ESonoTraceUESimulationDrawColorModeEnum::EmitterDirectivity:{}
								NormalizedValueForColor = FMath::Clamp( FMath::RoundToInt(Point.EmitterDirectivities[InputSettings->DrawPointsDirectivityEmitterIndex] / 1 * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							default:
								NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(Point.SummedStrength / DrawPointsStrengthMaximumValue * 255.0f), 0, 254);
								PointColor = ColorMap[NormalizedValueForColor];
								break;
							}

							// Determine size normalization
							switch (InputSettings->DrawPointsSizeMode)
							{
							case ESonoTraceUESimulationDrawSizeModeEnum::Strength:
								NormalizedValueForSize = FMath::Clamp(Point.SummedStrength /  InputSettings->DrawPointsStrengthMaximumValue, 0.0f, 1.0f);
								PointSize = FMath::Lerp(1.0f, InputSettings->DrawDefaultPointsSize, NormalizedValueForSize);
								break;
							default:
								PointSize = InputSettings->DrawDefaultPointsSize;
								break;
							}

							
							FVector DrawLocation = Point.Location;
							if (InputSettings->PointsInSensorFrame)
								DrawLocation = SensorToWorldTransform.TransformPosition(Point.Location);					
											
							// Draw a small point at the location
							DrawDebugPoint(
								GetWorld(),
								DrawLocation,
								PointSize,
								PointColor,       
								false,                
								-1,                    
								0                   
							);	
						}						
					}									
				}
			}
		}
	}
}

void ASonoTraceUEActor::DrawSimulationDebug()
{
	
	const FVector ActorLocation = GetActorLocation();
	const FRotator ActorRotation = GetActorRotation();

	// Draw a coordinate system where the sensor origin is located
	if (InputSettings->EnableDrawDebugSensorPose){
		DrawDebugCoordinateSystem(
			GetWorld(),
			ActorLocation,
			ActorRotation,
			40.0f,
			false,
			-1,
			0);
	}

	// Draw purple points for all the used emitter locations
	if (InputSettings->EnableDrawDebugAllEmitters){
		for (int i = 0; i < EmitterPoses.Num(); i++)
		{
			DrawDebugPoint(
				GetWorld(),
				EmitterPoses[i].GetLocation(),
				6.0f,                   
				FColor::Purple,       
				false,                
				-1,                    
				0                   
			);	
		}
	}
	
	// Draw sensor frustum with white lines
	if (InputSettings->EnableDrawDebugSensorFrustum){
		DrawDebugNonSymmetricalFrustum(GetWorld(), GetActorTransform(),
		                               InputSettings->SensorLowerAzimuthLimit,
		                               InputSettings->SensorUpperAzimuthLimit,
		                               InputSettings->SensorLowerElevationLimit,
		                               InputSettings->SensorUpperElevationLimit,
		                               InputSettings->MaximumRayDistance,
		                               FColor::White, false, -1, 0, 1.0f);
	}

	// Calculate originally loaded receiver locations and draw orange points there
	if (InputSettings->EnableDrawDebugLoadedReceivers){		
		for (int i = 0; i < GeneratedSettings.LoadedReceiverPositions.Num(); i++)
		{

			FVector LoadedReceiverPosition;
			if (InputSettings->EnableStaticReceivers)
			{
				if (InputSettings->EnableUseWorldCoordinatesReceivers)
				{
					LoadedReceiverPosition = GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset;
				}else
				{
					LoadedReceiverPosition = StartingActorTransform.TransformPosition(GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset);
				}
			}else
			{
				const FTransform ActorTransform(CurrentOutput.SensorRotation, CurrentOutput.SensorLocation);
				LoadedReceiverPosition = ActorTransform.TransformPosition(GeneratedSettings.LoadedReceiverPositions[i] + InputSettings->ReceiverPositionsOffset);
			}
			DrawDebugPoint(
				GetWorld(),
				LoadedReceiverPosition,
				4.0f,                    
				FColor::Orange,       
				false,                
				-1,                    
				0                   
			);	
		}		
	}

	// Draw yellow points for all the used receiver locations
	if (InputSettings->EnableDrawDebugAllReceivers){
		for (int i = 0; i < ReceiverPoses.Num(); i++)
		{
			DrawDebugPoint(
				GetWorld(),
				ReceiverPoses[i].GetLocation(),
				3.0f,                   
				FColor::Yellow,       
				false,                
				-1,                    
				0                   
			);	
		}
	}

	// Draw turquoise lines between the sensor and receivers
	if (InputSettings->EnableDrawDebugSensorToReceiverLines && InputSettings->EnableDirectPathComponentCalculation){	
		for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
		{		
			const float Azimuth = DirectPathAzimuthAngles[ReceiverIndex];
			const float Elevation = DirectPathElevationAngles[ReceiverIndex];
			FVector LocalRayDirection(
				FMath::Cos(Elevation) * FMath::Cos(Azimuth), 
				FMath::Cos(Elevation) * FMath::Sin(Azimuth), 
				FMath::Sin(Elevation)                          
			);
			FVector WorldRayDirection = ActorRotation.RotateVector(LocalRayDirection);
			
			FVector ReceiverLocation = ActorLocation + WorldRayDirection * InputSettings->MaximumRayDistance;
						
			DrawDebugLine(
				GetWorld(),
				ActorLocation,
				ReceiverLocation,
				FColor::Turquoise,           
				false,
				-1.0f,
				0,
				1.0f
			);
		}	
	}

	if (InputSettings->EnableDrawDebugDirectPathLOS && InputSettings->EnableDirectPathComponentCalculation){	
		for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverPoses.Num(); ++ReceiverIndex)
		{			
			TTuple<bool, FVector> LOSFoundAndTransformResult = DirectPathReceiverOutput[ReceiverIndex];
			const bool DirectPathResult = LOSFoundAndTransformResult.Get<0>();
			const FVector DirectTransformLocation = LOSFoundAndTransformResult.Get<1>();

			const float Azimuth = DirectPathAzimuthAngles[ReceiverIndex];
			const float Elevation = DirectPathElevationAngles[ReceiverIndex];
			FVector LocalRayDirection(
				FMath::Cos(Elevation) * FMath::Cos(Azimuth), 
				FMath::Cos(Elevation) * FMath::Sin(Azimuth), 
				FMath::Sin(Elevation)                          
			);
			FVector WorldRayDirection = -ActorRotation.RotateVector(LocalRayDirection);			
			
			if (DirectPathResult)
			{
				DrawDebugPoint(
					GetWorld(),
					DirectTransformLocation,
					20.0f,                   
					FColor::Green,       
					false,                
					-1,                    
					0                   
				);

				DrawDebugLine(
					GetWorld(),
					DirectTransformLocation,       
					DirectTransformLocation + WorldRayDirection * 50.0f,  
					FColor::Green,       
					false,               
					-1,                  
					0,                   
					1.0f                 
				);
			}else
			{
				DrawDebugPoint(
					GetWorld(),
					ReceiverPoses[ReceiverIndex].GetLocation(),
					20.0f,                   
					FColor::Red,       
					false,                
					-1,                    
					0                   
				);				

				DrawDebugLine(
					GetWorld(),
					ReceiverPoses[ReceiverIndex].GetLocation(),       
					ReceiverPoses[ReceiverIndex].GetLocation() + WorldRayDirection * 50.0f,  
					FColor::Red,       
					false,               
					-1,                  
					0,                   
					1.0f                 
				);

				if (!isnan(DirectTransformLocation.X))
				{
					DrawDebugPoint(
						GetWorld(),
						DirectTransformLocation,
						20.0f,                   
						FColor::Orange,       
						false,                
						-1,                    
						0
					);
				
					DrawDebugLine(
						GetWorld(),
						DirectTransformLocation,       
						DirectTransformLocation + WorldRayDirection * 50.0f,  
						FColor::Orange,       
						false,               
						-1,                  
						0,                   
						1.0f                 
					);
				}
			}	
		}	
	}
	
	// Plot the ray tracing results
	if (((EnableSimulationEnableOverride && EnableSimulation) || (!EnableSimulationEnableOverride && InputSettings->EnableSimulation)) && InputSettings->EnableRaytracing && InputSettings->EnableDrawDebugPoints)
	{
		// Validate that the saved data is valid
		if (!RayTracingRawOutput)
			return;

		// Determine how many rays to draw
		const int32 RaysToDraw = FMath::Min<int32>(GeneratedSettings.AzimuthAngles.Num(), InputSettings->MaximumDrawDebugRaysNumber);

		// Index array to store the indices of rays to be drawn	
		TArray<int32> SelectedIndices;
		if (InputSettings->RandomizeDrawDebugRaysSelection)
		{
			FRandomIterator Iterator (RaysToDraw, 0, GeneratedSettings.AzimuthAngles.Num() - 2);
			while(Iterator.HasNext())
			{
				SelectedIndices.Add(Iterator.Next());
			}
		}
		else
		{
			// Calculate the step size to evenly decimate the rays
			const int32 StepSize = GeneratedSettings.AzimuthAngles.Num() / RaysToDraw;

			// Select rays with the calculated step size
			for (int32 i = 0; i < GeneratedSettings.AzimuthAngles.Num(); i += StepSize)
			{
				// Add the index to the selection
				SelectedIndices.Add(i);

				// Stop if it has selected the desired number of rays
				if (SelectedIndices.Num() >= RaysToDraw)
				{
					break;
				}
			}
		}

		// Draw debug lines for the selected rays
		for (const int32 RayIndex : SelectedIndices)
		{
			if (RayIndex >= 0 && RayIndex < GeneratedSettings.AzimuthAngles.Num())
			{
				// Get azimuth and elevation from saved data
				const float Azimuth = GeneratedSettings.AzimuthAngles[RayIndex];
				const float Elevation = GeneratedSettings.ElevationAngles[RayIndex];
				
				// Get the local ray direction in the sensor's coordinate space
				FVector LocalRayDirection(
					FMath::Cos(Elevation) * FMath::Cos(Azimuth), 
					FMath::Cos(Elevation) * FMath::Sin(Azimuth), 
					FMath::Sin(Elevation)                          
				);
				
				// Transform a local direction to world space using the sensor's rotation at save
				FVector WorldRayDirection = ActorRotation.RotateVector(LocalRayDirection);
				
				// Compute ray origin position (sensor's position at save in world coordinates)
				FVector RayOrigin = ActorLocation;
				
				// Start multi-bounce loop
				FVector CurrentOrigin = SensorLocation;
				for (int32 BounceIndex = 0; BounceIndex < InputSettings->MaximumBounces; BounceIndex++)
				{
					// Compute the output index for this ray and this bounce
					int32 OutputIndex = RayIndex * InputSettings->MaximumBounces + BounceIndex;
				
					// Access the ray tracing output element
					const FStructuredOutputBufferElem& CurrentRayTracingOutput = RayTracingRawOutput[OutputIndex];
			
					// Check whether the ray hit something			
					if (CurrentRayTracingOutput.IsHit)
					{								
						// Compute the hit location in world space
						FVector HitLocation = FVector(CurrentRayTracingOutput.HitPosX,
													  CurrentRayTracingOutput.HitPosY,
													  CurrentRayTracingOutput.HitPosZ);

						// Draw a line between the ray origin and the hit location 
						if (InputSettings->EnableDrawDebugRaysHitLines)
						{
							// Draw the normal direction line in blue
							DrawDebugLine(
								GetWorld(),
								CurrentOrigin,       // Start at the ray origin
								HitLocation,         // End at the hit location
								FColor::Green,      
								false,             
								-1,                 
								0,                  
								1.5f                
							);
						}

						// Prepare for the next iteration
						CurrentOrigin = HitLocation;
				
						// Draw a small point at the hit location
						if (InputSettings->EnableDrawDebugRaysHitPoints)
						{
							DrawDebugPoint(
								GetWorld(),
								HitLocation,
								10.0f,
								FColor::Green,       
								false,                
								-1,                    
								0                   
							);
						}			
			
						if (InputSettings->EnableDrawDebugRaysLastHitReflection)
						{
							bool DrawHitReflection = false;
							if (BounceIndex == InputSettings->MaximumBounces - 1)
							{
								DrawHitReflection = true;
							}
							else if (!RayTracingRawOutput[OutputIndex + 1].IsHit)					
							{
								DrawHitReflection = true;				
							}
					
							if (DrawHitReflection)
							{
								// Draw the world normal at the hit point
								FVector HitReflectionDirection(CurrentRayTracingOutput.HitReflectionX,
													CurrentRayTracingOutput.HitReflectionY,
													CurrentRayTracingOutput.HitReflectionZ);
								HitReflectionDirection.Normalize(); // Ensure the normal is a unit vector
				
								// Define the endpoint for the normal's debug line (5 cm in normal's direction)
								FVector NormalLineEnd = HitLocation + HitReflectionDirection * 50.0f;
			
								// Draw the normal direction line in blue
								DrawDebugLine(
									GetWorld(),
									HitLocation,         // Start at the hit location
									NormalLineEnd,       // End 10 cm away in the normal direction
									FColor::Blue,       
									false,               
									-1,                  
									0,                   
									1.5f                 
								);
							}				
						}			
					}
					else
					{
						if (InputSettings->EnableDrawDebugRaysInitialMisses && BounceIndex == 0)
						{
							// Use the max trace distance to represent non-hits
							FVector MissLocation = RayOrigin + WorldRayDirection * InputSettings->MaximumRayDistance;
						
							// Draw the miss ray in red
							DrawDebugLine(
								GetWorld(),
								RayOrigin,
								MissLocation,
								FColor::Red,           
								false,
								-1.0f,
								0,
								1.0f
							);
						}
						break;
					}
				}
			}			
		}
	}
}

void ASonoTraceUEActor::DrawMeshDebug(const UMeshComponent* MeshComponent, FSonoTraceUEMeshDataStruct& NewMeshData) const
{
	if (InputSettings->EnableDrawDebugMeshData)
	{
		const int32 TrianglesToDrawActual = NewMeshData.TrianglePosition.Num();
		const int32 TrianglesToDraw = FMath::Min<int32>(TrianglesToDrawActual, InputSettings->DrawDebugMeshMaximumPoints);

		TArray<int32> SelectedIndices;
		SelectedIndices.Empty();
		if (InputSettings->RandomizeDrawDebugMeshData)
		{
			FRandomIterator Iterator (TrianglesToDraw, 0, TrianglesToDrawActual - 2);
			while(Iterator.HasNext())
			{
				SelectedIndices.Add(Iterator.Next());
			}
		}
		else
		{
			const int32 StepSize =TrianglesToDrawActual / TrianglesToDraw;
			for (int32 i = 0; i < TrianglesToDrawActual; i += StepSize)
			{
				SelectedIndices.Add(i);
				if (SelectedIndices.Num() >= TrianglesToDraw)
				{
					break;
				}
			}
		}

		const TArray<FColor>& ColorMap = FColorMapSelector::GetColorMap(InputSettings->DrawDebugMeshColorMap);

		for (const int32 SelectedTriangleIndex : SelectedIndices)
		{
			if (SelectedTriangleIndex < TrianglesToDrawActual && SelectedTriangleIndex >= 0)
			{
				int32 NormalizedValueForColor;
				FColor PointColor;

				switch (InputSettings->DrawDebugMeshColorMode){
					case ESonoTraceUEMeshModeEnum::Curvature:
					    {
					        const float PointCurvatureValue = NewMeshData.TriangleCurvatureMagnitude[SelectedTriangleIndex];					        
					        const float MinLimit = InputSettings->DrawDebugMeshCurvatureLimits.X;
					        const float MaxLimit = InputSettings->DrawDebugMeshCurvatureLimits.Y;
					        const float ClampedValue = FMath::Max(PointCurvatureValue - MinLimit, 0.0f);  
					        NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(ClampedValue / (MaxLimit - MinLimit) * 255.0f), 0, 254);
					        PointColor = ColorMap[NormalizedValueForColor];
					        break;
					    }
					case ESonoTraceUEMeshModeEnum::SurfaceBRDF:
					    {
					        const float PointOpeningAngleValue = NewMeshData.TriangleBRDF[SelectedTriangleIndex][InputSettings->DrawDebugMeshOpeningAngleFrequencyIndex];					        
					        const float MinLimit = InputSettings->DrawDebugMeshOpeningAngleLimits.X;
					        const float MaxLimit = InputSettings->DrawDebugMeshOpeningAngleLimits.Y;
					        const float ClampedValue = FMath::Max(PointOpeningAngleValue - MinLimit, 0.0f);
					        NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(ClampedValue / (MaxLimit - MinLimit) * 255.0f), 0, 254);
					        PointColor = ColorMap[NormalizedValueForColor];
					        break;
					    }
					case ESonoTraceUEMeshModeEnum::SurfaceMaterial:
					    {
					        const float PointReflectionStrength = NewMeshData.TriangleMaterial[SelectedTriangleIndex][InputSettings->DrawDebugMeshOpeningAngleFrequencyIndex];					        
					        const float MinLimit = InputSettings->DrawDebugMeshReflectionStrengthLimits.X;
					        const float MaxLimit = InputSettings->DrawDebugMeshReflectionStrengthLimits.Y;
					        const float ClampedValue = FMath::Max(PointReflectionStrength - MinLimit, 0.0f);
					        NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(ClampedValue / (MaxLimit - MinLimit) * 255.0f), 0, 254);
					        PointColor = ColorMap[NormalizedValueForColor];
					        break;
					    }        
					case ESonoTraceUEMeshModeEnum::Normal:
					    {
					        const FVector PointNormal = NewMeshData.TriangleNormal[SelectedTriangleIndex];
					        PointColor = FColor(static_cast<uint8>((PointNormal.X + 1.0f) * 127.5f), 
					                            static_cast<uint8>((PointNormal.Y + 1.0f) * 127.5f), 
					                            static_cast<uint8>((PointNormal.Z + 1.0f) * 127.5f));
					        break;
					    }
					case ESonoTraceUEMeshModeEnum::Size:
					    {
					        const float PointAreaSize = NewMeshData.TriangleSize[SelectedTriangleIndex];					        
					        const float MinLimit = InputSettings->DrawDebugMeshTriangleSizeLimits.X;
					        const float MaxLimit = InputSettings->DrawDebugMeshTriangleSizeLimits.Y;
					        const float ClampedValue = FMath::Max(PointAreaSize - MinLimit, 0.0f);
					        NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(ClampedValue / (MaxLimit - MinLimit) * 255.0f), 0, 254);
					        PointColor = ColorMap[NormalizedValueForColor];
					        break;
					    }
					default:
					    {
					        const float DefaultV = NewMeshData.TriangleCurvatureMagnitude[SelectedTriangleIndex];					        
					        const float MinLimit = InputSettings->DrawDebugMeshTriangleSizeLimits.X;
					        const float MaxLimit = InputSettings->DrawDebugMeshTriangleSizeLimits.Y;
					        const float ClampedValue = FMath::Max(DefaultV - MinLimit, 0.0f);
					        NormalizedValueForColor = FMath::Clamp(FMath::RoundToInt(ClampedValue / (MaxLimit - MinLimit) * 255.0f), 0, 254);
					        PointColor = ColorMap[NormalizedValueForColor];
					        break;
					    }
				}			

				FVector DrawLocation = NewMeshData.TrianglePosition[SelectedTriangleIndex];
				DrawLocation = MeshComponent->GetComponentTransform().TransformPosition(DrawLocation);	
				DrawDebugPoint(
					GetWorld(),
					DrawLocation,
					InputSettings->DrawDebugMeshSize,
					PointColor,       
					true,                
					-1,                    
					0                   
				);		
			}			
		}
	}
}

void ASonoTraceUEActor::MergeEmitterPatternImpulseResponses(const int32 OriginalReceiverCount, const int32 NewReceiverCount, const int NumberOfIRSamples, TArray<TArray<float>>* ImpulseResponses){

	TArray<TArray<float>> SummedImpulseResponses;
	SummedImpulseResponses.Init(TArray<float>(), OriginalReceiverCount);
	
	// Iterate through original receivers
	for (int32 OriginalReceiverIndex = 0; OriginalReceiverIndex < OriginalReceiverCount; ++OriginalReceiverIndex)
	{
		// Prepare storage for summation per original receiver
		SummedImpulseResponses[OriginalReceiverIndex].Init(0.0f, NumberOfIRSamples);

		// Iterate over the new receivers corresponding to the current original receiver
		for (int32 ReceiverOffset = 0; ReceiverOffset < NewReceiverCount / OriginalReceiverCount; ++ReceiverOffset)
		{
			// Calculate the index of the virtual receiver
			int32 VirtualReceiverIndex = OriginalReceiverIndex * (NewReceiverCount / OriginalReceiverCount) + ReceiverOffset;

			// Sum up impulse responses sample by sample
			for (int32 SampleIndex = 0; SampleIndex < NumberOfIRSamples; ++SampleIndex)
			{
				SummedImpulseResponses[OriginalReceiverIndex][SampleIndex] += (*ImpulseResponses)[VirtualReceiverIndex][SampleIndex];
			}
		}
	}
	// Copy the summed impulse responses to the output
	*ImpulseResponses = SummedImpulseResponses;
}

TArray<float> ASonoTraceUEActor::Interpolate(const TArray<float>& X, const TArray<float>& Y, const TArray<float>& Xq)
{
	TArray<float> Yq;
	Yq.Init(9, Xq.Num());

	// Ensure input is valid (X and Y must have the same size)
	if (X.Num() != Y.Num() || X.Num() < 2)
	{
		// Invalid input, return an empty array
		Yq.Empty();
		return Yq;
	}

	for (int32 i = 0; i < Xq.Num(); ++i)
	{
		float QueryX = Xq[i];

		// Handle extrapolation on the left
		if (QueryX <= X[0])
		{
			Yq[i] = Y[0];
			continue;
		}

		// Handle extrapolation on the right
		if (QueryX >= X.Last())
		{
			Yq[i] = Y.Last();
			continue;
		}

		// Perform linear interpolation
		for (int32 j = 0; j < X.Num() - 1; ++j)
		{
			if (QueryX >= X[j] && QueryX <= X[j + 1])
			{
				float T = (QueryX - X[j]) / (X[j + 1] - X[j]); // Normalized position between X[j] and X[j+1]
				Yq[i] = FMath::Lerp(Y[j], Y[j + 1], T);        // Interpolate corresponding Y values
				break;
			}
		}
	}

	return Yq;
}

TArray<float> ASonoTraceUEActor::Convolve(const TArray<float>& Signal1, const TArray<float>& Signal2, bool bSame)
{
	const int32 Sig1Len = Signal1.Num();
	const int32 Sig2Len = Signal2.Num();
	const int32 ConvLen = Sig1Len + Sig2Len - 1;

	TArray<float> Result;
	Result.SetNum(ConvLen);

	// Perform convolution
	for (int32 i = 0; i < ConvLen; ++i)
	{
		float Sum = 0.0f;
		for (int32 j = 0; j < Sig2Len; ++j)
		{
			if (i - j >= 0 && i - j < Sig1Len)
			{
				Sum += Signal1[i - j] * Signal2[j];
			}
		}
		Result[i] = Sum;
	}

	// If 'same' option equivalent in MATLAB is desired, center the result
	if (bSame)
	{
		const int32 Start = (ConvLen - Sig1Len) / 2;
		return TArray(Result.GetData() + Start, Sig1Len);
	}

	return Result;
}

void ASonoTraceUEActor::CircShift(TArray<float>& Signal, int32 Shift)
{
	const int32 Len = Signal.Num();
	Shift = (Shift % Len + Len) % Len; // Ensure a positive shift value
	if (Shift == 0) return;

	TArray<float> Temp = Signal;
	for (int32 i = 0; i < Len; ++i)
	{
		Signal[i] = Temp[(i - Shift + Len) % Len];
	}
}

TArray<float> ASonoTraceUEActor::NormLog(const TArray<float>& MatIn, float ThreshDB)
{
	// Calculate the threshold and the max value in the input array
	const float Thresh = FMath::Pow(10.0f, ThreshDB / 20.0f);
	
	// Find the maximum absolute value in a single loop
	float MaxValue = 1e-6f; // Prevent division by zero
	for (const float Value : MatIn)
	{
		MaxValue = FMath::Max(MaxValue, FMath::Abs(Value));
	}

	// Transform the array using a single loop
	TArray<float> MatOut;
	MatOut.Reserve(MatIn.Num());

	for (float Value : MatIn)
	{
		// Normalize, clamp, and apply log transformation
		float NormalizedValue = FMath::Max(Value / MaxValue, 0.0f); // Clamp negative to 0
		MatOut.Add(20.0f * FMath::LogX(10.0f, NormalizedValue + Thresh));
	}

	return MatOut;
}

FString ASonoTraceUEActor::DebugRunMeshPreprocessingTest(int32 NumberOfTriangles, int32 NumberOfFrequencies, const FString& TestDescription, bool bSaveToFile, const FString& FilePath, bool bAppendToFile)
{
	const FString TestTimestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	float CurvatureCalculationTimeMs = 0.0f;
	float BRDFMaterialCalculationTimeMs = 0.0f;
	float TotalCalculationTimeMs = 0.0f;
	float MemoryUsedMB = 0.0f;

	int32 ResolvedFrequencyCount = NumberOfFrequencies;

	UE_LOG(SonoTraceUE, Log, TEXT("===== Starting Mesh Preprocessing Test ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("Description: %s"), *TestDescription);
	UE_LOG(SonoTraceUE, Log, TEXT("Timestamp: %s"), *TestTimestamp);
	UE_LOG(SonoTraceUE, Log, TEXT("Parameters: Triangles=%d, Frequencies=%d"), NumberOfTriangles, ResolvedFrequencyCount);

	FSonoTraceUEMeshDataStruct TestMeshData;
	FSonoTraceUEObjectSettingsStruct TestObjectSettings;
	TestObjectSettings.BrdfTransitionPosition = 0.4f;
	TestObjectSettings.BrdfTransitionSlope = 2.0f;
	TestObjectSettings.MaterialsTransitionPosition = 0.4f;
	TestObjectSettings.MaterialsTransitionSlope = 2.0f;
	const float BrdfSpecStart = InputSettings ? InputSettings->ObjectSettingsDefault.BrdfExponentSpecularStart : 8.0f;
	const float BrdfDiffStart = InputSettings ? InputSettings->ObjectSettingsDefault.BrdfExponentDiffractionStart : 70.0f;
	const float MatSpecStart = InputSettings ? InputSettings->ObjectSettingsDefault.MaterialStrengthSpecularStart : 10.0f;
	const float MatDiffStart = InputSettings ? InputSettings->ObjectSettingsDefault.MaterialStrengthDiffractionStart : 0.025f;
	TestObjectSettings.BrdfExponentsSpecular.Init(BrdfSpecStart, ResolvedFrequencyCount);
	TestObjectSettings.BrdfExponentsDiffraction.Init(BrdfDiffStart, ResolvedFrequencyCount);
	TestObjectSettings.MaterialStrengthsSpecular.Init(MatSpecStart, ResolvedFrequencyCount);
	TestObjectSettings.MaterialStrengthsDiffraction.Init(MatDiffStart, ResolvedFrequencyCount);
	TestObjectSettings.DefaultTriangleBRDF.Init(1.0f, ResolvedFrequencyCount);
	TestObjectSettings.DefaultTriangleMaterial.Init(1.0f, ResolvedFrequencyCount);

	UE_LOG(SonoTraceUE, Log, TEXT("Generating synthetic mesh data (simulating CalculateMeshCurvature operations)..."));
	const float GridSize = FMath::Sqrt(static_cast<float>(NumberOfTriangles) / 2.0f); 
	const int32 QuadsPerSide = FMath::CeilToInt(GridSize);
	const float QuadSize = 100.0f;
	
	UE::Geometry::FDynamicMesh3 TestMesh;
	TestMesh.EnableVertexNormals(FVector3f::UpVector);
	
	for (int32 Y = 0; Y <= QuadsPerSide; ++Y)
	{
		for (int32 X = 0; X <= QuadsPerSide; ++X)
		{
			const float PosX = X * QuadSize;
			const float PosY = Y * QuadSize;
			const float PosZ = FMath::Sin(X * 0.3f) * FMath::Cos(Y * 0.3f) * 20.0f;
			
			FVector3f Normal(FMath::Cos(X * 0.3f) * FMath::Cos(Y * 0.3f), 
			                 FMath::Sin(X * 0.3f) * FMath::Sin(Y * 0.3f), 
			                 1.0f);
			Normal.Normalize();
			
			int32 VertexID = TestMesh.AppendVertex(FVector3d(PosX, PosY, PosZ));
			TestMesh.SetVertexNormal(VertexID, Normal);
		}
	}
	
	int32 TriangleCount = 0;
	for (int32 Y = 0; Y < QuadsPerSide && TriangleCount < NumberOfTriangles; ++Y)
	{
		for (int32 X = 0; X < QuadsPerSide && TriangleCount < NumberOfTriangles; ++X)
		{
			const int32 V00 = Y * (QuadsPerSide + 1) + X;
			const int32 V10 = Y * (QuadsPerSide + 1) + X + 1;
			const int32 V01 = (Y + 1) * (QuadsPerSide + 1) + X;
			const int32 V11 = (Y + 1) * (QuadsPerSide + 1) + X + 1;
			
			if (TriangleCount < NumberOfTriangles)
			{
				TestMesh.AppendTriangle(V00, V10, V01);
				TriangleCount++;
			}
			if (TriangleCount < NumberOfTriangles)
			{
				TestMesh.AppendTriangle(V10, V11, V01);
				TriangleCount++;
			}
		}
	}
	UE_LOG(SonoTraceUE, Log, TEXT("Built FDynamicMesh3 with %d vertices and %d triangles"), TestMesh.VertexCount(), TestMesh.TriangleCount());
	
	const int64 N = static_cast<int64>(NumberOfTriangles);
	const int64 F = static_cast<int64>(ResolvedFrequencyCount);
	const int64 SizeOfFloat = sizeof(float);                 
	const int64 SizeOfInt32 = sizeof(int32);                    
	const int64 SizeOfFVector = sizeof(FVector);        
	const int64 TArrayOverhead = sizeof(TArray<float>);    	

	const int64 MemTriangleCurvatureMagnitude = N * SizeOfFloat + TArrayOverhead;
	const int64 MemTriangleSize = N * SizeOfFloat + TArrayOverhead;
	const int64 MemTriangleBRDF = N * F * SizeOfFloat + N * TArrayOverhead + TArrayOverhead; 
	const int64 MemTriangleMaterial = N * F * SizeOfFloat + N * TArrayOverhead + TArrayOverhead;
	const int64 MemTriangleNormal = N * SizeOfFVector + TArrayOverhead;
	const int64 MemTrianglePosition = N * SizeOfFVector + TArrayOverhead;
	const int64 MemImportanceVertexOrderedBRDFValue = N * SizeOfFloat + TArrayOverhead;
	const int64 MemImportanceVertexOrderedIndex = N * SizeOfInt32 + TArrayOverhead;	
	const int64 TotalCalculatedMemoryBytes = MemTriangleCurvatureMagnitude + MemTriangleSize +
		MemTriangleBRDF + MemTriangleMaterial + MemTriangleNormal + MemTrianglePosition +
		MemImportanceVertexOrderedBRDFValue + MemImportanceVertexOrderedIndex;	
	MemoryUsedMB = static_cast<float>(TotalCalculatedMemoryBytes) / (1024.0f * 1024.0f);
	
	UE_LOG(SonoTraceUE, Log, TEXT("Calculated memory consumption for FSonoTraceUEMeshDataStruct:"));
	UE_LOG(SonoTraceUE, Log, TEXT("%lld bytes (%.3f MB)"), TotalCalculatedMemoryBytes, MemoryUsedMB);
	
	const float CurvatureScaleFactor = InputSettings ? InputSettings->CurvatureScale : 1.0f;
	const bool EnableCurvatureScaler = InputSettings ? InputSettings->EnableCurvatureTriangleSizeBasedScaler : true;
	const float CurvatureScalerMin = InputSettings ? InputSettings->CurvatureScalerMinimumEffect : 0.02f;
	const float CurvatureScalerMax = InputSettings ? InputSettings->CurvatureScalerMaximumEffect : 2.0f;
	const float CurvatureScalerLowerThreshold = InputSettings ? InputSettings->CurvatureScalerLowerTriangleSizeThreshold : 0.5f;
	const float CurvatureScalerUpperThreshold = InputSettings ? InputSettings->CurvatureScalerUpperTriangleSizeThreshold : 5.0f;
	const float DiffractionThreshold = InputSettings ? InputSettings->DiffractionTriangleSizeThreshold : 200.0f;

	UE_LOG(SonoTraceUE, Log, TEXT("Starting curvature calculation test (using real UE::MeshCurvature::MeanCurvatureNormal)..."));
	const double CurvatureStartTime = FPlatformTime::Seconds();
	
	for (int32 TriangleID : TestMesh.TriangleIndicesItr())
	{
		UE::Geometry::FIndex3i TriVertices = TestMesh.GetTriangle(TriangleID);
		
		FVector3d Vertex1Position = TestMesh.GetVertex(TriVertices.A);
		FVector3d Vertex2Position = TestMesh.GetVertex(TriVertices.B);
		FVector3d Vertex3Position = TestMesh.GetVertex(TriVertices.C);
		
		FVector3d TrianglePosition = (Vertex1Position + Vertex2Position + Vertex3Position) / 3.0;
		TestMeshData.TrianglePosition.Add(FVector(TrianglePosition));
		
		FVector3f Vertex1Normal = TestMesh.GetVertexNormal(TriVertices.A);
		
		FVector Edge1 = FVector(Vertex2Position) - FVector(Vertex1Position);
		FVector Edge2 = FVector(Vertex3Position) - FVector(Vertex1Position);
		FVector TriangleNormal = FVector::CrossProduct(Edge1, Edge2);
		
		float DotNormal = FVector::DotProduct(TriangleNormal, FVector(Vertex1Normal));
		if (DotNormal < 0.0f)
			TriangleNormal = -TriangleNormal;
		TestMeshData.TriangleNormal.Add(TriangleNormal);
		
		float TriangleSize = FVector3d::CrossProduct(Vertex2Position - Vertex1Position, Vertex3Position - Vertex1Position).Length() * 0.5;
		TestMeshData.TriangleSize.Add(TriangleSize);
		
		FVector3d Vertex1CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(TestMesh, TriVertices.A);
		FVector3d Vertex2CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(TestMesh, TriVertices.B);
		FVector3d Vertex3CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(TestMesh, TriVertices.C);
		
		double CurvatureMag1 = Vertex1CurvatureNormal.Length() / 2;
		double CurvatureMag2 = Vertex2CurvatureNormal.Length() / 2;
		double CurvatureMag3 = Vertex3CurvatureNormal.Length() / 2;
		
		double MaxCurvature = FMath::Max3(CurvatureMag1, CurvatureMag2, CurvatureMag3);
		double MinCurvature = FMath::Min3(CurvatureMag1, CurvatureMag2, CurvatureMag3);
		double CurvatureRange = MaxCurvature - MinCurvature;
		
		double MeanCurvatureNormalValue = CurvatureRange;
		
		if (EnableCurvatureScaler)
		{
			float ScaledAreaEffect = 0;
			if (TriangleSize <= CurvatureScalerLowerThreshold)
			{
				ScaledAreaEffect = CurvatureScalerMin + (1 - CurvatureScalerMin) / CurvatureScalerLowerThreshold * TriangleSize;
			}
			else if (TriangleSize > CurvatureScalerLowerThreshold && TriangleSize <= CurvatureScalerUpperThreshold)
			{
				ScaledAreaEffect = 1 + (CurvatureScalerMax - 1) / (CurvatureScalerUpperThreshold - CurvatureScalerLowerThreshold) * (TriangleSize - CurvatureScalerLowerThreshold);
			}
			else if (TriangleSize > CurvatureScalerUpperThreshold && TriangleSize <= DiffractionThreshold)
			{
				ScaledAreaEffect = CurvatureScalerMax - CurvatureScalerMax / (DiffractionThreshold - CurvatureScalerUpperThreshold) * (TriangleSize - CurvatureScalerUpperThreshold);
			}
			MeanCurvatureNormalValue = MeanCurvatureNormalValue * ScaledAreaEffect;
		}
		
		TestMeshData.TriangleCurvatureMagnitude.Add(MeanCurvatureNormalValue * CurvatureScaleFactor);
	}	
	
	const double CurvatureEndTime = FPlatformTime::Seconds();
	CurvatureCalculationTimeMs = static_cast<float>((CurvatureEndTime - CurvatureStartTime) * 1000.0);	
	UE_LOG(SonoTraceUE, Log, TEXT("Curvature calculation completed in %.3f ms"), CurvatureCalculationTimeMs);

	UE_LOG(SonoTraceUE, Log, TEXT("Starting BRDF and Material calculation test..."));
	const double BRDFStartTime = FPlatformTime::Seconds();
	
	GenerateBRDFAndMaterial(&TestObjectSettings, &TestMeshData);	
	const double BRDFEndTime = FPlatformTime::Seconds();
	BRDFMaterialCalculationTimeMs = static_cast<float>((BRDFEndTime - BRDFStartTime) * 1000.0);	
	UE_LOG(SonoTraceUE, Log, TEXT("BRDF and Material calculation completed in %.3f ms"), BRDFMaterialCalculationTimeMs);

	TotalCalculationTimeMs = CurvatureCalculationTimeMs + BRDFMaterialCalculationTimeMs;
	FString CSVHeader = TEXT("Timestamp,Description,TriangleCount,FrequencyCount,CurvatureTimeMs,BRDFMaterialTimeMs,TotalTimeMs,MemoryUsedMB\n");
	FString CSVRow = FString::Printf(TEXT("%s,\"%s\",%d,%d,%.15f,%.15f,%.15f,%.15f\n"),
		*TestTimestamp,
		*TestDescription,
		NumberOfTriangles,
		ResolvedFrequencyCount,
		CurvatureCalculationTimeMs,
		BRDFMaterialCalculationTimeMs,
		TotalCalculationTimeMs,
		MemoryUsedMB);
	FString CSVFormattedData = CSVHeader + CSVRow;

	UE_LOG(SonoTraceUE, Log, TEXT("===== Mesh Preprocessing Test Results ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("Triangles: %d | Frequencies: %d"), NumberOfTriangles, ResolvedFrequencyCount);
	UE_LOG(SonoTraceUE, Log, TEXT("Total calculation time: %.3f ms"), TotalCalculationTimeMs);
	UE_LOG(SonoTraceUE, Log, TEXT("  - Curvature calculation: %.3f ms (%.1f%%)"), 
		CurvatureCalculationTimeMs, 
		(CurvatureCalculationTimeMs / TotalCalculationTimeMs) * 100.0f);
	UE_LOG(SonoTraceUE, Log, TEXT("  - BRDF/Material calculation: %.3f ms (%.1f%%)"), 
		BRDFMaterialCalculationTimeMs, 
		(BRDFMaterialCalculationTimeMs / TotalCalculationTimeMs) * 100.0f);
	UE_LOG(SonoTraceUE, Log, TEXT("Calculated memory for FSonoTraceUEMeshDataStruct: %.3f MB"), MemoryUsedMB);
	UE_LOG(SonoTraceUE, Log, TEXT("Time per triangle: %.6f ms"), TotalCalculationTimeMs / NumberOfTriangles);
	UE_LOG(SonoTraceUE, Log, TEXT("Triangles per second: %.0f"), (NumberOfTriangles / TotalCalculationTimeMs) * 1000.0f);

	if (bSaveToFile)
	{
		FString TargetFilePath = FilePath;
		if (TargetFilePath.IsEmpty())TargetFilePath = FPaths::ProjectSavedDir() + TEXT("SonoTraceUE/MeshPreprocessingTest.csv");

		FString DirectoryPath = FPaths::GetPath(TargetFilePath);
		if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*DirectoryPath))FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);

		bool bFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*TargetFilePath);
		
		FString ContentToWrite;
		if (!bFileExists || !bAppendToFile)
		{
			ContentToWrite = CSVFormattedData;
			UE_LOG(SonoTraceUE, Log, TEXT("Creating new mesh preprocessing test CSV file: %s"), *TargetFilePath);
		}
		else if (bAppendToFile && bFileExists)
		{
			ContentToWrite = CSVRow;
			UE_LOG(SonoTraceUE, Log, TEXT("Appending to existing mesh preprocessing test CSV file: %s"), *TargetFilePath);
		}

		if (FFileHelper::SaveStringToFile(ContentToWrite, *TargetFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), bAppendToFile ? EFileWrite::FILEWRITE_Append : EFileWrite::FILEWRITE_NoFail))
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Successfully saved mesh preprocessing test results to: %s"), *TargetFilePath);
		}
		else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Failed to save mesh preprocessing test results to: %s"), *TargetFilePath);
		}
	}
	
	TestMesh.Clear();
	TestMeshData.TriangleCurvatureMagnitude.Empty();
	TestMeshData.TriangleSize.Empty();
	TestMeshData.TriangleNormal.Empty();
	TestMeshData.TrianglePosition.Empty();
	TestMeshData.ImportanceVertexOrderedBRDFValue.Empty();
	TestMeshData.ImportanceVertexOrderedIndex.Empty();
	for (TArray<float>& InnerArray : TestMeshData.TriangleBRDF)
	{
		InnerArray.Empty();
	}
	TestMeshData.TriangleBRDF.Empty();
	for (TArray<float>& InnerArray : TestMeshData.TriangleMaterial)
	{
		InnerArray.Empty();
	}
	TestMeshData.TriangleMaterial.Empty();
	TestObjectSettings.BrdfExponentsSpecular.Empty();
	TestObjectSettings.BrdfExponentsDiffraction.Empty();
	TestObjectSettings.MaterialStrengthsSpecular.Empty();
	TestObjectSettings.MaterialStrengthsDiffraction.Empty();
	TestObjectSettings.DefaultTriangleBRDF.Empty();
	TestObjectSettings.DefaultTriangleMaterial.Empty();

	UE_LOG(SonoTraceUE, Log, TEXT("===== Test Complete ====="));

	return CSVFormattedData;
}

FString ASonoTraceUEActor::DebugRunSimulationTest(bool testRayTracingParcing, bool testSpecularSimulation, bool testDiffractionSimulation, 
	                                              const TArray<FVector>& InputEmitterPositions, const TArray<FVector>& InputReceiverPositions, int32 InputNumberOfInitialRays, int32 InputMaxBounces,
	                                              int32 InputNumberOfSimFrequencies, int32 NumberOfRuns, const FString& TestDescription, bool bSaveToFile, const FString& FilePath, bool bAppendToFile)
{
	const FString TestTimestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	
	UE_LOG(SonoTraceUE, Log, TEXT("===== Starting Simulation Test ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("Description: %s"), *TestDescription);
	UE_LOG(SonoTraceUE, Log, TEXT("Timestamp: %s"), *TestTimestamp);
	UE_LOG(SonoTraceUE, Log, TEXT("Parameters: Rays=%d, MaxBounces=%d, Emitters=%d, Receivers=%d, Frequencies=%d, Runs=%d"),
		InputNumberOfInitialRays, InputMaxBounces, InputEmitterPositions.Num(), InputReceiverPositions.Num(), InputNumberOfSimFrequencies, NumberOfRuns);
	UE_LOG(SonoTraceUE, Log, TEXT("Tests: RayTracingParsing=%s, Specular=%s, Diffraction=%s"),
		testRayTracingParcing ? TEXT("ON") : TEXT("OFF"),
		testSpecularSimulation ? TEXT("ON") : TEXT("OFF"),
		testDiffractionSimulation ? TEXT("ON") : TEXT("OFF"));	
	
	if (!InputSettings)
	{
		UE_LOG(SonoTraceUE, Error, TEXT("InputSettings is not assigned! Cannot run simulation test."));
		return TEXT("Error: InputSettings not assigned");
	}
	
	if (!InputSettings->DebugDisableInitialization)
	{
		UE_LOG(SonoTraceUE, Warning, TEXT("DebugDisableInitialization is not enabled on InputSettings. Consider enabling it to prevent automatic initialization interfering with tests."));
		return TEXT("Error: DebugDisableInitialization not enabled");
	}
	
	UE_LOG(SonoTraceUE, Log, TEXT("Initializing debug settings and generating mesh data..."));
	
	InputSettings->NumberOfInitialRays = InputNumberOfInitialRays;
	InputSettings->MaximumBounces = InputMaxBounces;
	InputSettings->NumberOfSimFrequencies = InputNumberOfSimFrequencies;
	InputSettings->EnableEmitterPositionsDataTable = false;
	InputSettings->EnableReceiverPositionsDataTable = false;
	InputSettings->EmitterPositions = InputEmitterPositions.Num() > 0 ? InputEmitterPositions : TArray<FVector>{FVector(0, 0, 100)};
	InputSettings->ReceiverPositions = InputReceiverPositions.Num() > 0 ? InputReceiverPositions : TArray<FVector>{FVector(0, 0, 0)};
	InputSettings->EnableSpecularComponentCalculation = testSpecularSimulation;
	InputSettings->EnableDiffractionComponentCalculation = testDiffractionSimulation;
	InputSettings->MinimumSimFrequency = 25000.0f;
	InputSettings->MaximumSimFrequency = 80000.0f;
	InputSettings->PointsInSensorFrame = false;
	InputSettings->EnableEmitterDirectivity = false;
	InputSettings->EnableReceiverDirectivity = false;
	InputSettings->SpecularMinimumStrength = 0.0f;
	InputSettings->EnableSpecularSimulationOnlyOnLastHits = false;
	InputSettings->EnableDirectPathComponentCalculation = false;
	InputSettings->EnableRaytracing = true;
	InputSettings->EnableRunSimulationOnlyOnTrigger = false; 
	
	TMap<UObject*, int32> DebugAssetToObjectTypeIndexSettings;
	GeneratedSettings = GenerateInputSettings(InputSettings, &DebugAssetToObjectTypeIndexSettings);
	
	ReceiverPoses.Empty();
	EmitterPoses.Empty();
	UpdateTransformations();
	
	const double MeshDataStartTime = FPlatformTime::Seconds();
	GenerateAllInitialMeshData();
	const double MeshDataEndTime = FPlatformTime::Seconds();
	const float MeshDataGenerationTimeMs = static_cast<float>((MeshDataEndTime - MeshDataStartTime) * 1000.0);
	
	UE_LOG(SonoTraceUE, Log, TEXT("Mesh data generation completed in %.3f ms"), MeshDataGenerationTimeMs);
	
	const int32 ActualEmitterCount = GeneratedSettings.FinalEmitterPositions.Num();
	const int32 ActualReceiverCount = GeneratedSettings.FinalReceiverPositions.Num();
	
	Initialized = true;	
	
	const int64 SizeOfFloat = sizeof(float);
	const int64 SizeOfInt32 = sizeof(int32);
	const int64 SizeOfBool = sizeof(bool);
	const int64 SizeOfFVector = sizeof(FVector);
	const int64 SizeOfFName = sizeof(FName);
	const int64 SizeOfDouble = sizeof(double);
	const int64 SizeOfPointer = sizeof(void*);
	const int64 TArrayOverhead = sizeof(TArray<float>);
	
	const int64 N = static_cast<int64>(InputNumberOfInitialRays); 
	const int64 F = static_cast<int64>(InputNumberOfSimFrequencies); 
	const int64 E = static_cast<int64>(ActualEmitterCount);
	const int64 R = static_cast<int64>(ActualReceiverCount);  
	const int64 B = static_cast<int64>(InputMaxBounces);  
	
	const int64 SizeOfFStructuredOutputBufferElem = sizeof(FStructuredOutputBufferElem);
	
	const int64 FSonoTraceUEPointStructBase = 2 * SizeOfFVector + SizeOfFName + 4 * SizeOfInt32 + 
	                                          4 * SizeOfFloat + 5 * SizeOfBool + 2 * SizeOfPointer;
	
	const int64 TotalDistancesFromEmittersMemory = E * SizeOfFloat + TArrayOverhead;
	const int64 EmitterDirectivitiesMemory = E * SizeOfFloat + TArrayOverhead;
	const int64 StrengthsArrayMemory = E * R * F * SizeOfFloat + E * R * TArrayOverhead + E * TArrayOverhead + TArrayOverhead;
	const int64 TotalDistancesToReceiversMemory = E * R * SizeOfFloat + E * TArrayOverhead + TArrayOverhead;
	
	const int64 FSonoTraceUEPointStructWorstCase = FSonoTraceUEPointStructBase + 
	                                               TotalDistancesFromEmittersMemory + EmitterDirectivitiesMemory +
	                                               StrengthsArrayMemory + TotalDistancesToReceiversMemory;
	
	const int64 FSonoTraceUESubOutputStructBaseOverhead = 3 * TArrayOverhead + SizeOfDouble + 3 * SizeOfFloat;
	
	const int64 RayTracingParsingInputElements = N * B;
	const int64 RayTracingParsingInputMemory = RayTracingParsingInputElements * SizeOfFStructuredOutputBufferElem + TArrayOverhead;
	
	const int64 EmptyStrengthsOverhead = TArrayOverhead;  
	const int64 EmptyTotalDistancesToReceiversOverhead = TArrayOverhead;  
	const int64 SizeOfPointForParsing = FSonoTraceUEPointStructBase + TotalDistancesFromEmittersMemory + EmitterDirectivitiesMemory +
	                                    EmptyStrengthsOverhead + EmptyTotalDistancesToReceiversOverhead;
	const int64 RayTracingParsingOutputPoints = RayTracingParsingInputElements * SizeOfPointForParsing + TArrayOverhead;
	const int64 RayTracingParsingReflectedStrengths = RayTracingParsingInputElements * SizeOfFloat + TArrayOverhead;
	const int64 RayTracingParsingHitIndexes = RayTracingParsingInputElements * SizeOfInt32 + TArrayOverhead;
	const int64 RayTracingParsingOutputMemory = RayTracingParsingOutputPoints + RayTracingParsingReflectedStrengths + 
	                                            RayTracingParsingHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	const float RayTracingParsingMemoryMB = static_cast<float>(RayTracingParsingInputMemory + RayTracingParsingOutputMemory) / (1024.0f * 1024.0f);
	
    const int64 SpecularPointsMemory = N * FSonoTraceUEPointStructWorstCase + TArrayOverhead;
	const int64 SpecularReflectedStrengths = N * SizeOfFloat + TArrayOverhead;
	const int64 SpecularHitIndexes = N * SizeOfInt32 + TArrayOverhead;  
	const int64 SpecularSimulationTotalMemory = SpecularPointsMemory + SpecularReflectedStrengths + 
	                                            SpecularHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	const float SpecularSimulationMemoryMB = static_cast<float>(SpecularSimulationTotalMemory) / (1024.0f * 1024.0f);
	
	const int64 DiffractionPointsMemory = N * FSonoTraceUEPointStructWorstCase + TArrayOverhead;
	const int64 DiffractionReflectedStrengths = N * SizeOfFloat + TArrayOverhead;
	const int64 DiffractionHitIndexes = N * SizeOfInt32 + TArrayOverhead; 
	const int64 DiffractionSimulationTotalMemory = DiffractionPointsMemory + DiffractionReflectedStrengths + 
	                                               DiffractionHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	const float DiffractionSimulationMemoryMB = static_cast<float>(DiffractionSimulationTotalMemory) / (1024.0f * 1024.0f);
	
	UE_LOG(SonoTraceUE, Log, TEXT("===== Memory Estimates (Worst Case) ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("Ray Tracing Parsing: %.3f MB"), 
		RayTracingParsingMemoryMB);
	UE_LOG(SonoTraceUE, Log, TEXT("  - Input (FStructuredOutputBufferElem): %.3f MB"), 
		static_cast<float>(RayTracingParsingInputMemory) / (1024.0f * 1024.0f));
	UE_LOG(SonoTraceUE, Log, TEXT("  - Output (FSonoTraceUESubOutputStruct): %.3f MB"), 
		static_cast<float>(RayTracingParsingOutputMemory) / (1024.0f * 1024.0f));
	UE_LOG(SonoTraceUE, Log, TEXT("Specular Simulation: %.3f MB "), 
		SpecularSimulationMemoryMB);
	UE_LOG(SonoTraceUE, Log, TEXT("Diffraction Simulation: %.3f MB"), 
		DiffractionSimulationMemoryMB);
	
	TArray<float> AllRayTracingParsingTimes;
	TArray<float> AllSpecularSimulationTimes;
	TArray<float> AllDiffractionSimulationTimes;
	
	for (int32 RunIndex = 0; RunIndex < NumberOfRuns; ++RunIndex)
	{
		UE_LOG(SonoTraceUE, Log, TEXT("===== Run %d of %d ====="), RunIndex + 1, NumberOfRuns);
		
		float RayTracingParsingTimeMs = 0.0f;
		float SpecularSimulationTimeMs = 0.0f;
		float DiffractionSimulationTimeMs = 0.0f;
		int32 NumberOfReflectionPoints = 0;
	
		if (testSpecularSimulation)
		{
			RunDebugSpecularSimulation(InputNumberOfInitialRays, InputNumberOfSimFrequencies, ActualEmitterCount, ActualReceiverCount, SpecularSimulationTimeMs);
			UE_LOG(SonoTraceUE, Log, TEXT("Specular simulation completed in %.3f ms"), SpecularSimulationTimeMs);
		}
		
		if (testRayTracingParcing)
		{
			RunDebugRayTracingParsing(InputMaxBounces, ActualEmitterCount, ActualReceiverCount, RayTracingParsingTimeMs, NumberOfReflectionPoints);
			UE_LOG(SonoTraceUE, Log, TEXT("Ray tracing parsing completed in %.3f ms"), RayTracingParsingTimeMs);
		}
		
		if (testDiffractionSimulation)
		{		
			// RunDebugDiffractionSimulation(InputNumberOfInitialRays, InputNumberOfSimFrequencies, ActualEmitterCount, ActualReceiverCount, DiffractionSimulationTimeMs);
			// UE_LOG(SonoTraceUE, Log, TEXT("Old Diffraction simulation completed in %.3f ms"), DiffractionSimulationTimeMs);
			RunDebugDiffractionSimulationNew(InputNumberOfInitialRays, InputNumberOfSimFrequencies, ActualEmitterCount, ActualReceiverCount, DiffractionSimulationTimeMs);
			UE_LOG(SonoTraceUE, Log, TEXT("New Diffraction simulation completed in %.3f ms"), DiffractionSimulationTimeMs);
		}
	
		AllRayTracingParsingTimes.Add(RayTracingParsingTimeMs);
		AllSpecularSimulationTimes.Add(SpecularSimulationTimeMs);
		AllDiffractionSimulationTimes.Add(DiffractionSimulationTimeMs);
		
		const float TotalTimeMs = RayTracingParsingTimeMs + SpecularSimulationTimeMs + DiffractionSimulationTimeMs;
		
		UE_LOG(SonoTraceUE, Log, TEXT("Run %d completed in %.3f ms total"), RunIndex + 1, TotalTimeMs);
	}

	FString CSVHeader = TEXT("Timestamp,Description,RunIndex,NumberOfInitialRays,MaxBounces,EmitterCount,ReceiverCount,NumberOfSimFrequencies,RayTracingParsingTimeMs,SpecularSimulationTimeMs,DiffractionSimulationTimeMs,TotalTimeMs,RayTracingParsingMemoryMB,SpecularSimulationMemoryMB,DiffractionSimulationMemoryMB\n");
	FString CSVRows;
	
	for (int32 RunIndex = 0; RunIndex < NumberOfRuns; ++RunIndex)
	{
		const float TotalTimeMs = AllRayTracingParsingTimes[RunIndex] + AllSpecularSimulationTimes[RunIndex] + AllDiffractionSimulationTimes[RunIndex];
		
		FString CSVRow = FString::Printf(TEXT("%s,\"%s\",%d,%d,%d,%d,%d,%d,%.15f,%.15f,%.15f,%.15f,%.15f,%.15f,%.15f\n"),
			*TestTimestamp,
			*TestDescription,
			RunIndex + 1,
			InputNumberOfInitialRays,
			InputMaxBounces,
			ActualEmitterCount,
			ActualReceiverCount,
			InputNumberOfSimFrequencies,
			AllRayTracingParsingTimes[RunIndex],
			AllSpecularSimulationTimes[RunIndex],
			AllDiffractionSimulationTimes[RunIndex],
			TotalTimeMs,
			RayTracingParsingMemoryMB,
			SpecularSimulationMemoryMB,
			DiffractionSimulationMemoryMB);
		
		CSVRows += CSVRow;
	}
	
	FString CSVFormattedData = CSVHeader + CSVRows;
	
	if (bSaveToFile)
	{
		FString TargetFilePath = FilePath;
		if (TargetFilePath.IsEmpty())
		{
			TargetFilePath = FPaths::ProjectSavedDir() + TEXT("SonoTraceUE/SimulationTest.csv");
		}

		FString DirectoryPath = FPaths::GetPath(TargetFilePath);
		if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*DirectoryPath))
		{
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
		}

		bool bFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*TargetFilePath);
		
		FString ContentToWrite;
		if (!bFileExists || !bAppendToFile)
		{
			ContentToWrite = CSVFormattedData;
			UE_LOG(SonoTraceUE, Log, TEXT("Creating new simulation test CSV file: %s"), *TargetFilePath);
		}
		else if (bAppendToFile && bFileExists)
		{
			ContentToWrite = CSVRows; 
			UE_LOG(SonoTraceUE, Log, TEXT("Appending %d run(s) to existing simulation test CSV file: %s"), NumberOfRuns, *TargetFilePath);
		}

		if (FFileHelper::SaveStringToFile(ContentToWrite, *TargetFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), bAppendToFile ? EFileWrite::FILEWRITE_Append : EFileWrite::FILEWRITE_NoFail))
		{
			UE_LOG(SonoTraceUE, Log, TEXT("Successfully saved %d run(s) to: %s"), NumberOfRuns, *TargetFilePath);
		}
		else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Failed to save simulation test results to: %s"), *TargetFilePath);
		}
	}
	
	UE_LOG(SonoTraceUE, Log, TEXT("===== Test Complete ====="));
	
	return CSVFormattedData;
}

FString ASonoTraceUEActor::DebugRunRaytracingShaderTest(bool& bOutTestStarted, const TArray<FVector>& InputEmitterPositions, int32 InputNumberOfInitialRays, int32 InputMaxBounces,
												        int32 NumberOfRuns, const FString& TestDescription, bool bSaveToFile, const FString& FilePath, bool bAppendToFile)
{
	// bOutTestStarted indicates if a new test was able to start (false if previous test still running)
	bOutTestStarted = !bDebugRaytracingShaderTestActive;
	
	if (!bOutTestStarted)
	{
		UE_LOG(SonoTraceUE, Warning, TEXT("A debug raytracing shader test is already running. Wait for it to finish."));
		return TEXT("Error: Test already running");
	}
	
	const FString TestTimestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	
	UE_LOG(SonoTraceUE, Log, TEXT("===== Starting Raytracing Shader Test ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("Description: %s"), *TestDescription);
	UE_LOG(SonoTraceUE, Log, TEXT("Timestamp: %s"), *TestTimestamp);
	UE_LOG(SonoTraceUE, Log, TEXT("Parameters: Rays=%d, MaxBounces=%d, Emitters=%d, Runs=%d"),
		InputNumberOfInitialRays, InputMaxBounces, InputEmitterPositions.Num(), NumberOfRuns);
	
	if (!InputSettings)
	{
		UE_LOG(SonoTraceUE, Error, TEXT("InputSettings is not assigned! Cannot run raytracing shader test."));
		bOutTestStarted = false;
		return TEXT("Error: InputSettings not assigned");
	}
	
	if (!InputSettings->DebugDisableInitialization)
	{
		UE_LOG(SonoTraceUE, Warning, TEXT("DebugDisableInitialization is not enabled on InputSettings. Consider enabling it to prevent automatic initialization interfering with tests."));
		bOutTestStarted = false;
		return TEXT("Error: DebugDisableInitialization not enabled");
	}
	
	UE_LOG(SonoTraceUE, Log, TEXT("Initializing debug settings and generating mesh data..."));
	
	InputSettings->NumberOfInitialRays = InputNumberOfInitialRays;
	InputSettings->MaximumBounces = InputMaxBounces;
	InputSettings->NumberOfSimFrequencies = 1;
	InputSettings->EnableEmitterPositionsDataTable = false;
	InputSettings->EnableReceiverPositionsDataTable = false;
	InputSettings->EmitterPositions = InputEmitterPositions.Num() > 0 ? InputEmitterPositions : TArray<FVector>{FVector(0, 0, 100)};
	InputSettings->ReceiverPositions = TArray<FVector>{FVector(0, 0, 0)};
	InputSettings->EnableSpecularComponentCalculation = false;
	InputSettings->EnableDiffractionComponentCalculation = false;
	InputSettings->MinimumSimFrequency = 25000.0f;
	InputSettings->MaximumSimFrequency = 80000.0f;
	InputSettings->PointsInSensorFrame = false;
	InputSettings->EnableEmitterDirectivity = false;
	InputSettings->EnableReceiverDirectivity = false;
	InputSettings->SpecularMinimumStrength = 0.0f;
	InputSettings->EnableSpecularSimulationOnlyOnLastHits = false;
	InputSettings->EnableDirectPathComponentCalculation = false;
	InputSettings->EnableRaytracing = true;
	InputSettings->EnableRunSimulationOnlyOnTrigger = false; 
	
	TMap<UObject*, int32> DebugAssetToObjectTypeIndexSettings;
	GeneratedSettings = GenerateInputSettings(InputSettings, &DebugAssetToObjectTypeIndexSettings);
	
	ReceiverPoses.Empty();
	EmitterPoses.Empty();
	UpdateTransformations();
	
	const double MeshDataStartTime = FPlatformTime::Seconds();
	GenerateAllInitialMeshData();
	const double MeshDataEndTime = FPlatformTime::Seconds();
	const float MeshDataGenerationTimeMs = static_cast<float>((MeshDataEndTime - MeshDataStartTime) * 1000.0);
	
	UE_LOG(SonoTraceUE, Log, TEXT("Mesh data generation completed in %.3f ms"), MeshDataGenerationTimeMs);
	
	const int32 ActualEmitterCount = GeneratedSettings.FinalEmitterPositions.Num();
	const int32 ActualReceiverCount = GeneratedSettings.FinalReceiverPositions.Num();
	
	Initialized = true;
	
	float GPURaytracingMemoryMB = 0.0f;
	const int64 FStructuredOutputBufferElemSize = sizeof(FStructuredOutputBufferElem);
	

	const uint32 NumDistributionRays = InputNumberOfInitialRays;
	const uint32 NumOutputElements = NumDistributionRays * InputMaxBounces;
	
	const int64 AzimuthAnglesBufferSize = NumDistributionRays * sizeof(float);
	const int64 ElevationAnglesBufferSize = NumDistributionRays * sizeof(float);
	const int64 SensorConfigBufferSize = (7 + 3 * ActualEmitterCount) * sizeof(float);
	const int64 StructuredOutputBufferSize = NumOutputElements * FStructuredOutputBufferElemSize;
	
	const int64 TotalGPUMemoryBytes = AzimuthAnglesBufferSize + ElevationAnglesBufferSize + 
									  SensorConfigBufferSize + StructuredOutputBufferSize;
	GPURaytracingMemoryMB = static_cast<float>(TotalGPUMemoryBytes) / (1024.0f * 1024.0f);
	
	UE_LOG(SonoTraceUE, Log, TEXT("Total GPU Memory: %.3f MB (%lld bytes)"), GPURaytracingMemoryMB, TotalGPUMemoryBytes);
	
	DebugRaytracingShaderTestTimestamp = TestTimestamp;
	DebugRaytracingShaderTestDescription = TestDescription;
	DebugRaytracingShaderTestNumberOfRuns = NumberOfRuns;
	DebugRaytracingShaderTestCurrentRunIndex = 0;
	DebugRaytracingShaderTestNumberOfInitialRays = InputNumberOfInitialRays;
	DebugRaytracingShaderTestMaxBounces = InputMaxBounces;
	DebugRaytracingShaderTestEmitterCount = ActualEmitterCount;
	DebugRaytracingShaderTestGPURaytracingMemoryMB = GPURaytracingMemoryMB;
	DebugRaytracingShaderTestSaveToFile = bSaveToFile;
	DebugRaytracingShaderTestFilePath = FilePath;
	DebugRaytracingShaderTestAppendToFile = bAppendToFile;
	DebugRaytracingShaderTestRaytracingShaderTimes.Empty();
	DebugRaytracingShaderTestRaytracingShaderTimes.Init(0.0f, NumberOfRuns);
	DebugRaytracingTestState = 0;
	
	DebugSonoTrace = FSonoTrace(30.0f, true);
	
	if (DebugGPUReadback == nullptr)
	{
		DebugGPUReadback = new FRHIGPUBufferReadback(TEXT("DebugSonoTraceReadback"));
	}
	
	EmitterPoses.Empty();
	for (int32 i = 0; i < ActualEmitterCount; ++i)
	{
		EmitterPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalEmitterPositions[i]));
	}
	
	FSonoTraceParameters Parameters;
	Parameters.Scene = GetWorld()->Scene->GetRenderScene();
	Parameters.GPUReadback = DebugGPUReadback;
	Parameters.SensorPosition = GetActorLocation(); 
	Parameters.SensorRotation = GetActorRotation();  
	Parameters.DistributionAzimuthAngles = GeneratedSettings.AzimuthAngles;
	Parameters.DistributionElevationAngles = GeneratedSettings.ElevationAngles;
	Parameters.MaxTraceDistance = InputSettings->MaximumRayDistance;
	Parameters.NumDistributionRays = GeneratedSettings.AzimuthAngles.Num();
	Parameters.MaxBounces = static_cast<uint32>(InputMaxBounces);
	TArray<FVector> EmitterPositionsForShader;
	for (int32 EmitterIndex = 0; EmitterIndex < EmitterPoses.Num(); EmitterIndex++)
	{
		EmitterPositionsForShader.Add(EmitterPoses[EmitterIndex].GetLocation());
	}
	Parameters.EmitterPositions = EmitterPositionsForShader;
	Parameters.EmitterCount = EmitterPoses.Num();	
	Parameters.EnableDirectPath = false;
	
	// Validation logging for parameters being sent to shader
	UE_LOG(SonoTraceUE, Log, TEXT("===== SHADER PARAMETER VALIDATION ====="));
	UE_LOG(SonoTraceUE, Log, TEXT("NumDistributionRays (Parameters): %d"), Parameters.NumDistributionRays);
	UE_LOG(SonoTraceUE, Log, TEXT("AzimuthAngles.Num(): %d"), Parameters.DistributionAzimuthAngles.Num());
	UE_LOG(SonoTraceUE, Log, TEXT("ElevationAngles.Num(): %d"), Parameters.DistributionElevationAngles.Num());
	UE_LOG(SonoTraceUE, Log, TEXT("MaxBounces: %d"), Parameters.MaxBounces);
	UE_LOG(SonoTraceUE, Log, TEXT("EmitterCount: %d"), Parameters.EmitterCount);
	UE_LOG(SonoTraceUE, Log, TEXT("EnableDirectPath: %s"), Parameters.EnableDirectPath ? TEXT("true") : TEXT("false"));
	
	const uint64 ExpectedOutputElements = (Parameters.DistributionAzimuthAngles.Num() + DirectPathAzimuthAngles.Num()) * static_cast<uint64>(Parameters.MaxBounces);
	const uint64 ExpectedOutputBytes = ExpectedOutputElements * sizeof(FStructuredOutputBufferElem);
	UE_LOG(SonoTraceUE, Log, TEXT("Expected output elements: %llu"), ExpectedOutputElements);
	UE_LOG(SonoTraceUE, Log, TEXT("Expected output buffer size: %llu bytes (%.3f MB)"), 
		ExpectedOutputBytes, static_cast<float>(ExpectedOutputBytes) / (1024.0f * 1024.0f));
	UE_LOG(SonoTraceUE, Log, TEXT("========================================"));
	
	DebugSonoTrace.UpdateParameters(Parameters);
	
	ENQUEUE_RENDER_COMMAND(FSonoTraceDebugInit)([this](FRHICommandListImmediate&)
	{
		DebugSonoTrace.BeginRendering();
	});
	
	bDebugSonoTraceInitialized = true;
	DebugRaytracingPreviousExecutionCounter = DebugSonoTrace.ExecutionCounter;
	
	UE_LOG(SonoTraceUE, Log, TEXT("Debug raytracing shader test initialized. Async test will run across ticks."));
	
	bDebugRaytracingShaderTestActive = true;
	
	return TEXT("Debug raytracing shader test started (async).");
}

void ASonoTraceUEActor::DebugRaytracingShaderTestTick(float DeltaTime)
{
	if (!bDebugRaytracingShaderTestActive)
	{
		return;
	}

	if (DebugRaytracingShaderTestCurrentRunIndex >= DebugRaytracingShaderTestNumberOfRuns)
	{
		LastDebugRaytracingShaderTestCsv = BuildRaytracingShaderTestCsv();
		UE_LOG(SonoTraceUE, Log, TEXT("===== All Raytracing Shader Test Runs Complete - Summary ====="));

		if (DebugRaytracingShaderTestSaveToFile)
		{
			FString TargetFilePath = DebugRaytracingShaderTestFilePath;
			if (TargetFilePath.IsEmpty())
			{
				TargetFilePath = FPaths::ProjectSavedDir() + TEXT("SonoTraceUE/RaytracingShaderTest.csv");
			}

			FString DirectoryPath = FPaths::GetPath(TargetFilePath);
			if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*DirectoryPath))
			{
				FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
			}

			bool bFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*TargetFilePath);
			
			FString CSVRowsOnly = LastDebugRaytracingShaderTestCsv;
			int32 HeaderIndex = CSVRowsOnly.Find(TEXT("\n"));
			if (HeaderIndex != INDEX_NONE)
			{
				CSVRowsOnly = CSVRowsOnly.Mid(HeaderIndex + 1);
			}
			
			FString ContentToWrite;
			if (!bFileExists || !DebugRaytracingShaderTestAppendToFile)
			{
				ContentToWrite = LastDebugRaytracingShaderTestCsv;
				UE_LOG(SonoTraceUE, Log, TEXT("Creating new raytracing shader test CSV file: %s"), *TargetFilePath);
			}
			else
			{
				ContentToWrite = CSVRowsOnly;
				UE_LOG(SonoTraceUE, Log, TEXT("Appending %d run(s) to existing raytracing shader test CSV file: %s"), DebugRaytracingShaderTestNumberOfRuns, *TargetFilePath);
			}

			if (FFileHelper::SaveStringToFile(ContentToWrite, *TargetFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), DebugRaytracingShaderTestAppendToFile ? EFileWrite::FILEWRITE_Append : EFileWrite::FILEWRITE_NoFail))
			{
				UE_LOG(SonoTraceUE, Log, TEXT("Successfully saved %d run(s) to: %s"), DebugRaytracingShaderTestNumberOfRuns, *TargetFilePath);
			}
			else
			{
				UE_LOG(SonoTraceUE, Warning, TEXT("Failed to save raytracing shader test results to: %s"), *TargetFilePath);
			}
		}

		if (bDebugSonoTraceInitialized)
		{
			DebugSonoTrace.EndRendering();
			bDebugSonoTraceInitialized = false;
		}
		if (DebugGPUReadback)
		{
			delete DebugGPUReadback;
			DebugGPUReadback = nullptr;
		}

		bDebugRaytracingShaderTestActive = false;
		UE_LOG(SonoTraceUE, Log, TEXT("===== Debug Raytracing Shader Test Complete ====="));
		return;
	}
	
	if (DebugRaytracingTestState == 0)
	{
		DebugRaytracingDispatchStartTime = FPlatformTime::Seconds();
		DebugRaytracingPreviousExecutionCounter = DebugSonoTrace.ExecutionCounter;
		
		DebugSonoTrace.RunState = 1;
		
		DebugRaytracingTestState = 1;
		UE_LOG(SonoTraceUE, Log, TEXT("Run %d/%d: Dispatched raytracing shader..."), 
			DebugRaytracingShaderTestCurrentRunIndex + 1, DebugRaytracingShaderTestNumberOfRuns);
	}
	else if (DebugRaytracingTestState == 1)
	{
		if (DebugSonoTrace.RunState == 2 && DebugSonoTrace.ExecutionCounter > DebugRaytracingPreviousExecutionCounter)
		{
			if (DebugGPUReadback && DebugGPUReadback->IsReady())
			{
				const double EndTime = FPlatformTime::Seconds();
				const float RaytracingShaderTimeMs = static_cast<float>((EndTime - DebugRaytracingDispatchStartTime) * 1000.0);

				constexpr bool bEnableFullReadbackValidation = false;
				if (DebugRaytracingShaderTestCurrentRunIndex == DebugRaytracingShaderTestNumberOfRuns - 1)
				{
					const uint64 ExpectedNumElements = static_cast<uint64>(DebugRaytracingShaderTestNumberOfInitialRays) * static_cast<uint64>(DebugRaytracingShaderTestMaxBounces);
					const uint64 ExpectedByteSize = ExpectedNumElements * sizeof(FStructuredOutputBufferElem);
					const uint32 AllocatedSize = DebugGPUReadback->GetGPUSizeBytes();

					UE_LOG(SonoTraceUE, Log, TEXT("===== READBACK VALIDATION (Last Run) ====="));
					UE_LOG(SonoTraceUE, Log, TEXT("Expected elements: %llu"), ExpectedNumElements);
					UE_LOG(SonoTraceUE, Log, TEXT("Expected bytes: %llu (%.3f MB)"), ExpectedByteSize, static_cast<float>(ExpectedByteSize) / (1024.0f * 1024.0f));
					UE_LOG(SonoTraceUE, Log, TEXT("Actual GPU allocated bytes: %u (%.3f MB)"), AllocatedSize, static_cast<float>(AllocatedSize) / (1024.0f * 1024.0f));

					if (AllocatedSize == 0)
					{
						UE_LOG(SonoTraceUE, Error, TEXT("GPU buffer allocation FAILED - AllocatedSize is 0!"));
						UE_LOG(SonoTraceUE, Error, TEXT("The shader did not produce any output. Timing is invalid."));
					}
					else if (AllocatedSize != ExpectedByteSize)
					{
						UE_LOG(SonoTraceUE, Warning, TEXT("GPU readback size mismatch! Expected: %llu, Actual: %u"), ExpectedByteSize, AllocatedSize);
					}
					else
					{
						UE_LOG(SonoTraceUE, Log, TEXT("GPU buffer allocation: SUCCESS - size matches expected"));
					}
					
					if (bEnableFullReadbackValidation && AllocatedSize > 0)
					{
						FStructuredOutputBufferElem* ReadbackData = static_cast<FStructuredOutputBufferElem*>(
							DebugGPUReadback->Lock(AllocatedSize));
						
						if (ReadbackData != nullptr)
						{
							UE_LOG(SonoTraceUE, Log, TEXT("Readback Lock: SUCCESS - buffer is valid"));
							
							int32 HitCount = 0;
							int32 ValidElementCount = 0;
							const int32 ActualNumElements = AllocatedSize / sizeof(FStructuredOutputBufferElem);
							const int32 SampleSize = FMath::Min(ActualNumElements, 1000);
							
							for (int32 i = 0; i < SampleSize; ++i)
							{
								const FStructuredOutputBufferElem& Elem = ReadbackData[i];
								if (Elem.IsHit)
								{
									HitCount++;
									if (FMath::IsFinite(Elem.HitPosX) && FMath::IsFinite(Elem.HitPosY) && FMath::IsFinite(Elem.HitPosZ))
									{
										ValidElementCount++;
									}
								}
							}
							
							UE_LOG(SonoTraceUE, Log, TEXT("Sampled first %d elements:"), SampleSize);
							UE_LOG(SonoTraceUE, Log, TEXT("  - Hits: %d (%.2f%%)"), HitCount, (SampleSize > 0) ? (100.0f * HitCount / SampleSize) : 0.0f);
							UE_LOG(SonoTraceUE, Log, TEXT("  - Valid hit positions: %d"), ValidElementCount);
							
							if (HitCount > 0)
							{
								for (int32 i = 0; i < SampleSize; ++i)
								{
									const FStructuredOutputBufferElem& Elem = ReadbackData[i];
									if (Elem.IsHit)
									{
										UE_LOG(SonoTraceUE, Log, TEXT("  - First hit (element %d): Pos(%.2f, %.2f, %.2f), PrimitiveIdx=%d, TriIdx=%d, Dist=%.2f"),
											i, Elem.HitPosX, Elem.HitPosY, Elem.HitPosZ, 
											Elem.HitScenePrimitiveIndex, Elem.HitTriangleIndex, Elem.RayDistanceTotal);
										break;
									}
								}
							}
							
							DebugGPUReadback->Unlock();
						}
						else
						{
							UE_LOG(SonoTraceUE, Error, TEXT("Readback Lock: FAILED - buffer returned nullptr!"));
						}
					}
					
					UE_LOG(SonoTraceUE, Log, TEXT("=========================================="));
				}
				
				DebugRaytracingShaderTestRaytracingShaderTimes[DebugRaytracingShaderTestCurrentRunIndex] = RaytracingShaderTimeMs;
				
				UE_LOG(SonoTraceUE, Log, TEXT("Run %d/%d: Raytracing shader completed in %.3f ms"), 
					DebugRaytracingShaderTestCurrentRunIndex + 1, DebugRaytracingShaderTestNumberOfRuns, RaytracingShaderTimeMs);				

				DebugRaytracingShaderTestCurrentRunIndex++;
				DebugRaytracingTestState = 0;
			}
		}
		
		const double ElapsedSeconds = FPlatformTime::Seconds() - DebugRaytracingDispatchStartTime;
		if (ElapsedSeconds > 30.0)
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Run %d/%d: Raytracing shader timeout after 30 seconds"), 
				DebugRaytracingShaderTestCurrentRunIndex + 1, DebugRaytracingShaderTestNumberOfRuns);
			
			DebugRaytracingShaderTestRaytracingShaderTimes[DebugRaytracingShaderTestCurrentRunIndex] = 30000.0f;
			DebugRaytracingShaderTestCurrentRunIndex++;
			DebugRaytracingTestState = 0;
		}
	}
}

void ASonoTraceUEActor::RunDebugSpecularSimulation(int32 NumberOfInitialRays, int32 NumberOfSimFrequencies, int32 ActualEmitterCount, int32 ActualReceiverCount, float& OutTimeMs)
{
	const int32 SimulatedHitRayCount = NumberOfInitialRays;

	FSonoTraceUESubOutputStruct TestRayTracingSubOutput;
	TestRayTracingSubOutput.ReflectedPoints.SetNum(SimulatedHitRayCount);
	TestRayTracingSubOutput.MaximumCurvature = 0.0f;
	TestRayTracingSubOutput.MaximumStrength = 0.0f;
	TestRayTracingSubOutput.MaximumTotalDistance = 0.0f;
	TestRayTracingSubOutput.Timestamp = FDateTime::Now().ToUnixTimestamp();

	TArray<float> FakeBRDF;
	TArray<float> FakeMaterial;
	FakeBRDF.Init(8.0f, NumberOfSimFrequencies);
	FakeMaterial.Init(10.0f, NumberOfSimFrequencies);

	for (int32 i = 0; i < SimulatedHitRayCount; ++i)
	{
		FSonoTraceUEPointStruct& Point = TestRayTracingSubOutput.ReflectedPoints[i];

		const float Radius = FMath::FRandRange(100.0f, 5000.0f);
		const float Theta = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Phi = FMath::FRandRange(0.0f, PI);
		Point.Location = FVector(
			Radius * FMath::Sin(Phi) * FMath::Cos(Theta),
			Radius * FMath::Sin(Phi) * FMath::Sin(Theta),
			Radius * FMath::Cos(Phi)
		);

		Point.ReflectionDirection = FVector(
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f)
		).GetSafeNormal();

		Point.Label = FName(*FString::Printf(TEXT("TEST_POINT_%d"), i));
		Point.Index = i;
		Point.IsHit = true;
		Point.IsLastHit = true;
		Point.IsSpecular = true;
		Point.IsDiffraction = false;
		Point.IsDirectPath = false;
		Point.RayIndex = i % FMath::Max(1, GeneratedSettings.AzimuthAngles.Num());
		Point.BounceIndex = 0;
		Point.ObjectTypeIndex = 0;
		Point.CurvatureMagnitude = FMath::FRandRange(0.1f, 1.0f);
		Point.TotalDistance = FMath::FRandRange(100.0f, 5000.0f);

		Point.TotalDistancesFromEmitters.SetNum(ActualEmitterCount);
		for (int32 EmitterIdx = 0; EmitterIdx < ActualEmitterCount; ++EmitterIdx)
		{
			Point.TotalDistancesFromEmitters[EmitterIdx] = FMath::FRandRange(100.0f, 5000.0f);
		}

		Point.DistanceToSensor = FMath::FRandRange(100.0f, 5000.0f);
		Point.EmitterDirectivities.Init(1.0f, ActualEmitterCount);
		Point.SurfaceBRDF = &FakeBRDF;
		Point.SurfaceMaterial = &FakeMaterial;
	}

	TArray<FTransform> TestEmitterPoses;
	for (int32 i = 0; i < ActualEmitterCount; ++i)
	{
		TestEmitterPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalEmitterPositions[i]));
	}

	TArray<FTransform> TestReceiverPoses;
	for (int32 i = 0; i < ActualReceiverCount; ++i)
	{
		TestReceiverPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalReceiverPositions[i]));
	}

	FTransform TestWorldToSensorTransform = FTransform::Identity;

	const double SpecularStartTime = FPlatformTime::Seconds();

	RunSpecularComponentSimulation(
		&TestRayTracingSubOutput,
		InputSettings,
		&GeneratedSettings,
		TestWorldToSensorTransform,
		TestEmitterPoses,
		TestReceiverPoses
	);

	const double SpecularEndTime = FPlatformTime::Seconds();
	OutTimeMs = static_cast<float>((SpecularEndTime - SpecularStartTime) * 1000.0);
	
	for (FSonoTraceUEPointStruct& Point : TestRayTracingSubOutput.ReflectedPoints)
	{
		Point.TotalDistancesFromEmitters.Empty();
		Point.EmitterDirectivities.Empty();
		Point.TotalDistancesToReceivers.Empty();
		for (TArray<TArray<float>>& EmitterStrengths : Point.Strengths)
		{
			for (TArray<float>& ReceiverStrengths : EmitterStrengths)
			{
				ReceiverStrengths.Empty();
			}
			EmitterStrengths.Empty();
		}
		Point.Strengths.Empty();
	}
	TestRayTracingSubOutput.ReflectedPoints.Empty();
	TestRayTracingSubOutput.ReflectedStrengths.Empty();
	TestRayTracingSubOutput.HitPersistentPrimitiveIndexes.Empty();
	FakeBRDF.Empty();
	FakeMaterial.Empty();
	TestEmitterPoses.Empty();
	TestReceiverPoses.Empty();
}

void ASonoTraceUEActor::RunDebugRayTracingParsing(int32 MaxBounces, int32 ActualEmitterCount, int32 ActualReceiverCount, float& OutTimeMs, int32& OutReflectionPointCount)
{
	const int32 TotalRays = GeneratedSettings.AzimuthAngles.Num();
	const int32 TotalOutputElements = TotalRays * MaxBounces;

	TArray<FStructuredOutputBufferElem> FakeRayTracingRawOutput;
	FakeRayTracingRawOutput.SetNum(TotalOutputElements);

	int32 DebugScenePrimitiveIndex = 0;
	if (ScenePrimitiveIndexToPersistentPrimitiveIndex.Num() > 0)
	{
		DebugScenePrimitiveIndex = ScenePrimitiveIndexToPersistentPrimitiveIndex.CreateConstIterator().Key();
	}

	for (int32 RayIndex = 0; RayIndex < TotalRays; ++RayIndex)
	{
		for (int32 BounceIndex = 0; BounceIndex < MaxBounces; ++BounceIndex)
		{
			const int32 OutputIndex = RayIndex * MaxBounces + BounceIndex;
			FStructuredOutputBufferElem& Elem = FakeRayTracingRawOutput[OutputIndex];

			Elem.IsHit = true;
			Elem.HitLineOfSightToSensor = true;
			Elem.DirectPath = 0;

			const float Radius = FMath::FRandRange(100.0f, 5000.0f);
			const float Theta = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Phi = FMath::FRandRange(0.0f, PI);
			Elem.HitPosX = Radius * FMath::Sin(Phi) * FMath::Cos(Theta);
			Elem.HitPosY = Radius * FMath::Sin(Phi) * FMath::Sin(Theta);
			Elem.HitPosZ = Radius * FMath::Cos(Phi);

			FVector ReflectionDir = FVector(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)
			).GetSafeNormal();
			Elem.HitReflectionX = ReflectionDir.X;
			Elem.HitReflectionY = ReflectionDir.Y;
			Elem.HitReflectionZ = ReflectionDir.Z;

			Elem.HitScenePrimitiveIndex = DebugScenePrimitiveIndex;
			Elem.HitTriangleIndex = 0;
			Elem.RayDistanceTotal = FMath::FRandRange(100.0f, 5000.0f);

			for (int32 EmitterIdx = 0; EmitterIdx < ActualEmitterCount && EmitterIdx < MaxEmitterCount; ++EmitterIdx)
			{
				Elem.DistancesFromEmitterTotal[EmitterIdx] = FMath::FRandRange(100.0f, 5000.0f);
			}
		}
	}

	TArray<FTransform> ParseTestEmitterPoses;
	for (int32 i = 0; i < ActualEmitterCount; ++i)
	{
		ParseTestEmitterPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalEmitterPositions[i]));
	}

	FVector TestSensorLocation = FVector::ZeroVector;

	const double ParsingStartTime = FPlatformTime::Seconds();

	FSonoTraceUESubOutputStruct ParseTestRayTracingSubOutput;
	ParseTestRayTracingSubOutput.ReflectedPoints.Init(FSonoTraceUEPointStruct(), TotalOutputElements);
	ParseTestRayTracingSubOutput.HitPersistentPrimitiveIndexes.Empty();
	ParseTestRayTracingSubOutput.Timestamp = FDateTime::Now().ToUnixTimestamp();
	ParseTestRayTracingSubOutput.MaximumCurvature = 0.0f;
	ParseTestRayTracingSubOutput.MaximumStrength = 0.0f;
	ParseTestRayTracingSubOutput.MaximumTotalDistance = 0.0f;

	int32 SavedPointIndex = 0;
	for (int32 RayIndex = 0; RayIndex < TotalRays; ++RayIndex)
	{
		bool CurrentRayIsHitting = false;
		TArray<float> CachedSourceDirectivities;
		CachedSourceDirectivities.Init(1.0f, ActualEmitterCount);

		for (int32 BounceIndex = 0; BounceIndex < MaxBounces; ++BounceIndex)
		{
			const int32 OutputIndex = RayIndex * MaxBounces + BounceIndex;

			if (OutputIndex < 0 || OutputIndex >= TotalOutputElements)
			{
				continue;
			}

			const FStructuredOutputBufferElem& CurrentRayTracingOutput = FakeRayTracingRawOutput[OutputIndex];
			if (CurrentRayTracingOutput.IsHit)
			{
				CurrentRayIsHitting = true;

				if (CurrentRayTracingOutput.HitLineOfSightToSensor)
				{
					FVector HitLocation = FVector(
						CurrentRayTracingOutput.HitPosX,
						CurrentRayTracingOutput.HitPosY,
						CurrentRayTracingOutput.HitPosZ
					);

					FVector HitReflectionDirection(
						CurrentRayTracingOutput.HitReflectionX,
						CurrentRayTracingOutput.HitReflectionY,
						CurrentRayTracingOutput.HitReflectionZ
					);

					const float DistanceToSensor = FVector::Distance(HitLocation, TestSensorLocation);
					FName ObjectName = FName(TEXT("TEST_OBJECT"));
					int32 ObjectTypeIndex = 0;
					const float RayDistanceTotal = CurrentRayTracingOutput.RayDistanceTotal;

					TArray<float> RayDistancesTotalFromEmitters;
					RayDistancesTotalFromEmitters.SetNum(ActualEmitterCount);
					for (int32 EmitterIdx = 0; EmitterIdx < ActualEmitterCount && EmitterIdx < MaxEmitterCount; ++EmitterIdx)
					{
						RayDistancesTotalFromEmitters[EmitterIdx] = CurrentRayTracingOutput.DistancesFromEmitterTotal[EmitterIdx];
					}

					float CurvatureMagnitude = FMath::FRandRange(0.1f, 1.0f);
					TArray<float>* SurfaceBRDF = &GeneratedSettings.ObjectSettings[0].DefaultTriangleBRDF;
					TArray<float>* SurfaceMaterial = &GeneratedSettings.ObjectSettings[0].DefaultTriangleMaterial;

					ParseTestRayTracingSubOutput.ReflectedPoints[SavedPointIndex] = FSonoTraceUEPointStruct(
						HitLocation,
						HitReflectionDirection,
						ObjectName,
						OutputIndex,
						RayDistanceTotal,
						RayDistancesTotalFromEmitters,
						DistanceToSensor,
						ObjectTypeIndex,
						CurvatureMagnitude,
						SurfaceBRDF,
						SurfaceMaterial,
						RayIndex,
						BounceIndex,
						CachedSourceDirectivities
					);

					SavedPointIndex++;

					if (CurvatureMagnitude > ParseTestRayTracingSubOutput.MaximumCurvature)
						ParseTestRayTracingSubOutput.MaximumCurvature = CurvatureMagnitude;
					if (RayDistanceTotal > ParseTestRayTracingSubOutput.MaximumTotalDistance)
						ParseTestRayTracingSubOutput.MaximumTotalDistance = RayDistanceTotal;
				}
			}
			else
			{
				if (CurrentRayIsHitting)
					ParseTestRayTracingSubOutput.ReflectedPoints[SavedPointIndex - 1].IsLastHit = true;
				break;
			}
		}
	}

	ParseTestRayTracingSubOutput.ReflectedPoints.RemoveAt(SavedPointIndex, ParseTestRayTracingSubOutput.ReflectedPoints.Num() - SavedPointIndex);

	const double ParsingEndTime = FPlatformTime::Seconds();
	OutTimeMs = static_cast<float>((ParsingEndTime - ParsingStartTime) * 1000.0);
	OutReflectionPointCount = ParseTestRayTracingSubOutput.ReflectedPoints.Num();
	
	for (FSonoTraceUEPointStruct& Point : ParseTestRayTracingSubOutput.ReflectedPoints)
	{
		Point.TotalDistancesFromEmitters.Empty();
		Point.EmitterDirectivities.Empty();
	}
	ParseTestRayTracingSubOutput.ReflectedPoints.Empty();
	ParseTestRayTracingSubOutput.ReflectedStrengths.Empty();
	ParseTestRayTracingSubOutput.HitPersistentPrimitiveIndexes.Empty();
	FakeRayTracingRawOutput.Empty();
	ParseTestEmitterPoses.Empty();
}

void ASonoTraceUEActor::RunDebugDiffractionSimulation(int32 NumberOfInitialRays, int32 NumberOfSimFrequencies, int32 ActualEmitterCount, int32 ActualReceiverCount, float& OutTimeMs)
{
	if (PersistentPrimitiveIndexToMeshDataIndex.Num() == 0 || GeneratedSettings.ObjectSettings.Num() == 0)
	{
		OutTimeMs = 0.0f;
		return;
	}

	TArray<FTransform> TestEmitterPoses;
	for (int32 i = 0; i < ActualEmitterCount; ++i)
	{
		TestEmitterPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalEmitterPositions[i]));
	}

	TArray<FTransform> TestReceiverPoses;
	for (int32 i = 0; i < ActualReceiverCount; ++i)
	{
		TestReceiverPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalReceiverPositions[i]));
	}

	FVector TestSensorLocation = FVector::ZeroVector;
	FVector FixedWorldPosition = FVector(0, 0, 100);
	FVector FixedNormal = FVector(0, 0, 1);

	// Set up line trace parameters (for fair comparison with real diffraction code)
	FCollisionQueryParams TraceParams(FName(TEXT("DiffractionTraceTest")), true);

	const double DiffractionStartTime = FPlatformTime::Seconds();

	FSonoTraceUESubOutputStruct TestDiffractionSubOutput;
	TestDiffractionSubOutput.MaximumCurvature = 0.0f;
	TestDiffractionSubOutput.MaximumStrength = 0.0f;
	TestDiffractionSubOutput.MaximumTotalDistance = 0.0f;

	for (int32 SampleIndex = 0; SampleIndex < NumberOfInitialRays; ++SampleIndex)
	{
		FVector PointLocation = FixedWorldPosition;

		// --- LINE TRACE FOR LINE-OF-SIGHT CHECK (for fair test comparison) ---
		// This simulates the line-of-sight check done in real diffraction code
		// The result is intentionally ignored - we just want the execution cost
		{
			FHitResult HitResult;
			GetWorld()->LineTraceSingleByChannel(
				HitResult,
				TestSensorLocation,
				PointLocation,
				ECC_Visibility,
				TraceParams
			);
			// Result ignored - just executing the trace for fair timing comparison
		}

		TArray<float> VectorEmitterToDiffractionDistance;
		VectorEmitterToDiffractionDistance.Init(0, ActualEmitterCount);

		for (int32 EmitterIndex = 0; EmitterIndex < ActualEmitterCount; ++EmitterIndex)
		{
			VectorEmitterToDiffractionDistance[EmitterIndex] = FVector::Dist(PointLocation, TestEmitterPoses[EmitterIndex].GetLocation());
		}

		float DistancePointToSensor = FVector::Dist(PointLocation, TestSensorLocation);

		FSonoTraceUEPointStruct NewPoint;
		NewPoint.TotalDistancesToReceivers.Init(TArray<float>(), ActualEmitterCount);
		NewPoint.Strengths.SetNum(ActualEmitterCount);

		float SummedStrength = 0.0f;
		for (int32 EmitterIndex = 0; EmitterIndex < ActualEmitterCount; ++EmitterIndex)
		{
			NewPoint.Strengths[EmitterIndex].Init(TArray<float>(), ActualReceiverCount);
			NewPoint.TotalDistancesToReceivers[EmitterIndex].Init(0, ActualReceiverCount);

			for (int32 ReceiverIndex = 0; ReceiverIndex < ActualReceiverCount; ++ReceiverIndex)
			{
				NewPoint.Strengths[EmitterIndex][ReceiverIndex].Init(0, NumberOfSimFrequencies);

				for (int32 FreqIndex = 0; FreqIndex < NumberOfSimFrequencies; ++FreqIndex)
				{
					FVector ReceiverLocation = TestReceiverPoses[ReceiverIndex].GetLocation();
					float DistanceEmitterToPoint = (TestEmitterPoses[EmitterIndex].GetLocation() - PointLocation).Size();
					float DistanceMicToPoint = (ReceiverLocation - PointLocation).Size();
					float FullDistance = DistanceEmitterToPoint + DistanceMicToPoint;
					NewPoint.TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] = FullDistance;

					float FullDistanceMeters = FullDistance / 100.0f;
					float PathLossDiff = 1.0f / FMath::Square(FMath::Max(FullDistanceMeters, 0.01f));
					float AlphaAbsorption = 0.038f * (GeneratedSettings.Frequencies[FreqIndex] / 1000.0f) - 0.3f;
					float PathLossAbsorption = FMath::Pow(10.0f, -(AlphaAbsorption * FullDistanceMeters) / 20.0f);

					float Strength = GeneratedSettings.ObjectSettings[0].MaterialStrengthsDiffraction[FreqIndex] *
									 PathLossDiff * PathLossAbsorption;
					NewPoint.Strengths[EmitterIndex][ReceiverIndex][FreqIndex] = Strength;
					SummedStrength += Strength * Strength;
				}
			}
		}

		SummedStrength = SummedStrength / ActualReceiverCount / ActualEmitterCount / NumberOfSimFrequencies;

		NewPoint.Location = PointLocation;
		NewPoint.ReflectionDirection = FixedNormal;
		NewPoint.Label = FName(TEXT("TEST_DIFFRACTION"));
		NewPoint.Index = SampleIndex;
		NewPoint.SummedStrength = SummedStrength;
		NewPoint.TotalDistance = DistancePointToSensor;
		NewPoint.TotalDistancesFromEmitters = VectorEmitterToDiffractionDistance;
		NewPoint.DistanceToSensor = DistancePointToSensor;
		NewPoint.ObjectTypeIndex = 0;
		NewPoint.IsHit = true;
		NewPoint.IsLastHit = true;
		NewPoint.CurvatureMagnitude = 0.5f;
		NewPoint.SurfaceBRDF = &GeneratedSettings.ObjectSettings[0].DefaultTriangleBRDF;
		NewPoint.SurfaceMaterial = &GeneratedSettings.ObjectSettings[0].DefaultTriangleMaterial;
		NewPoint.IsSpecular = false;
		NewPoint.IsDiffraction = true;

		TestDiffractionSubOutput.ReflectedPoints.Add(NewPoint);
		TestDiffractionSubOutput.ReflectedStrengths.Add(SummedStrength);

		if (SummedStrength > TestDiffractionSubOutput.MaximumStrength)
			TestDiffractionSubOutput.MaximumStrength = SummedStrength;
	}

	const double DiffractionEndTime = FPlatformTime::Seconds();
	OutTimeMs = static_cast<float>((DiffractionEndTime - DiffractionStartTime) * 1000.0);
	
	for (FSonoTraceUEPointStruct& Point : TestDiffractionSubOutput.ReflectedPoints)
	{
		Point.TotalDistancesFromEmitters.Empty();
		Point.EmitterDirectivities.Empty();
		Point.TotalDistancesToReceivers.Empty();
		for (TArray<TArray<float>>& EmitterStrengths : Point.Strengths)
		{
			for (TArray<float>& ReceiverStrengths : EmitterStrengths)
			{
				ReceiverStrengths.Empty();
			}
			EmitterStrengths.Empty();
		}
		Point.Strengths.Empty();
	}
	TestDiffractionSubOutput.ReflectedPoints.Empty();
	TestDiffractionSubOutput.ReflectedStrengths.Empty();
	TestDiffractionSubOutput.HitPersistentPrimitiveIndexes.Empty();
	TestEmitterPoses.Empty();
	TestReceiverPoses.Empty();
}

void ASonoTraceUEActor::RunDebugDiffractionSimulationNew(int32 NumberOfInitialRays, int32 NumberOfSimFrequencies, int32 ActualEmitterCount, int32 ActualReceiverCount, float& OutTimeMs)
{
	if (PersistentPrimitiveIndexToMeshDataIndex.Num() == 0 || GeneratedSettings.ObjectSettings.Num() == 0)
	{
		OutTimeMs = 0.0f;
		return;
	}

	TArray<FTransform> TestEmitterPoses;
	for (int32 i = 0; i < ActualEmitterCount; ++i)
	{
		TestEmitterPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalEmitterPositions[i]));
	}

	TArray<FTransform> TestReceiverPoses;
	for (int32 i = 0; i < ActualReceiverCount; ++i)
	{
		TestReceiverPoses.Add(FTransform(FRotator::ZeroRotator, GeneratedSettings.FinalReceiverPositions[i]));
	}

	FVector TestSensorLocation = FVector::ZeroVector;
	FVector FixedWorldPosition = FVector(0, 0, 100);
	FVector FixedNormal = FVector(0, 0, 1);

	const double DiffractionStartTime = FPlatformTime::Seconds();

	struct FDiffractionCandidate
	{
		int32 SampleIndex;
		FVector WorldPosition;
		FVector WorldNormal;
	};

	
	TArray<FDiffractionCandidate> AllCandidates;
	AllCandidates.SetNum(NumberOfInitialRays);

	for (int32 SampleIndex = 0; SampleIndex < NumberOfInitialRays; ++SampleIndex)
	{
		FDiffractionCandidate& Cand = AllCandidates[SampleIndex];
		Cand.SampleIndex = SampleIndex;
		Cand.WorldPosition = FixedWorldPosition;
		Cand.WorldNormal = FixedNormal;
	}

	TArray<FSonoTraceUEPointStruct> TempResults;
	TempResults.SetNum(AllCandidates.Num());
	
	TArray<float> TempStrengths;
	TempStrengths.SetNum(AllCandidates.Num());


	volatile int32 ValidPointCount = 0;

	const FSonoTraceUEObjectSettingsStruct& CachedObjectSettings = GeneratedSettings.ObjectSettings[0];
	const TArray<float>& CachedFrequencies = GeneratedSettings.Frequencies;
	const TArray<float>& CachedMaterialStrengthsDiffraction = CachedObjectSettings.MaterialStrengthsDiffraction;
	const TArray<float>* CachedDefaultTriangleBRDF = &CachedObjectSettings.DefaultTriangleBRDF;
	const TArray<float>* CachedDefaultTriangleMaterial = &CachedObjectSettings.DefaultTriangleMaterial;

	UWorld* CachedWorld = GetWorld();

	ParallelFor(AllCandidates.Num(), [&](int32 Idx)
	{
		const FDiffractionCandidate& Cand = AllCandidates[Idx];
		const FVector& PointLocation = Cand.WorldPosition;
		const FVector& PointNormal = Cand.WorldNormal;

		{
			FCollisionQueryParams TraceParams(FName(TEXT("DiffractionTraceTest")), true);
			FHitResult HitResult;
			CachedWorld->LineTraceSingleByChannel(
				HitResult,
				TestSensorLocation,
				PointLocation,
				ECC_Visibility,
				TraceParams
			);
			
		}

		TArray<float> VectorEmitterToDiffractionDistance;
		VectorEmitterToDiffractionDistance.Init(0, ActualEmitterCount);

		for (int32 EmitterIndex = 0; EmitterIndex < ActualEmitterCount; ++EmitterIndex)
		{
			VectorEmitterToDiffractionDistance[EmitterIndex] = FVector::Dist(PointLocation, TestEmitterPoses[EmitterIndex].GetLocation());
		}

		float DistancePointToSensor = FVector::Dist(PointLocation, TestSensorLocation);

		FSonoTraceUEPointStruct NewPoint;
		NewPoint.TotalDistancesToReceivers.Init(TArray<float>(), ActualEmitterCount);
		NewPoint.Strengths.SetNum(ActualEmitterCount);

		float SummedStrength = 0.0f;
		for (int32 EmitterIndex = 0; EmitterIndex < ActualEmitterCount; ++EmitterIndex)
		{
			NewPoint.Strengths[EmitterIndex].Init(TArray<float>(), ActualReceiverCount);
			NewPoint.TotalDistancesToReceivers[EmitterIndex].Init(0, ActualReceiverCount);

			for (int32 ReceiverIndex = 0; ReceiverIndex < ActualReceiverCount; ++ReceiverIndex)
			{
				NewPoint.Strengths[EmitterIndex][ReceiverIndex].Init(0, NumberOfSimFrequencies);

				for (int32 FreqIndex = 0; FreqIndex < NumberOfSimFrequencies; ++FreqIndex)
				{
					FVector ReceiverLocation = TestReceiverPoses[ReceiverIndex].GetLocation();
					float DistanceEmitterToPoint = (TestEmitterPoses[EmitterIndex].GetLocation() - PointLocation).Size();
					float DistanceMicToPoint = (ReceiverLocation - PointLocation).Size();
					float FullDistance = DistanceEmitterToPoint + DistanceMicToPoint;
					NewPoint.TotalDistancesToReceivers[EmitterIndex][ReceiverIndex] = FullDistance;

					float FullDistanceMeters = FullDistance / 100.0f;
					float PathLossDiff = 1.0f / FMath::Square(FMath::Max(FullDistanceMeters, 0.01f));
					float AlphaAbsorption = 0.038f * (CachedFrequencies[FreqIndex] / 1000.0f) - 0.3f;
					float PathLossAbsorption = FMath::Pow(10.0f, -(AlphaAbsorption * FullDistanceMeters) / 20.0f);

					float Strength = CachedMaterialStrengthsDiffraction[FreqIndex] * PathLossDiff * PathLossAbsorption;
					NewPoint.Strengths[EmitterIndex][ReceiverIndex][FreqIndex] = Strength;
					SummedStrength += Strength * Strength;
				}
			}
		}

		SummedStrength = SummedStrength / ActualReceiverCount / ActualEmitterCount / NumberOfSimFrequencies;

		NewPoint.Location = PointLocation;
		NewPoint.ReflectionDirection = PointNormal;
		NewPoint.Label = FName(TEXT("TEST_DIFFRACTION"));
		NewPoint.Index = Cand.SampleIndex;
		NewPoint.SummedStrength = SummedStrength;
		NewPoint.TotalDistance = DistancePointToSensor;
		NewPoint.TotalDistancesFromEmitters = MoveTemp(VectorEmitterToDiffractionDistance);
		NewPoint.DistanceToSensor = DistancePointToSensor;
		NewPoint.ObjectTypeIndex = 0;
		NewPoint.IsHit = true;
		NewPoint.IsLastHit = true;
		NewPoint.CurvatureMagnitude = 0.5f;
		NewPoint.SurfaceBRDF = const_cast<TArray<float>*>(CachedDefaultTriangleBRDF);
		NewPoint.SurfaceMaterial = const_cast<TArray<float>*>(CachedDefaultTriangleMaterial);
		NewPoint.IsSpecular = false;
		NewPoint.IsDiffraction = true;

		int32 WriteIndex = FPlatformAtomics::InterlockedIncrement(&ValidPointCount) - 1;

		TempResults[WriteIndex] = MoveTemp(NewPoint);
		TempStrengths[WriteIndex] = SummedStrength;
	});

	TempResults.SetNum(ValidPointCount);
	TempStrengths.SetNum(ValidPointCount);

	FSonoTraceUESubOutputStruct TestDiffractionSubOutput;
	TestDiffractionSubOutput.MaximumCurvature = 0.0f;
	TestDiffractionSubOutput.MaximumStrength = 0.0f;
	TestDiffractionSubOutput.MaximumTotalDistance = 0.0f;
	TestDiffractionSubOutput.ReflectedPoints = MoveTemp(TempResults);
	TestDiffractionSubOutput.ReflectedStrengths = MoveTemp(TempStrengths);

	for (const FSonoTraceUEPointStruct& Pt : TestDiffractionSubOutput.ReflectedPoints)
	{
		if (Pt.SummedStrength > TestDiffractionSubOutput.MaximumStrength)
		{
			TestDiffractionSubOutput.MaximumStrength = Pt.SummedStrength;
		}
	}

	const double DiffractionEndTime = FPlatformTime::Seconds();
	OutTimeMs = static_cast<float>((DiffractionEndTime - DiffractionStartTime) * 1000.0);
	
	for (FSonoTraceUEPointStruct& Point : TestDiffractionSubOutput.ReflectedPoints)
	{
		Point.TotalDistancesFromEmitters.Empty();
		Point.EmitterDirectivities.Empty();
		Point.TotalDistancesToReceivers.Empty();
		for (TArray<TArray<float>>& EmitterStrengths : Point.Strengths)
		{
			for (TArray<float>& ReceiverStrengths : EmitterStrengths)
			{
				ReceiverStrengths.Empty();
			}
			EmitterStrengths.Empty();
		}
		Point.Strengths.Empty();
	}
	TestDiffractionSubOutput.ReflectedPoints.Empty();
	TestDiffractionSubOutput.ReflectedStrengths.Empty();
	TestDiffractionSubOutput.HitPersistentPrimitiveIndexes.Empty();
	TestEmitterPoses.Empty();
	TestReceiverPoses.Empty();
}

FString ASonoTraceUEActor::BuildRaytracingShaderTestCsv() const
{
	FString CSVHeader = TEXT("Timestamp,Description,RunIndex,NumberOfInitialRays,MaxBounces,EmitterCount,GPURaytracingMemoryMB,RaytracingShaderTimeMs\n");
	FString CSVRows;

	for (int32 RunIndex = 0; RunIndex < DebugRaytracingShaderTestNumberOfRuns; ++RunIndex)
	{
		FString CSVRow = FString::Printf(TEXT("%s,\"%s\",%d,%d,%d,%d,%.15f,%.15f\n"),
			*DebugRaytracingShaderTestTimestamp,
			*DebugRaytracingShaderTestDescription,
			RunIndex + 1,
			DebugRaytracingShaderTestNumberOfInitialRays,
			DebugRaytracingShaderTestMaxBounces,
			DebugRaytracingShaderTestEmitterCount,			
			DebugRaytracingShaderTestGPURaytracingMemoryMB,
			DebugRaytracingShaderTestRaytracingShaderTimes[RunIndex]);
		CSVRows += CSVRow;
	}
	return CSVHeader + CSVRows;
}

bool ASonoTraceUEActor::EstimateSimulationMemoryUsage(int32 NumEmitters, int32 NumReceivers, int32 NumFrequencies, int32 NumPoints, int32 NumBounces,
	float& OutRayTracingParsingMemoryMB, float& OutSpecularMemoryMB, float& OutDiffractionMemoryMB, float& OutTotalMemoryMB)
{
	const int64 SizeOfFloat = sizeof(float);
	const int64 SizeOfInt32 = sizeof(int32);
	const int64 SizeOfBool = sizeof(bool);
	const int64 SizeOfFVector = sizeof(FVector);
	const int64 SizeOfFName = sizeof(FName);
	const int64 SizeOfDouble = sizeof(double);
	const int64 SizeOfPointer = sizeof(void*);
	const int64 TArrayOverhead = sizeof(TArray<float>);
	const int64 SizeOfFStructuredOutputBufferElem = sizeof(FStructuredOutputBufferElem);
	
	const int64 N = static_cast<int64>(NumPoints);
	const int64 E = static_cast<int64>(NumEmitters);
	const int64 R = static_cast<int64>(NumReceivers);
	const int64 F = static_cast<int64>(NumFrequencies);
	const int64 B = static_cast<int64>(NumBounces);
	
	const int64 FSonoTraceUEPointStructBase = 2 * SizeOfFVector + SizeOfFName + 4 * SizeOfInt32 + 
	                                          4 * SizeOfFloat + 5 * SizeOfBool + 2 * SizeOfPointer;
	
	const int64 TotalDistancesFromEmittersMemory = E * SizeOfFloat + TArrayOverhead;
	const int64 EmitterDirectivitiesMemory = E * SizeOfFloat + TArrayOverhead;
	const int64 StrengthsArrayMemory = E * R * F * SizeOfFloat + E * R * TArrayOverhead + E * TArrayOverhead + TArrayOverhead;
	const int64 TotalDistancesToReceiversMemory = E * R * SizeOfFloat + E * TArrayOverhead + TArrayOverhead;
	
	const int64 FSonoTraceUEPointStructWorstCase = FSonoTraceUEPointStructBase + 
	                                               TotalDistancesFromEmittersMemory + EmitterDirectivitiesMemory +
	                                               StrengthsArrayMemory + TotalDistancesToReceiversMemory;
	
	const int64 FSonoTraceUESubOutputStructBaseOverhead = 3 * TArrayOverhead + SizeOfDouble + 3 * SizeOfFloat;
	
	const int64 RayTracingParsingInputElements = N * B;
	const int64 RayTracingParsingInputMemory = RayTracingParsingInputElements * SizeOfFStructuredOutputBufferElem + TArrayOverhead;
	
	const int64 EmptyStrengthsOverhead = TArrayOverhead;
	const int64 EmptyTotalDistancesToReceiversOverhead = TArrayOverhead;
	const int64 SizeOfPointForParsing = FSonoTraceUEPointStructBase + TotalDistancesFromEmittersMemory + EmitterDirectivitiesMemory +
	                                    EmptyStrengthsOverhead + EmptyTotalDistancesToReceiversOverhead;
	const int64 RayTracingParsingOutputPoints = RayTracingParsingInputElements * SizeOfPointForParsing + TArrayOverhead;
	const int64 RayTracingParsingReflectedStrengths = RayTracingParsingInputElements * SizeOfFloat + TArrayOverhead;
	const int64 RayTracingParsingHitIndexes = RayTracingParsingInputElements * SizeOfInt32 + TArrayOverhead;
	const int64 RayTracingParsingOutputMemory = RayTracingParsingOutputPoints + RayTracingParsingReflectedStrengths + 
	                                            RayTracingParsingHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	OutRayTracingParsingMemoryMB = static_cast<float>(RayTracingParsingInputMemory + RayTracingParsingOutputMemory) / (1024.0f * 1024.0f);
	
	const int64 SpecularPointsMemory = N * FSonoTraceUEPointStructWorstCase + TArrayOverhead;
	const int64 SpecularReflectedStrengths = N * SizeOfFloat + TArrayOverhead;
	const int64 SpecularHitIndexes = N * SizeOfInt32 + TArrayOverhead;
	const int64 SpecularSimulationTotalMemory = SpecularPointsMemory + SpecularReflectedStrengths + 
	                                            SpecularHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	OutSpecularMemoryMB = static_cast<float>(SpecularSimulationTotalMemory) / (1024.0f * 1024.0f);
	
	const int64 DiffractionPointsMemory = N * FSonoTraceUEPointStructWorstCase + TArrayOverhead;
	const int64 DiffractionReflectedStrengths = N * SizeOfFloat + TArrayOverhead;
	const int64 DiffractionHitIndexes = N * SizeOfInt32 + TArrayOverhead;
	const int64 DiffractionSimulationTotalMemory = DiffractionPointsMemory + DiffractionReflectedStrengths + 
	                                               DiffractionHitIndexes + FSonoTraceUESubOutputStructBaseOverhead;
	
	OutDiffractionMemoryMB = static_cast<float>(DiffractionSimulationTotalMemory) / (1024.0f * 1024.0f);
	
	OutTotalMemoryMB = OutRayTracingParsingMemoryMB + OutSpecularMemoryMB + OutDiffractionMemoryMB;
	
	return true;
}

bool ASonoTraceUEActor::WouldSimulationTestFitInMemory(int32 NumEmitters, int32 NumReceivers, int32 NumFrequencies, int32 NumPoints, int32 NumBounces,
	float MemoryLimitMB, bool bTestRayTracingParsing, bool bTestSpecular, bool bTestDiffraction, float& OutEstimatedMemoryMB, float& OutTotalEstimatedMemoryMB)
{
	float RayTracingParsingMemoryMB = 0.0f;
	float SpecularMemoryMB = 0.0f;
	float DiffractionMemoryMB = 0.0f;
	float TotalMemoryMB = 0.0f;
	
	EstimateSimulationMemoryUsage(NumEmitters, NumReceivers, NumFrequencies, NumPoints, NumBounces,
		RayTracingParsingMemoryMB, SpecularMemoryMB, DiffractionMemoryMB, TotalMemoryMB);
	
	OutEstimatedMemoryMB = 0.0f;
	OutTotalEstimatedMemoryMB = 0.0f;
	if (bTestRayTracingParsing)
	{
		OutEstimatedMemoryMB = RayTracingParsingMemoryMB;
		OutTotalEstimatedMemoryMB += RayTracingParsingMemoryMB;
	}
	if (bTestSpecular)
	{
		OutEstimatedMemoryMB = FMath::Max(OutEstimatedMemoryMB, SpecularMemoryMB);
			OutTotalEstimatedMemoryMB += SpecularMemoryMB;
	}
	if (bTestDiffraction)
	{
		OutEstimatedMemoryMB = FMath::Max(OutEstimatedMemoryMB, DiffractionMemoryMB);
		OutTotalEstimatedMemoryMB += DiffractionMemoryMB;
	}	
	return OutEstimatedMemoryMB <= MemoryLimitMB;
}

float ASonoTraceUEActor::SigmoidMix(const float X, const float Slope, const float Center, const float Value1, const float Value2)
{
	// Compute the sigmoid value
	const float Sigmoid1 = 1.0f / (1.0f + exp(-Slope * (X - Center)));

	// Compute the complementary sigmoid value
	const float Sigmoid2 = 1.0f - Sigmoid1;

	// Blend V1 and V2 based on the sigmoid values
	const float Mixed = Sigmoid1 * Value1 + Sigmoid2 * Value2;

	return Mixed;
}

void ASonoTraceUEActor::GenerateBRDFAndMaterial(const FSonoTraceUEObjectSettingsStruct* ObjectSettings, FSonoTraceUEMeshDataStruct* MeshData)
{
	MeshData->TriangleBRDF.Init(TArray<float>(), MeshData->TriangleCurvatureMagnitude.Num());
	MeshData->TriangleMaterial.Init(TArray<float>(), MeshData->TriangleCurvatureMagnitude.Num());
	for (TArray<float>& InnerArray : MeshData->TriangleBRDF)
	{
		InnerArray.Init(0.0f, ObjectSettings->BrdfExponentsDiffraction.Num());
	}
	for (TArray<float>& InnerArray : MeshData->TriangleMaterial)
	{
		InnerArray.Init(0.0f, ObjectSettings->BrdfExponentsDiffraction.Num());
	}

	// A slope of 1 feels "natural" but is too slow in material transition.
	// It should be 8 or 10 or so.
	// Therefore, this factor is added.
	const float SigmoidSlopeMultiplier = 8;
	
	TArray<float> ImportanceVertexValues;
	for (int32 FrequencyIndex = 0; FrequencyIndex < ObjectSettings->BrdfExponentsDiffraction.Num(); ++FrequencyIndex)
	{
		for (int32 TriangleIndex = 0; TriangleIndex < MeshData->TriangleCurvatureMagnitude.Num(); ++TriangleIndex)
		{
			const float CurCurvatureMagnitude = MeshData->TriangleCurvatureMagnitude[TriangleIndex];
			float SurfaceBRDF = SigmoidMix(CurCurvatureMagnitude, SigmoidSlopeMultiplier * ObjectSettings->BrdfTransitionSlope, ObjectSettings->BrdfTransitionPosition,
										   ObjectSettings->BrdfExponentsDiffraction[FrequencyIndex], ObjectSettings->BrdfExponentsSpecular[FrequencyIndex]);
			float SurfaceMaterial = SigmoidMix(CurCurvatureMagnitude, SigmoidSlopeMultiplier * ObjectSettings->MaterialsTransitionSlope, ObjectSettings->MaterialsTransitionPosition,
							   ObjectSettings->MaterialStrengthsDiffraction[FrequencyIndex], ObjectSettings->MaterialStrengthsSpecular[FrequencyIndex]);
			MeshData->TriangleBRDF[TriangleIndex][FrequencyIndex] = SurfaceBRDF;
			MeshData->TriangleMaterial[TriangleIndex][FrequencyIndex] = SurfaceMaterial;
			if (FrequencyIndex == 0)
			{
				ImportanceVertexValues.Add(SurfaceBRDF);
			}
		}
	}
	
	// Normalized and sorted importance vector based on BRDF of vertices	
	float MinValue = TNumericLimits<float>::Max();
	for (const float Value : ImportanceVertexValues)
	{
		MinValue = FMath::Min(MinValue, Value);
	}
	for (float& Value : ImportanceVertexValues)
	{
		Value -= MinValue;
	}

	float MaxValue = TNumericLimits<float>::Min();
	for (const float Value : ImportanceVertexValues)
	{
		MaxValue = FMath::Max(MaxValue, Value);
	}
	if (MaxValue > 0.0f) 
	{
		for (float& Value : ImportanceVertexValues)
		{
			Value /= MaxValue;
		}
	}

	for (float& Value : ImportanceVertexValues)
	{
		Value = FMath::Pow(Value, 4.0f);
	}
	MinValue = TNumericLimits<float>::Max();
	MaxValue = TNumericLimits<float>::Min();
	for (const float Value : ImportanceVertexValues)
	{
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
	}
	if (MaxValue > MinValue)
	{
		for (float& Value : ImportanceVertexValues)
		{
			Value = (Value - MinValue) / (MaxValue - MinValue);
		}
	}

	TArray<FDiffractionImportancePair> ValueIndexPairs;
	for (int32 Index = 0; Index < ImportanceVertexValues.Num(); ++Index)
	{
		ValueIndexPairs.Add(FDiffractionImportancePair(ImportanceVertexValues[Index], Index));
	}
	Algo::Sort(ValueIndexPairs);
	for (const FDiffractionImportancePair& Pair : ValueIndexPairs)
	{
		MeshData->ImportanceVertexOrderedBRDFValue.Add(Pair.Value);
		MeshData->ImportanceVertexOrderedIndex.Add(Pair.Index); 
	}
}

int32 ASonoTraceUEActor::GetMeshComponentTriangleCount(UMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return -1;
	}
	
	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(MeshComponent))
	{
		UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (StaticMesh && StaticMesh->GetRenderData())
		{
			const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0]; // LOD 0
			return LODResources.GetNumTriangles();
		}
	}
	
	if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		USkeletalMesh* SkeletalMesh = SkeletalMeshComp->GetSkeletalMeshAsset();
		if (SkeletalMesh)
		{
			const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
			if (RenderData && RenderData->LODRenderData.Num() > 0)
			{
				int32 TotalTriangles = 0;
				const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0]; // LOD 0
				for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
				{
					TotalTriangles += Section.NumTriangles;
				}
				return TotalTriangles;
			}
		}
	}
	
	
	UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>();
	
	static FGeometryScriptCopyMeshFromComponentOptions Options;
	Options.bWantNormals = false;  
	Options.bWantTangents = false;
	Options.bWantInstanceColors = false;
	
	FTransform DummyTransform;         
	EGeometryScriptOutcomePins Outcome; 	
	UGeometryScriptLibrary_SceneUtilityFunctions::CopyMeshFromComponent(
		MeshComponent,
		DynamicMesh,
		Options,
		false,        
		DummyTransform,
		Outcome,
		nullptr
	);
	
	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		return -1;		
	}
	
	const FDynamicMesh3* Mesh = DynamicMesh->GetMeshPtr();
	if (!Mesh)
	{
		return -1;			
	}
	
	return Mesh->TriangleCount();
}

void ASonoTraceUEActor::CalculateMeshCurvature(UMeshComponent* MeshComponent, FSonoTraceUEMeshDataStruct& OutMeshData, const float CurvatureScaleFactor, const bool EnableCurvatureTriangleSizeBasedScaler,
	                                            const float CurvatureScalerMinimumEffect, const float CurvatureScalerMaximumEffect, const float CurvatureScalerLowerTriangleSizeThreshold, const float CurvatureScalerUpperTriangleSizeThreshold, const float DiffractionTriangleSizeThreshold)
{	
   	UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>();
	
	static FGeometryScriptCopyMeshFromComponentOptions Options;
	Options.bWantNormals = true;  
	Options.bWantTangents = false;
	Options.bWantInstanceColors = false;
	
	FTransform DummyTransform;         
	EGeometryScriptOutcomePins Outcome; 	
	UGeometryScriptLibrary_SceneUtilityFunctions::CopyMeshFromComponent(
		MeshComponent,
		DynamicMesh,
		Options,
		false,        
		DummyTransform,
		Outcome,
		nullptr
	);
	
	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to convert StaticMesh to DynamicMesh!"));		
	}else
	{			
		const FDynamicMesh3* Mesh = DynamicMesh->GetMeshPtr();
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to access internal mesh data!"));			
		}else
		{
			for (int32 TriangleIndex : Mesh->TriangleIndicesItr())
			{
				UE::Geometry::FIndex3i TriVertices = Mesh->GetTriangle(TriangleIndex);
				
				FVector3d Vertex1Position = Mesh->GetVertex(TriVertices.A);
				FVector3d Vertex2Position = Mesh->GetVertex(TriVertices.B);
				FVector3d Vertex3Position = Mesh->GetVertex(TriVertices.C);
				FVector3d TrianglePosition = (Vertex1Position + Vertex2Position + Vertex3Position) / 3.0;
				OutMeshData.TrianglePosition.Add(TrianglePosition);

				FVector3f Vertex1Normal = Mesh->GetVertexNormal(TriVertices.A);
				FVector Edge1 = FVector(Vertex2Position) - FVector(Vertex1Position);
				FVector Edge2 = FVector(Vertex3Position) - FVector(Vertex1Position);
				FVector TriangleNormal = FVector::CrossProduct(Edge1, Edge2);
				float DotNormal = FVector::DotProduct( TriangleNormal, FVector(Vertex1Normal));
				if (DotNormal < 0.0f)
					TriangleNormal = -TriangleNormal;
				OutMeshData.TriangleNormal.Add(TriangleNormal);

				float TriangleSize = FVector3d::CrossProduct(Vertex2Position - Vertex1Position, Vertex3Position - Vertex1Position).Length() * 0.5;
				OutMeshData.TriangleSize.Add(TriangleSize);		

				// based on equations in http://www.geometry.caltech.edu/pubs/DMSB_III.pdf
				FVector3d Vertex1CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(*Mesh, TriVertices.A);
				FVector3d Vertex2CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(*Mesh, TriVertices.B);
				FVector3d Vertex3CurvatureNormal = UE::MeshCurvature::MeanCurvatureNormal(*Mesh, TriVertices.C);

				// Compute magnitudes of the curvature normals
				double CurvatureMag1 = Vertex1CurvatureNormal.Length() / 2;
				double CurvatureMag2 = Vertex2CurvatureNormal.Length() / 2;
				double CurvatureMag3 = Vertex3CurvatureNormal.Length() / 2;

				// Compute the range of magnitudes for the triangle
				double MaxCurvature = FMath::Max3(CurvatureMag1, CurvatureMag2, CurvatureMag3);
				double MinCurvature = FMath::Min3(CurvatureMag1, CurvatureMag2, CurvatureMag3);
				double CurvatureRange = MaxCurvature - MinCurvature;

				// Use CurvatureRange as the sharpness metric
				double MeanCurvatureNormal = CurvatureRange;				
				// FVector3d MeanCurvatureNormal = (Vertex1CurvatureNormal + Vertex2CurvatureNormal + Vertex3CurvatureNormal) / 3.0;
				if (EnableCurvatureTriangleSizeBasedScaler)
				{
					float ScaledAreaEffect = 0;
				    if(TriangleSize <= CurvatureScalerLowerTriangleSizeThreshold){
				    	ScaledAreaEffect = CurvatureScalerMinimumEffect + (1 - CurvatureScalerMinimumEffect) / CurvatureScalerLowerTriangleSizeThreshold * TriangleSize;
				    }else if(TriangleSize > CurvatureScalerLowerTriangleSizeThreshold && TriangleSize <= CurvatureScalerUpperTriangleSizeThreshold){
				    	ScaledAreaEffect = 1 + (CurvatureScalerMaximumEffect - 1) / (CurvatureScalerUpperTriangleSizeThreshold - CurvatureScalerLowerTriangleSizeThreshold) * (TriangleSize - CurvatureScalerLowerTriangleSizeThreshold);
				    }else if(TriangleSize > CurvatureScalerUpperTriangleSizeThreshold && TriangleSize <= DiffractionTriangleSizeThreshold){
				    	ScaledAreaEffect = CurvatureScalerMaximumEffect - CurvatureScalerMaximumEffect / (DiffractionTriangleSizeThreshold - CurvatureScalerUpperTriangleSizeThreshold) * (TriangleSize - CurvatureScalerUpperTriangleSizeThreshold);
				    }
					MeanCurvatureNormal = MeanCurvatureNormal * ScaledAreaEffect;
				}
				OutMeshData.TriangleCurvatureMagnitude.Add(MeanCurvatureNormal * CurvatureScaleFactor);								
			}
		}
	} 
}

FVector ASonoTraceUEActor::CalculateTrianglePosition(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3)
{
	return (FVector(Vertex1) + FVector(Vertex2) + FVector(Vertex3)) / 3.0f;
}

FVector ASonoTraceUEActor::CalculateTriangleNormal(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3)
{
	const FVector Edge1 = FVector(Vertex2) - FVector(Vertex1);
	const FVector Edge2 = FVector(Vertex3) - FVector(Vertex1);
	const FVector TriangleNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();	
	return TriangleNormal;
}

float ASonoTraceUEActor::CalculateTriangleCurvature(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3,
const FVector3f& Normal1, const FVector3f& Normal2, const FVector3f& Normal3)
{
	// Calculate edge vectors
	FVector Edge1 = FVector(Vertex2) - FVector(Vertex1);
	FVector Edge2 = FVector(Vertex3) - FVector(Vertex1);
	FVector Edge3 = FVector(Vertex3) - FVector(Vertex2);

	// Normalize edge vectors
	Edge1 = Edge1.GetSafeNormal();
	Edge2 = Edge2.GetSafeNormal();
	Edge3 = Edge3.GetSafeNormal();

	// Compute normal differences for each edge
	const FVector NormalDiff1 = FVector(Normal2) - FVector(Normal1);
	const FVector NormalDiff2 = FVector(Normal3) - FVector(Normal1);
	const FVector NormalDiff3 = FVector(Normal3) - FVector(Normal2);

	// Project normal differences onto edge directions
	const float CurvatureS1 = FVector::DotProduct(NormalDiff1, Edge1);
	const float CurvatureS2 = FVector::DotProduct(NormalDiff2, Edge2);
	const float CurvatureS3 = FVector::DotProduct(NormalDiff3, Edge3);

	// Principal curvatures K1 and K2 (simplified approximation based on triangle geometry)
	const float K1 = FMath::Max3(FMath::Abs(CurvatureS1), FMath::Abs(CurvatureS2), FMath::Abs(CurvatureS3)); // Maximum curvature
	const float K2 = FMath::Min3(FMath::Abs(CurvatureS1), FMath::Abs(CurvatureS2), FMath::Abs(CurvatureS3)); // Minimum curvature

	// Calculate curvature magnitude
	const float CurvatureMagnitude = FMath::Sqrt(K1 * K1 + K2 * K2);
	
	return CurvatureMagnitude;
}

FSonoTraceUEGeneratedInputStruct ASonoTraceUEActor::GenerateInputSettings(const USonoTraceUEInputSettingsData* InputSettings, TMap<UObject*, int32>* AssetToObjectTypeIndexSettings)
{

	FSonoTraceUEGeneratedInputStruct GeneratedInputSettings;	

	GeneratedInputSettings.LoadedEmitterPositions = PopulatePositions(InputSettings->EnableEmitterPositionsDataTable, InputSettings->EmitterPositionsDataTable, InputSettings->EmitterPositions, FString(TEXT("emitters")), MaxEmitterCount);
	GeneratedInputSettings.LoadedReceiverPositions = PopulatePositions(InputSettings->EnableReceiverPositionsDataTable, InputSettings->ReceiverPositionsDataTable, InputSettings->ReceiverPositions, FString(TEXT("receivers")));	
	GeneratedInputSettings.ObjectSettings = PopulateObjectSettings(InputSettings, AssetToObjectTypeIndexSettings);
	GeneratedInputSettings.Frequencies = GenerateLinearSpacedArray(InputSettings->MinimumSimFrequency, InputSettings->MaximumSimFrequency, InputSettings->NumberOfSimFrequencies);

	GeneratedInputSettings.EmitterSignals.SetNum(InputSettings->EmitterSignals.Num());
	for (int32 EmitterSignalIndex = 0; EmitterSignalIndex < InputSettings->EmitterSignals.Num(); ++EmitterSignalIndex)
	{
		TArray<float> EmitterSignal;
		for (const FRichCurveKey& Key : InputSettings->EmitterSignals[EmitterSignalIndex].GetRichCurveConst()->GetConstRefOfKeys())
		{
			float Value =  InputSettings->EmitterSignals[EmitterSignalIndex].GetRichCurveConst()->Eval(Key.Time); 
			EmitterSignal.Add(Value); 
		}
		GeneratedInputSettings.EmitterSignals[EmitterSignalIndex] = EmitterSignal;
	}
	
	
	TArray<float> LoadedEmitterDirectivity;
	if(InputSettings->EnableEmitterDirectivity && InputSettings->EmitterDirectivity.Num() != GeneratedInputSettings.LoadedEmitterPositions.Num())
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Emitter directivity is enabled and the directivity values do not match the amount of emitters. Setting all to 0 (omnidirectional)."));
		LoadedEmitterDirectivity.Empty();
		LoadedEmitterDirectivity.Init(0.0f, GeneratedInputSettings.LoadedEmitterPositions.Num());
	}else
	{
		LoadedEmitterDirectivity.Empty();
		LoadedEmitterDirectivity.Append(InputSettings->EmitterDirectivity);
	}
	
	TArray<float> LoadedReceiverDirectivity;
	if(InputSettings->EnableReceiverDirectivity && InputSettings->ReceiverDirectivity.Num() != GeneratedInputSettings.LoadedReceiverPositions.Num())
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Receiver directivity is enabled and the directivity values do not match the amount of receivers. Setting all to 0 (omnidirectional)."));
		LoadedReceiverDirectivity.Empty();
		LoadedReceiverDirectivity.Init(0.0f, GeneratedInputSettings.LoadedReceiverPositions.Num());
	}else
	{
		LoadedReceiverDirectivity.Empty();
		LoadedReceiverDirectivity.Append(InputSettings->ReceiverDirectivity);
	}

	for (int32 EmitterIndex = 0; EmitterIndex < GeneratedInputSettings.LoadedEmitterPositions.Num(); ++EmitterIndex)
	{
		GeneratedInputSettings.FinalEmitterPositions.Add(GeneratedInputSettings.LoadedEmitterPositions[EmitterIndex] + InputSettings->EmitterPositionsOffset);
	}
	GeneratedInputSettings.FinalEmitterDirectivities.Empty();
	if(InputSettings->EnableEmitterDirectivity)GeneratedInputSettings.FinalEmitterDirectivities.Append(LoadedEmitterDirectivity);	

	if(InputSettings->EnableEmitterPatternSimulation)
	{
		TArray<FVector> CircularArrayOffsets = GenerateCircularArray(InputSettings->EmitterPatternSpacing, InputSettings->EmitterPatternRadius, InputSettings->EmitterPatternHexagonalLattice, InputSettings->EmitterPatternPlane);
		
		GeneratedInputSettings.FinalReceiverPositions.Empty();
		GeneratedInputSettings.FinalReceiverDirectivities.Empty();
		for (int i = 0; i < GeneratedInputSettings.LoadedReceiverPositions.Num(); i++)
		{
			FVector CurrentReceiverOffset = GeneratedInputSettings.LoadedReceiverPositions[i];
			for (int j = 0; j < CircularArrayOffsets.Num(); j++)
			{
				if(InputSettings->EnableReceiverDirectivity)GeneratedInputSettings.FinalReceiverDirectivities.Add(LoadedReceiverDirectivity[i]);
				GeneratedInputSettings.FinalReceiverPositions.Add(CurrentReceiverOffset + CircularArrayOffsets[j] + InputSettings->ReceiverPositionsOffset);				
			}
		}
	}else{
		for (int32 ReceiverIndex = 0; ReceiverIndex < GeneratedInputSettings.LoadedReceiverPositions.Num(); ++ReceiverIndex)
		{
			GeneratedInputSettings.FinalReceiverPositions.Add(GeneratedInputSettings.LoadedReceiverPositions[ReceiverIndex] + InputSettings->ReceiverPositionsOffset);
		}
		GeneratedInputSettings.FinalReceiverDirectivities.Empty();
		if(InputSettings->EnableReceiverDirectivity)GeneratedInputSettings.FinalReceiverDirectivities.Append(LoadedReceiverDirectivity);	
	}	

	if (InputSettings->NumberOfInitialRays < 0)
	{
		SampleHorizontalSlice(InputSettings->NumberOfInitialRays, InputSettings->SensorLowerAzimuthLimit, InputSettings->SensorUpperAzimuthLimit,
			GeneratedInputSettings.AzimuthAngles, GeneratedInputSettings.ElevationAngles, true);
	}
	else
	{
		SampleSphereCap(InputSettings->NumberOfInitialRays, InputSettings->SensorLowerAzimuthLimit, InputSettings->SensorUpperAzimuthLimit,
			InputSettings->SensorLowerElevationLimit, InputSettings->SensorUpperElevationLimit,
			GeneratedInputSettings.AzimuthAngles, GeneratedInputSettings.ElevationAngles);
	}

	GeneratedInputSettings.DefaultEmitterSignalIndexes.Init(0, GeneratedInputSettings.FinalEmitterPositions.Num());
	if(InputSettings->DefaultEmitterSignalIndexes.Num() != GeneratedInputSettings.FinalEmitterPositions.Num())
	{
		UE_LOG(SonoTraceUE, Error, TEXT("Could not set emitter signal indexes to the provided default ones. The array is not the same size as the amount of emitters. Setting all to 0."));
	}else
	{
		for (int EmitterSignalArrayIndex = 0; EmitterSignalArrayIndex < InputSettings->DefaultEmitterSignalIndexes.Num(); EmitterSignalArrayIndex++)
		{
			if (InputSettings->DefaultEmitterSignalIndexes[EmitterSignalArrayIndex] < InputSettings->EmitterSignals.Num() && InputSettings->DefaultEmitterSignalIndexes[EmitterSignalArrayIndex] >= 0)
			{
				GeneratedInputSettings.DefaultEmitterSignalIndexes[EmitterSignalArrayIndex] = InputSettings->DefaultEmitterSignalIndexes[EmitterSignalArrayIndex];
			}else
			{
				UE_LOG(SonoTraceUE, Error, TEXT("Invalid default emitter signal index %i for emitter #%i. Setting it to 0."), InputSettings->DefaultEmitterSignalIndexes[EmitterSignalArrayIndex], EmitterSignalArrayIndex );
			}
		}	
	}		
	return GeneratedInputSettings;
}

TArray<float> ASonoTraceUEActor::GenerateLinearSpacedArray(float Start, float End, int32 NumValues)
{
	TArray<float> LinearArray;
	if (NumValues <= 1)
	{
		// If less than 2 values, just return the Start value
		LinearArray.Add(Start);
		return LinearArray;
	}

	// Calculate the spacing between values
	const float Step = (End - Start) / (NumValues - 1);

	// Populate the array with linearly spaced values
	for (int32 i = 0; i < NumValues; ++i)
	{
		LinearArray.Add(Start + i * Step);
	}

	return LinearArray;
}

TArray<FSonoTraceUEObjectSettingsStruct>  ASonoTraceUEActor::PopulateObjectSettings(const USonoTraceUEInputSettingsData* InputSettings, TMap<UObject*, int32>* AssetToObjectTypeIndexSettings)
{
	TArray<FSonoTraceUEObjectSettingsStruct> ObjectSettings;
	FSonoTraceUEObjectSettingsStruct NewObjectSetting;
	NewObjectSetting.Name = "Default";
	NewObjectSetting.Description = "Default Material. Applied to everything without a tag";
	NewObjectSetting.UniqueIndex = 0;
	NewObjectSetting.BrdfTransitionPosition = InputSettings->ObjectSettingsDefault.BrdfTransitionPosition;
	NewObjectSetting.BrdfTransitionSlope = InputSettings->ObjectSettingsDefault.BrdfTransitionSlope;
	NewObjectSetting.BrdfExponentsSpecular = GenerateLinearSpacedArray(InputSettings->ObjectSettingsDefault.BrdfExponentSpecularStart, InputSettings->ObjectSettingsDefault.BrdfExponentSpecularEnd, InputSettings->NumberOfSimFrequencies);
	NewObjectSetting.BrdfExponentsDiffraction = GenerateLinearSpacedArray(InputSettings->ObjectSettingsDefault.BrdfExponentDiffractionStart, InputSettings->ObjectSettingsDefault.BrdfExponentDiffractionEnd, InputSettings->NumberOfSimFrequencies);
	NewObjectSetting.MaterialsTransitionPosition = 0.4;
	NewObjectSetting.MaterialsTransitionSlope = 2;
	NewObjectSetting.MaterialStrengthsSpecular = GenerateLinearSpacedArray(InputSettings->ObjectSettingsDefault.MaterialStrengthSpecularStart, InputSettings->ObjectSettingsDefault.MaterialStrengthSpecularEnd, InputSettings->NumberOfSimFrequencies);
	NewObjectSetting.MaterialStrengthsDiffraction = GenerateLinearSpacedArray(InputSettings->ObjectSettingsDefault.MaterialStrengthDiffractionStart, InputSettings->ObjectSettingsDefault.MaterialStrengthDiffractionEnd, InputSettings->NumberOfSimFrequencies);

	// A slope of 1 feels "natural" but is too slow in material transition.
	// It should be 8 or 10 or so.
	// Therefore, this factor is added.
	constexpr float SigmoidSlopeMultiplier = 8;

	for (int32 FrequencyIndex = 0; FrequencyIndex < InputSettings->NumberOfSimFrequencies; ++FrequencyIndex)
	{
		float SurfaceBRDF = SigmoidMix(0, NewObjectSetting.BrdfTransitionSlope, SigmoidSlopeMultiplier * NewObjectSetting.BrdfTransitionPosition,
									   NewObjectSetting.BrdfExponentsDiffraction[FrequencyIndex], NewObjectSetting.BrdfExponentsSpecular[FrequencyIndex]);
		float SurfaceMaterial = SigmoidMix(0, SigmoidSlopeMultiplier * NewObjectSetting.MaterialsTransitionSlope, NewObjectSetting.MaterialsTransitionPosition,
						   NewObjectSetting.MaterialStrengthsDiffraction[FrequencyIndex], NewObjectSetting.MaterialStrengthsSpecular[FrequencyIndex]);
		NewObjectSetting.DefaultTriangleBRDF.Add(SurfaceBRDF);
		NewObjectSetting.DefaultTriangleMaterial.Add(SurfaceMaterial);
	}
	ObjectSettings.Add(NewObjectSetting);
	AssetToObjectTypeIndexSettings->Add(nullptr, 0);
	
	if (InputSettings->ObjectSettingsDataTable)
	{
		int32 UniqueIndexCounter = 1;
		static const FString ContextString(TEXT("ReceiverPositionsContext"));

		// Retrieve all rows of the DataTable
		TArray<FName> RowNames = InputSettings->ObjectSettingsDataTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (const FSonoTraceUEObjectSettingsTable* Row = InputSettings->ObjectSettingsDataTable->FindRow<FSonoTraceUEObjectSettingsTable>(RowName, ContextString))
			{
				FSonoTraceUEObjectSettingsStruct CurrentNewObjectSetting;
				CurrentNewObjectSetting.Name = RowName;
				CurrentNewObjectSetting.Description = Row->Description;
				CurrentNewObjectSetting.DrawDebugFirstOccurrence = Row->DrawDebugFirstOccurrence;
				if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Row->Asset))
				{
					CurrentNewObjectSetting.StaticMesh = StaticMesh;
					CurrentNewObjectSetting.IsStaticMesh = true;
				}
				else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Row->Asset))
				{
					CurrentNewObjectSetting.SkeletalMesh = SkeletalMesh;
					CurrentNewObjectSetting.IsSkeletalMesh = true;
				}
				CurrentNewObjectSetting.UniqueIndex = UniqueIndexCounter;
				CurrentNewObjectSetting.BrdfTransitionPosition = Row->ObjectSettings.BrdfTransitionPosition;
				CurrentNewObjectSetting.BrdfTransitionSlope = Row->ObjectSettings.BrdfTransitionSlope;
				CurrentNewObjectSetting.BrdfExponentsSpecular = GenerateLinearSpacedArray(Row->ObjectSettings.BrdfExponentSpecularStart, Row->ObjectSettings.BrdfExponentSpecularEnd, InputSettings->NumberOfSimFrequencies);
				CurrentNewObjectSetting.BrdfExponentsDiffraction = GenerateLinearSpacedArray(Row->ObjectSettings.BrdfExponentDiffractionStart, Row->ObjectSettings.BrdfExponentDiffractionEnd, InputSettings->NumberOfSimFrequencies);
				CurrentNewObjectSetting.MaterialsTransitionPosition = Row->ObjectSettings.MaterialsTransitionPosition;
				CurrentNewObjectSetting.MaterialsTransitionSlope = Row->ObjectSettings.MaterialSTransitionSlope;
				CurrentNewObjectSetting.MaterialStrengthsSpecular = GenerateLinearSpacedArray(Row->ObjectSettings.MaterialStrengthSpecularStart, Row->ObjectSettings.MaterialStrengthSpecularEnd, InputSettings->NumberOfSimFrequencies);
				CurrentNewObjectSetting.MaterialStrengthsDiffraction = GenerateLinearSpacedArray(Row->ObjectSettings.MaterialStrengthDiffractionStart, Row->ObjectSettings.MaterialStrengthDiffractionEnd, InputSettings->NumberOfSimFrequencies);
				for (int32 FrequencyIndex = 0; FrequencyIndex < InputSettings->NumberOfSimFrequencies; ++FrequencyIndex)
				{
					float SurfaceBRDF = SigmoidMix(0, SigmoidSlopeMultiplier * CurrentNewObjectSetting.BrdfTransitionSlope, CurrentNewObjectSetting.BrdfTransitionPosition,
												   CurrentNewObjectSetting.BrdfExponentsDiffraction[FrequencyIndex], CurrentNewObjectSetting.BrdfExponentsSpecular[FrequencyIndex]);
					float SurfaceMaterial = SigmoidMix(0, SigmoidSlopeMultiplier * CurrentNewObjectSetting.MaterialsTransitionSlope, CurrentNewObjectSetting.MaterialsTransitionPosition,
									   CurrentNewObjectSetting.MaterialStrengthsDiffraction[FrequencyIndex], CurrentNewObjectSetting.MaterialStrengthsSpecular[FrequencyIndex]);
					CurrentNewObjectSetting.DefaultTriangleBRDF.Add(SurfaceBRDF);
					CurrentNewObjectSetting.DefaultTriangleMaterial.Add(SurfaceMaterial);
				}
				ObjectSettings.Add(CurrentNewObjectSetting);
				AssetToObjectTypeIndexSettings->Add(Row->Asset, UniqueIndexCounter);
				UniqueIndexCounter++;
			}
		}
	}
	else
	{
		UE_LOG(SonoTraceUE, Warning, TEXT("ObjectSettingsDataTable is null. Only default material will be used."));
	}
	return ObjectSettings;
}

TArray<FVector> ASonoTraceUEActor::PopulatePositions(const bool EnableTable, const UDataTable* DataTable, const TArray<FVector>& Positions, const FString LogString, const int32 MaxCount)
{
	TArray<FVector> LoadedPositions; 
	if (EnableTable)
	{
		if (DataTable)
		{
			static const FString ContextString(TEXT("PositionsContext"));
			for (TArray<FName> RowNames = DataTable->GetRowNames(); const FName& RowName : RowNames)
			{
				if (const FSonoTraceUECoordinateTable* Row = DataTable->FindRow<FSonoTraceUECoordinateTable>(RowName, ContextString))
				{
					FVector NewPosition(Row->X, Row->Y, Row->Z);
					LoadedPositions.Add(NewPosition);
				}
				if (MaxCount > 0 && LoadedPositions.Num() == MaxCount)
				{
					UE_LOG(SonoTraceUE, Warning, TEXT("Maximum position count of %i reached for the %s. Not adding other set positions."), MaxCount, *LogString);
					break;
				}
			}
		}
		else
		{
			UE_LOG(SonoTraceUE, Warning, TEXT("Positions Data Table for the %s is not set."), *LogString);
			if (Positions.IsEmpty())
			{
				LoadedPositions.Add(FVector());
				UE_LOG(SonoTraceUE, Warning, TEXT("No manual positions set for the %s, adding single one at origin of sensor."), *LogString);
			}else
			{
				LoadedPositions = Positions;
				if (MaxCount > 0 && LoadedPositions.Num() > MaxCount)
				{
					UE_LOG(SonoTraceUE, Warning, TEXT("Maximum position count of %i reached for the %s. Removing all with higher index."), MaxCount, *LogString);
					LoadedPositions.RemoveAt(MaxCount, LoadedPositions.Num() - MaxCount);
					LoadedPositions.Shrink();
				}
			}			
		}
	}else
	{
		LoadedPositions = Positions;
	}
	if (LoadedPositions.Num() == 0)
	{
		LoadedPositions.Add(FVector());
		UE_LOG(SonoTraceUE, Warning, TEXT("No positions set for the %s, adding single one at origin of sensor."), *LogString);
	}
	return LoadedPositions;
}

void ASonoTraceUEActor::SampleHorizontalSlice(int NumRays, const float LowerAzimuthLimit, const float UpperAzimuthLimit, TArray<float> &AzimuthAngles, TArray<float> &ElevationAngles, bool EnableRadians) {
	NumRays = -NumRays;

	const float AngleStep =(UpperAzimuthLimit - LowerAzimuthLimit) / (NumRays - 1);
	for (auto i = 0; i < NumRays; ++i)
	{
		float Angle = LowerAzimuthLimit + i * AngleStep;
		if (EnableRadians) FMath::DegreesToRadians(Angle);
		AzimuthAngles.Add(Angle);
		ElevationAngles.Add(0);
	}
}

void ASonoTraceUEActor::SampleSphereCap(const int NumRays, const float LowerAzimuthLimit, const float UpperAzimuthLimit, const float LowerElevationLimit, const float UpperElevationLimit, TArray<float> &AzimuthAngles, TArray<float> &ElevationAngles, const bool EnableRadians) {

	// Add point in the frontal direction
	AzimuthAngles.Add(0);
	ElevationAngles.Add(0);

	const float SurfaceArea = (FMath::DegreesToRadians(UpperAzimuthLimit) - FMath::DegreesToRadians(LowerAzimuthLimit)) * (FMath::Sin(FMath::DegreesToRadians(UpperElevationLimit)) - FMath::Sin(FMath::DegreesToRadians(LowerElevationLimit)));
	const float SurfaceScaler = 4 * PI / SurfaceArea;
	const int NumSpherePoints = FMath::CeilToInt(NumRays * SurfaceScaler);

	// Generate points on the sphere, retain those within the opening angle
	const float Offset = 2.0f / NumSpherePoints;
	const float Increment = PI * (3.0f - FMath::Sqrt(5.0f));
	for (auto i = 1; i <= NumSpherePoints; ++i)
	{
		if (AzimuthAngles.Num() == NumRays)
			return;
		
		const float Y = i * Offset - 1 + Offset / 2.0f;
		const float R = FMath::Sqrt(1 - FMath::Pow(Y ,2));
		const float Phi = (i + 1) % NumSpherePoints * Increment;
		const float X = FMath::Cos(Phi) * R;
		const float Z = FMath::Sin(Phi) * R;
		
		float Az = FMath::RadiansToDegrees(FMath::Atan2(Y, X));
		float El = FMath::RadiansToDegrees(FMath::Atan2(Z, FMath::Sqrt(FMath::Pow(X, 2) + FMath::Pow(Y, 2))));
		if (Az > LowerAzimuthLimit && Az < UpperAzimuthLimit && El > LowerElevationLimit && El < UpperElevationLimit)
		{
			if (EnableRadians)
			{
				Az = FMath::DegreesToRadians(Az);
				El = FMath::DegreesToRadians(El);
			}
			AzimuthAngles.Add(Az);
			ElevationAngles.Add(El);
		}		
	}
}

TArray<FVector> ASonoTraceUEActor::GenerateCircularArray(float DistanceMicrophones, float ArrayRadius, bool HexagonalLattice, ESonoTraceUEArrayPlaneEnum ArrayPlane)
{
    TArray<FVector> CircularArray;
    
    // Convert inputs to meters
    const float D = DistanceMicrophones / 100.0f; 
    const float R = ArrayRadius / 100.0f;    
   
    float Dy;
    float Dx;
    bool isHexagonal;

    if (!HexagonalLattice) // Square Lattice
    {
        isHexagonal = false;
        Dy = D;
        Dx = D;
    }
    else // Default to Hexagonal Lattice
    {
        isHexagonal = true;
        Dy = FMath::Sqrt(3.0f) / 2.0f * D;
        Dx = D;
    }

    const int Ny = FMath::FloorToInt(R / Dy); 
    
    for (int i = -Ny; i <= Ny; ++i)
    {
       TArray<float> XArray;
       const float y = Dy * i;
       
       float MaxX = FMath::Sqrt(FMath::Max(0.0f, FMath::Pow(R, 2) - FMath::Pow(y, 2)));
       
       if (isHexagonal)
       {
          int Nx;    
          if (i % 2 == 0) 
          {
             Nx = FMath::FloorToInt(MaxX / Dx);
             for (int j = -Nx; j <= Nx; ++j)
             {
                XArray.Add(j * Dx); 
             }
          }
          else 
          {
             Nx = FMath::FloorToInt((MaxX - Dx / 2.0f) / Dx); 
             
             for (int j = -Nx - 1; j <= Nx; ++j)
             {
                float x = (j + 0.5f) * Dx;
                
             
                if (FMath::Abs(x) <= MaxX + 1e-5f) 
                {
                    XArray.Add(x);
                }
             }
          }
       }
       else 
       {
           int Nx = FMath::FloorToInt(MaxX / Dx);
           for (int j = -Nx; j <= Nx; ++j)
           {
              XArray.Add(j * Dx); 
           }
       }
       
       for (const float x : XArray)
       {
       	FVector NewPoint;
       		switch (ArrayPlane)
       		{
       		case ESonoTraceUEArrayPlaneEnum::YZ:
       			NewPoint = FVector(0, -x * 100.0f, y * 100.0f);
       			break;
       		case ESonoTraceUEArrayPlaneEnum::XZ:
       			NewPoint = FVector(-x * 100.0f, 0, y * 100.0f);
       			break;
       		case ESonoTraceUEArrayPlaneEnum::XY:
       			NewPoint = FVector(-x * 100.0f, y * 100.0f, 0);
       			break;
       		default:
       			// Fallback to YZ
       			NewPoint = FVector(0, -x * 100.0f, y * 100.0f);
       			break;
       		}
       	CircularArray.Add(NewPoint);
       }
    }
	UE_LOG(SonoTraceUE, Log, TEXT("Generating circular array creates %i total virtual receivers for each real receiver."), CircularArray.Num());
    return CircularArray;
}

TArray<uint8> ASonoTraceUEActor::SerializeObjectSettingsStruct(FSonoTraceUEObjectSettingsStruct* ObjectSettingsStruct)
{
	TArray<uint8> ByteArray;
	FMemoryWriter Writer(ByteArray, true);

	const FString NameString = ObjectSettingsStruct->Name.ToString();
	const FTCHARToUTF8 UTF8NameConverter(*NameString);
	int NameLength = UTF8NameConverter.Length();
	Writer << NameLength;
	Writer.Serialize((void*)UTF8NameConverter.Get(), UTF8NameConverter.Length());	
	Writer << ObjectSettingsStruct->UniqueIndex;
	Writer << ObjectSettingsStruct->IsStaticMesh;
	Writer << ObjectSettingsStruct->IsSkeletalMesh;

	if (ObjectSettingsStruct->IsStaticMesh)
	{
		const FTCHARToUTF8 UTF8ResourceConverter(*ObjectSettingsStruct->StaticMesh->GetPathName());
		int ResourceLength = UTF8ResourceConverter.Length();
		Writer << ResourceLength;
		Writer.Serialize((void*)UTF8ResourceConverter.Get(), UTF8ResourceConverter.Length());	
	}else if (ObjectSettingsStruct->IsSkeletalMesh)
	{
		const FTCHARToUTF8 UTF8ResourceConverter(*ObjectSettingsStruct->SkeletalMesh->GetPathName());
		int ResourceLength = UTF8ResourceConverter.Length();
		Writer << ResourceLength;
		Writer.Serialize((void*)UTF8ResourceConverter.Get(), UTF8ResourceConverter.Length());	
	}else
	{
		const FString DefaultString = FString("Default");
		const FTCHARToUTF8 UTF8ResourceConverter(*DefaultString);
		int ResourceLength = UTF8ResourceConverter.Length();
		Writer << ResourceLength;
		Writer.Serialize((void*)UTF8ResourceConverter.Get(), UTF8ResourceConverter.Length());	
	}
	
	const FTCHARToUTF8 UTF8DescriptionConverter(*ObjectSettingsStruct->Description);
	int DescriptionLength = UTF8DescriptionConverter.Length();
	Writer << DescriptionLength;
	Writer.Serialize((void*)UTF8DescriptionConverter.Get(), UTF8DescriptionConverter.Length());

	Writer << ObjectSettingsStruct->DrawDebugFirstOccurrence;
	
	Writer << ObjectSettingsStruct->BrdfTransitionPosition;
	Writer << ObjectSettingsStruct->BrdfTransitionSlope;
	for (float SpecularExponent : ObjectSettingsStruct->BrdfExponentsSpecular)
	{
		Writer << SpecularExponent;
	}
	for (float DiffractionExponent : ObjectSettingsStruct->BrdfExponentsDiffraction)
	{
		Writer << DiffractionExponent;
	}
	for (float DefaultExponent : ObjectSettingsStruct->DefaultTriangleBRDF)
	{
		Writer << DefaultExponent;
	}
	
	Writer << ObjectSettingsStruct->MaterialsTransitionPosition;
	Writer << ObjectSettingsStruct->MaterialsTransitionSlope;
	for (float SpecularExponent : ObjectSettingsStruct->MaterialStrengthsSpecular)
	{
		Writer << SpecularExponent;
	}
	for (float DiffractionExponent : ObjectSettingsStruct->MaterialStrengthsDiffraction)
	{
		Writer << DiffractionExponent;
	}
	for (float DefaultExponent : ObjectSettingsStruct->DefaultTriangleMaterial)
	{
		Writer << DefaultExponent;
	}

	return ByteArray;
}

TArray<uint8> ASonoTraceUEActor::SerializePointStruct(FSonoTraceUEPointStruct* PointStruct){
	TArray<uint8> ByteArray;
	FMemoryWriter Writer(ByteArray, true);

	Writer << PointStruct->Location.X;
	Writer << PointStruct->Location.Y;
	Writer << PointStruct->Location.Z;
	Writer << PointStruct->ReflectionDirection.X;
	Writer << PointStruct->ReflectionDirection.Y;
	Writer << PointStruct->ReflectionDirection.Z;
	Writer << PointStruct->Index;
	Writer << PointStruct->SummedStrength;
	Writer << PointStruct->TotalDistance;

	int32 TotalDistancesCount = PointStruct->TotalDistancesFromEmitters.Num();
	Writer << TotalDistancesCount;
	
	for (float EmitterDistance : PointStruct->TotalDistancesFromEmitters)
	{
		Writer << EmitterDistance;
	}
	
	Writer << PointStruct->DistanceToSensor;
	Writer << PointStruct->ObjectTypeIndex;
	Writer << PointStruct->CurvatureMagnitude;
	Writer << PointStruct->IsHit;
	Writer << PointStruct->IsLastHit;
	Writer << PointStruct->IsSpecular;
	Writer << PointStruct->IsDiffraction;
	Writer << PointStruct->IsDirectPath;
	Writer << PointStruct->RayIndex;
	Writer << PointStruct->BounceIndex;
	
	int32 TotalEmitterDirectivitiesCount = PointStruct->EmitterDirectivities.Num();
	Writer << TotalEmitterDirectivitiesCount;
	
	for (float EmitterDirectivity : PointStruct->EmitterDirectivities)
	{
		Writer << EmitterDirectivity;
	}	

	for (const TArray<TArray<float>>& EmitterRow : PointStruct->Strengths)
	{
		for (const TArray<float>& ReceiverRow : EmitterRow)
		{
			for (float FrequencyValue : ReceiverRow)
			{
				Writer << FrequencyValue;
			}
		}
	}
	
	for (const TArray<float>& EmitterDistanceRow : PointStruct->TotalDistancesToReceivers)
	{
		for (float ReceiverDistanceValue : EmitterDistanceRow)
		{
			Writer << ReceiverDistanceValue;
		}
	}

	const FString LabelString = PointStruct->Label.ToString();
	const FTCHARToUTF8 UTF8Converter(*LabelString); 

	// Write UTF-8 bytes of the label to the ByteArray
	Writer.Serialize((void*)UTF8Converter.Get(), UTF8Converter.Length());


	return ByteArray; 
}

void ASonoTraceUEActor::DrawDebugNonSymmetricalFrustum(const UWorld* InWorld, const FTransform& StartTransform, const float LowerAzimuthLimit, const float UpperAzimuthLimit, const float LowerElevationLimit, const float UpperElevationLimit, const float Distance, FColor const& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
{
	const float MinAzimuthRad = FMath::DegreesToRadians(LowerAzimuthLimit);
	const float MaxAzimuthRad = FMath::DegreesToRadians(UpperAzimuthLimit);
	const float MinElevationRad = FMath::DegreesToRadians(LowerElevationLimit);
	const float MaxElevationRad = FMath::DegreesToRadians(UpperElevationLimit);
	const float HalfFarWidthUpper = Distance * FMath::Tan(MaxAzimuthRad);
	const float HalfFarWidthLower = Distance * FMath::Tan(MinAzimuthRad);
	const float HalfFarHeightUpper = Distance * FMath::Tan(MaxElevationRad);
	const float HalfFarHeightDownLower = Distance * FMath::Tan(MinElevationRad);

	FVector FrustumCorners[8]; 
	const FVector NearPlaneCenter = FVector::ZeroVector; // Sensor position
	FrustumCorners[0] = NearPlaneCenter; // All 4 corners for the near plane
	FrustumCorners[1] = NearPlaneCenter;
	FrustumCorners[2] = NearPlaneCenter;
	FrustumCorners[3] = NearPlaneCenter;

	// Far plane vertices
	FrustumCorners[4] = FVector(Distance, HalfFarWidthUpper, HalfFarHeightUpper);   // Top-Right
	FrustumCorners[5] = FVector(Distance, HalfFarWidthLower, HalfFarHeightUpper);  // Top-Left
	FrustumCorners[6] = FVector(Distance, HalfFarWidthLower, HalfFarHeightDownLower); // Bottom-Left
	FrustumCorners[7] = FVector(Distance, HalfFarWidthUpper, HalfFarHeightDownLower);  // Bottom-Right

	// Apply the sensor transform to align the frustum
	for (int32 i = 0; i < 8; ++i)
	{
		FrustumCorners[i] = StartTransform.TransformPosition(FrustumCorners[i]);
	}

	// Draw lines between the transformed corners to draw the frustum
	DrawDebugLine(InWorld, FrustumCorners[0], FrustumCorners[4], Color, bPersistentLines, LifeTime, DepthPriority); // Bottom Right
	DrawDebugLine(InWorld, FrustumCorners[0], FrustumCorners[5], Color, bPersistentLines, LifeTime, DepthPriority); // Top Right
	DrawDebugLine(InWorld, FrustumCorners[0], FrustumCorners[6], Color, bPersistentLines, LifeTime, DepthPriority); // Bottom Left
	DrawDebugLine(InWorld, FrustumCorners[0], FrustumCorners[7], Color, bPersistentLines, LifeTime, DepthPriority); // Top Left
	DrawDebugLine(InWorld, FrustumCorners[4], FrustumCorners[5], Color, bPersistentLines, LifeTime, DepthPriority); // Top Edge
	DrawDebugLine(InWorld, FrustumCorners[5], FrustumCorners[6], Color, bPersistentLines, LifeTime, DepthPriority); // Left Edge
	DrawDebugLine(InWorld, FrustumCorners[6], FrustumCorners[7], Color, bPersistentLines, LifeTime, DepthPriority); // Bottom Edge
	DrawDebugLine(InWorld, FrustumCorners[7], FrustumCorners[4], Color, bPersistentLines, LifeTime, DepthPriority); // Right Edge
}
