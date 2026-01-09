// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details.

#pragma once

#include "CoreMinimal.h"
#include "SonoTrace.h"
#include "ColorMaps.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "SceneInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#pragma warning(disable: 4668)
#include "ObjectDeliverer/Public/DeliveryBox/Utf8StringDeliveryBox.h"
#include "ObjectDeliverer/Public/DeliveryBox/ObjectDeliveryBoxUsingJson.h"
#include "ObjectDeliverer/Public/ObjectDelivererManager.h"
#include "SonoTraceUEActor.generated.h"

struct FDiffractionImportancePair
{
	float Value;    
	int32 Index;  

	FDiffractionImportancePair(const float InValue, const int32 InIndex)
		: Value(InValue), Index(InIndex) {}

	bool operator<(const FDiffractionImportancePair& Other) const
	{
		return Value < Other.Value;
	}
};

USTRUCT()
struct FSonoTraceUEMeshDataStruct
{
	GENERATED_BODY()

	TArray<float> TriangleCurvatureMagnitude;
	TArray<float> TriangleSize;
	TArray<TArray<float>> TriangleBRDF; // Triangle // Frequency
	TArray<TArray<float>> TriangleMaterial; // Triangle // Frequency
	TArray<FVector> TriangleNormal;
	TArray<FVector> TrianglePosition;
	TArray<float> ImportanceVertexOrderedBRDFValue;
	TArray<int32> ImportanceVertexOrderedIndex;

	FSonoTraceUEMeshDataStruct() {}
};

USTRUCT(BlueprintType)
struct SONOTRACEUE_API FSonoTraceUEObjectSettingsStruct
{
	GENERATED_BODY()

	FName Name;
	int32 UniqueIndex = 0;
    bool IsStaticMesh = false;
	bool IsSkeletalMesh = false;
	UPROPERTY()
	UStaticMesh* StaticMesh = nullptr;
	UPROPERTY()
	USkeletalMesh* SkeletalMesh = nullptr;
	FString Description;
	bool DrawDebugFirstOccurrence = false;
	
	// BRDF SETTINGS
	float BrdfTransitionPosition = 0;
	float BrdfTransitionSlope = 0;
	TArray<float> BrdfExponentsSpecular;	
	TArray<float> BrdfExponentsDiffraction;
	TArray<float> DefaultTriangleBRDF;
	
	// MATERIAL SETTINGS
	float MaterialsTransitionPosition = 0;
	float MaterialsTransitionSlope = 0;
	TArray<float> MaterialStrengthsSpecular;
	TArray<float> MaterialStrengthsDiffraction;
	TArray<float> DefaultTriangleMaterial;
};

USTRUCT(BlueprintType)
struct SONOTRACEUE_API FSonoTraceUEObjectSettingsOriginStruct
{
	GENERATED_BODY()

	// BRDF SETTINGS
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfTransitionPosition = 0.4;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfTransitionSlope = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfExponentSpecularStart = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfExponentSpecularEnd = 5;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfExponentDiffractionStart = 70;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BRDF")
	float BrdfExponentDiffractionEnd =  70;

	// MATERIAL SETTINGS

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialsTransitionPosition = 0.4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialSTransitionSlope = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialStrengthSpecularStart = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialStrengthSpecularEnd = 8;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialStrengthDiffractionStart = 0.025;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	float MaterialStrengthDiffractionEnd =  0.05;
};

USTRUCT(BlueprintType)
struct SONOTRACEUE_API FSonoTraceUEObjectSettingsTable : public FTableRowBase
{
	GENERATED_BODY()

	// Currently, only StaticMesh and SkeletalMesh are supported
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(AllowedClasses = "StaticMesh,SkeletalMesh"), Category = "SonoTraceUE")
    UObject* Asset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE")
	FString Description = "Description goes here.";	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE")
	FSonoTraceUEObjectSettingsOriginStruct ObjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE")
	bool DrawDebugFirstOccurrence = false;
};

UENUM(BlueprintType)
enum class ESonoTraceUESimulationDrawSizeModeEnum : uint8
{
	Static UMETA(DisplayName = "Static"),
	Strength UMETA(DisplayName = "Strength"),
};

UENUM(BlueprintType)
enum class ESonoTraceUESimulationDrawColorModeEnum : uint8
{
	Static UMETA(DisplayName = "Static"),
	TotalDistance UMETA(DisplayName = "Total distance"),
	SensorDistance UMETA(DisplayName = "Distance to sensor"),
	Strength UMETA(DisplayName = "Strength"),
	Curvature UMETA(DisplayName = "Curvature"),
};

UENUM(BlueprintType)
enum class ESonoTraceUEMeshModeEnum : uint8
{
	Curvature UMETA(DisplayName = "Curvature"),
	SurfaceBRDF UMETA(DisplayName = "Surface opening angle"),
	SurfaceMaterial UMETA(DisplayName = "Surface reflection strength"),
	Normal UMETA(DisplayName = "Surface normal"),
	Size UMETA(DisplayName = "Triangle size"),
};

UENUM(BlueprintType)
enum class ESonoTraceUEArrayPlaneEnum : uint8
{
	YZ UMETA(DisplayName = "Y-Z Plane (X = 0)"),
	XZ UMETA(DisplayName = "X-Z Plane (Y = 0)"),
	XY UMETA(DisplayName = "X-Y Plane (Z = 0)")
};

UCLASS(BlueprintType)
class SONOTRACEUE_API USonoTraceUEInterfaceSettingsData : public UDataAsset
{

	GENERATED_BODY()
	
public:

	// Enable the usage of the TCP interface for API functionality
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	bool EnableInterface = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	FString InterfaceIP = "localhost";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta=(ClampMin=1024, ClampMax=65535))
	int32 InterfacePort = 9099;

	// To optimize the data load, sub output is disabled by default
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	bool EnableSubOutput = false;
};

UCLASS(BlueprintType)
class SONOTRACEUE_API USonoTraceUEInputSettingsData : public UDataAsset
{

	GENERATED_BODY()
	
public:
	
	// EMITTER SETTINGS

	// The first signal is considered the default 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter")
	TArray<FRuntimeFloatCurve> EmitterSignals;
		
	// In centimeters and using a left-handed coordinate system. This offset will be applied to all emitter coordinates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter", meta=(Units="Centimeters"))
	FVector EmitterPositionsOffset = FVector(0, 0, 0);

	// Use a referenced datatable as input for the emitter coordinates 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter")
	bool EnableEmitterPositionsDataTable = true;

	// In centimeters and using a left-handed coordinate system. For loading the emitter coordinates from a file
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter", meta=(EditCondition="EnableEmitterPositionsDataTable", EditConditionHides))
	UDataTable* EmitterPositionsDataTable;

	// In centimeters and using a left-handed coordinate system. For manually defining the emitter coordinates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter", meta=(EditCondition="!EnableEmitterPositionsDataTable", EditConditionHides))
	TArray<FVector>EmitterPositions;

	// The index of the emitter signal to be used by each emitter.
	// Should match the size of the provided emitter coordinates.
	// If this is empty and not set, it will be set to 0 for all emitters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Emitter", meta=(EditCondition="!EnableEmitterPositionsDataTable", EditConditionHides))
	TArray<int32> DefaultEmitterSignalIndexes;

	// RECEIVER SETTINGS

	// Enable this to stop updating the receiver positions on every simulation. They can still be moved through the transformation functions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers")
	bool EnableStaticReceivers = false;

	// When using static receivers, this can be enabled to interpret the receiver positions as being world coordinates and not local to the sensor transform
	// If disabled, it will use the transform of the sensor on the first frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers", meta=(EditCondition="EnableStaticReceivers", EditConditionHides))
	bool EnableUseWorldCoordinatesReceivers = true;
	
	// In centimeters and using a left-handed coordinate system. This offset will be applied to all receiver coordinates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers", meta=(Units="Centimeters"))
	FVector ReceiverPositionsOffset = FVector(0, 0, 0);

	// Use a referenced datatable as input for the receiver coordinates 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers")
	bool EnableReceiverPositionsDataTable = true;

	// In centimeters and using a left-handed coordinate system. For loading the receiver coordinates from a file
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers", meta=(EditCondition="EnableReceiverPositionsDataTable", EditConditionHides))
	UDataTable* ReceiverPositionsDataTable;

	// In centimeters and using a left-handed coordinate system. For manually defining the receiver coordinates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers", meta=(EditCondition="!EnableReceiverPositionsDataTable", EditConditionHides))
	TArray<FVector> ReceiverPositions;

	// Simulate the circular emitter pattern on each receiver 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
	bool EnableEmitterPatternSimulation = false;

	// In centimeters and using a left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern", meta=(Units="Centimeters"))
	float EmitterPatternRadius = 1.25;

	// In centimeters and using a left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern", meta=(Units="Centimeters"))
	float EmitterPatternSpacing = 1.25;

    // Enable to use a hexagonal lattice for the emitter pattern simulation instead of the square lattice which is default
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
    bool EmitterPatternHexagonalLattice = false;

	// Set the plane where the generated circular array will exist within (YZ (default)/XZ/XY)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
	ESonoTraceUEArrayPlaneEnum EmitterPatternPlane = ESonoTraceUEArrayPlaneEnum::YZ;
	
	// SIMULATION SETTINGS

	// General toggle of the entire simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableSimulation = true;

	// Enable the raytracing. This is enabled automatically if the specular component simulation is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableRaytracing = true;

	// Enable specular component simulation with raytracing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableSpecularComponentCalculation = true;

	// Enable diffraction component simulation with Monte Carlo technique
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableDiffractionComponentCalculation = true;

	// Toggle the calculation for the direct path transmission between the emitters and the receivers if there is LOS between them
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableDirectPathComponentCalculation = false;

	// Only execute a simulation when running one of the available ways to trigger a new measurement (ex. blueprint)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableRunSimulationOnlyOnTrigger = true;

	// The coordinates of the point data are by default in the sensor reference frame.
	// Set this as false to change to world frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool PointsInSensorFrame = true;

	// To optimize the data load, sub output is disabled by default
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableSimulationSubOutput = false;

	// The rate in Hz that will be attempted to be simulated when not running only on trigger
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=1, ClampMax=60), meta=(Units="Hertz"))
	float SimulationRate = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, ClampMax=500))
	int32 NumberOfSimFrequencies = 14;

	// In Hertz
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, ClampMax=2000000), meta=(Units="Hertz"))
	int32 MinimumSimFrequency = 20000;

	// In Hertz
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, ClampMax=2000000), meta=(Units="Hertz"))
	int32 MaximumSimFrequency = 85000;
	
	// In Hertz
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, Units="Hertz"))
	int32 SampleRate = 450000;

	// In m/s
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, Units="MetersPerSecond"))
	float SpeedOfSound = 343;
	
	// The base strength to use for an emitter transmission when calculating the direct path component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	float DirectPathStrength = 100;

	// The minimum strength value (across all frequencies)
	// of a reflected point to be saved during simulation of the specular reflection component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	float SpecularMinimumStrength = 0;

	// The minimum summed strength value (across all frequencies)
	// of a reflected point to be saved during simulation of the diffraction reflection component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	float DiffractionMinimumStrength = 0;

	// A cut-off threshold for the size of triangles in centimeters squared.
	// Triangles have to be larger to be considered for diffraction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0))
	float DiffractionTriangleSizeThreshold = 200;

	// Based on the initial number of rays
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(ClampMin=0, Units="Multiplier"))
	int32 DiffractionSimDivisionFactor = 25;

	// For each diffraction point, make sure it has LOS to the emitter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableDiffractionLineOfSightRequired = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableDiffractionForDynamicObjects = false;

	// Only calculate the BRDF specular strength of the last hit for every initial ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General")
	bool EnableSpecularSimulationOnlyOnLastHits = false;

	// Amount ticks a manually added object is attempted to be added to the mesh data before giving up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|General", meta=(Units="Times"))
	int32 MeshDataGenerationAttempts = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects")
	UDataTable* ObjectSettingsDataTable;

	// If no tag is detected, these settings are applied by default
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects")
	FSonoTraceUEObjectSettingsOriginStruct ObjectSettingsDefault = FSonoTraceUEObjectSettingsOriginStruct();

	// General scale factor for the curvature magnitude calculation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects", meta=(ClampMin=0, Units="Multiplier"))
	float CurvatureScale = 1;

	// For the curvature calculation,
	// a scaling effect is applied on the curvature magnitude influenced by the triangle size.
	// // This can be toggled on and off with this setting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects")
	bool EnableCurvatureTriangleSizeBasedScaler = true;

	// For the curvature calculation a scaling function is applied on the curvature magnitude
	// influenced by the triangle size.
	// This variable specifies the minimum value for the scaling factor when the triangle area is very small.
	// This ensures that tiny triangles still contribute to the curvature but with a minimal effect.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects", meta=(ClampMin=0, ClampMax=1, Units="Multiplier", EditCondition="EnableCurvatureTriangleSizeBasedScaler", EditConditionHides))
	float CurvatureScalerMinimumEffect = 0.02;
	
	// For the curvature calculation a scaling function is applied on the curvature magnitude
	// influenced by the triangle size.
	// This variable specifies the maximum value for the scaling factor
	// when the triangle area is at the upper triangle size threshold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects", meta=(ClampMin=1, Units="Multiplier", EditCondition="EnableCurvatureTriangleSizeBasedScaler", EditConditionHides))
	float CurvatureScalerMaximumEffect = 2;

	// For the curvature calculation,
	// a scaling effect is applied on the curvature magnitude influenced by the triangle size.
	// This variable in centimeters squared defines the lower threshold for the triangle size.
	// Between 0 and this threshold,
	// the scaling effect on curvature rises linearly between the minimum scaler effect and 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects", meta=(ClampMin=0, EditCondition="EnableCurvatureTriangleSizeBasedScaler", EditConditionHides))
	float CurvatureScalerLowerTriangleSizeThreshold = 0.5;

	// For the curvature calculation,
	// a scaling effect is applied on the curvature magnitude influenced by the triangle size.
	// This variable in centimeters squared defines the upper threshold for the triangle size.
	// Between the lower threshold and upper threshold,
	// the scaling effect on curvature rises linearly between 1 and the maximum scaler effect.
	// Beyond this point, until the variable is set as Diffraction Triangle Size Threshold, it will linearly drop to 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Objects", meta=(ClampMin=0, EditCondition="EnableCurvatureTriangleSizeBasedScaler", EditConditionHides))
	float CurvatureScalerUpperTriangleSizeThreshold = 5;
	
	// In degrees, left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=-90, ClampMax=90, Units="Degrees"))
	float SensorLowerAzimuthLimit = -45;

	// In degrees, left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=-90, ClampMax=90, Units="Degrees"))
	float SensorUpperAzimuthLimit = 45; 

	// In degrees, left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing",meta=(ClampMin=-90, ClampMax=90, Units="Degrees"))
	float SensorLowerElevationLimit = -45; 

	// In degrees, left-handed coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=-90, ClampMax=90, Units="Degrees"))
	float SensorUpperElevationLimit = 45;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=0, ClampMax=1000000))
	int32 NumberOfInitialRays = 50000; 

	// In centimeters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=0, ClampMax=1000000, Units="Centimeters"))
	float MaximumRayDistance = 500;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Configuration|Simulation|Raytracing", meta=(ClampMin=1, ClampMax=10))
	int32 MaximumBounces = 3;

	// DRAW SETTINGS

	// Global toggle of debug drawing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDraw = true;

	// Toggle the drawing of emitter pose as a coordinates system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawSensorPose = true;
	
	// Draws the frustum in white lines that describes the measurement region for simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawSensorFrustum = true;

	// Toggle for drawing all the emitters as purple points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawAllEmitters = true;
	
	// Toggle the drawing of a configured receiver poses as orange points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawLoadedReceivers = true;

	// Toggle for drawing all the receivers as including those generated by the circular emitter pattern as yellow points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawAllReceivers = false;

	// Draw a green or red point on each receiver, depending on if there is LOS for the direct path component calculation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawDirectPathLOS = false;

	// Draws the points for all the components combined
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool EnableDrawPoints = true;

	// For performance only a selection of rays and points is drawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw", meta=(ClampMin=0, ClampMax=10000))
	int32 MaximumDrawNumber = 1000;

	// Randomize the drawing selection (if disabled, a fixed step size is used)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	bool RandomizeDrawSelection = true;

	// The color mode of the points 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	ESonoTraceUESimulationDrawColorModeEnum DrawPointsColorMode = ESonoTraceUESimulationDrawColorModeEnum::Strength;

	// The size mode of the points 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw")
	ESonoTraceUESimulationDrawSizeModeEnum DrawPointsSizeMode = ESonoTraceUESimulationDrawSizeModeEnum::Static;

	// Default size of the points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	int32 DrawDefaultPointsSize = 10;

	// Default color of the points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	FColor DrawDefaultPointsColor = FColor(0, 255, 0);	

	// Color map used for the color of the points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	ESonoTraceUEColorMapEnum DrawPointsColorMap = ESonoTraceUEColorMapEnum::Parula;

	// Enable auto-scaling of the maximum value to normalize the data for when in total distance mode for size or color
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	bool DrawPointsTotalDistanceMaximumAutoScale = true;

	// Enable auto-scaling of the maximum value to normalize the data for when in curvature mode for size or color
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	bool DrawPointsCurvatureMaximumAutoScale = true;

	// Enable auto-scaling of the maximum value to normalize the data when in strength mode for size or color
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	bool DrawPointsStrengthMaximumAutoScale  = true;
	
	// The maximum value in centimeters to normalize the data for when in total distance mode for size or color when not auto-scaling
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details", meta=(Units="Centimeters"))
	float DrawPointsTotalDistanceMaximumValue = 1000;

	// The maximum value to normalize the data for when in curvature mode for size or color when not auto-scaling
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details")
	float DrawPointsCurvatureMaximumValue = 1;

	// The maximum value in centimeters to normalize the data when in strength mode for size or color when not auto-scaling
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Draw|Details", meta=(Units="Centimeters"))
	float DrawPointsStrengthMaximumValue = 5;
	
	// DEBUG SETTINGS

	// Log the execution times
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDebugLogExecutionTimes = true;

	// Global toggle of debug drawing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebug = false;

	// Toggle the drawing of emitter pose as a coordinates system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugSensorPose = true;

	// Draws the frustum that describes the measurement region for simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugSensorFrustum = true;

	// Toggle for drawing all the emitters as purple points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugAllEmitters = true;
	
	// Toggle the drawing of a configured receiver poses as orange points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugLoadedReceivers = false;

	// Toggle for drawing all the receivers as including those generated by the circular emitter pattern as yellow points
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugAllReceivers = true;

	// Draw turquoise lines between the sensor and the receivers, useful for when direct path is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugSensorToReceiverLines = false;

	// Draw a green or red point on each receiver,
	// depending on if there is LOS for the direct path component calculation with a direction vector to the sensor.
	// In the case of no LOS, it will also draw a orange point and direction vector on the hit location that blocked that LOS.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugDirectPathLOS = false;

	// Draws the points of the raytracing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugPoints = true;

	// For performance only a selection of rays and points is drawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug", meta=(ClampMin=0, ClampMax=10000))
	int32 MaximumDrawDebugRaysNumber = 1000;

	// Randomize the drawing selection (if disabled, a fixed step size is used)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool RandomizeDrawDebugRaysSelection = true;

	// Draws the hit ray tracing lines in green
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugRaysHitLines = false;

	// Draws the hit ray tracing points in green
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugRaysHitPoints = true;

	// Draws the last reflection direction from the hit points of the ray tracing in blue
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugRaysLastHitReflection = false;

	// Draws the initial rays that missed during ray tracing in red
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugRaysInitialMisses = false;
	
	// Globally toggle the drawing of pre-calculated mesh data if enabled in the individual object settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool EnableDrawDebugMeshData = false;

	// The color mode of the points for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	ESonoTraceUEMeshModeEnum DrawDebugMeshColorMode = ESonoTraceUEMeshModeEnum::Curvature;

	// The maximum points to draw for the mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	int32 DrawDebugMeshMaximumPoints = 10000;

	// Randomize the drawing selection for the mesh data (if disabled, a fixed step size is used)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	bool RandomizeDrawDebugMeshData = true;
	
	// Size of the points for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	int32 DrawDebugMeshSize = 5;

	// Color map used for the color of the points for mesh data. Does not apply when showing surface normal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	ESonoTraceUEColorMapEnum DrawDebugMeshColorMap = ESonoTraceUEColorMapEnum::Hot;

	// The minimum and maximum values  to normalize the data for when in curvature mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	FVector2D DrawDebugMeshCurvatureLimits = FVector2D(0, 0.5);

	// The minimum and maximum values  to normalize the data for when in opening angle mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	FVector2D DrawDebugMeshOpeningAngleLimits = FVector2D(0, 70);

	// The minimum and maximum values  to normalize the data for when in reflection strength mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	FVector2D DrawDebugMeshReflectionStrengthLimits = FVector2D(0,  8);

	// The minimum and maximum values to normalize the data for when in surface area mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	FVector2D DrawDebugMeshTriangleSizeLimits = FVector2D(0,  200);

	// The frequency bin index to use for the data for when in opening angle mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	int32 DrawDebugMeshOpeningAngleFrequencyIndex = 1;

	// The frequency bin index to use for the data for when in reflection strength mode for mesh data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Debug")
	int32 DrawDebugMeshReflectionStrengthFrequencyIndex = 1;
};

USTRUCT(BlueprintType)
struct FSonoTraceUEGeneratedInputStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<float> AzimuthAngles;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<float> ElevationAngles;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<FVector> LoadedEmitterPositions;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<FVector> FinalEmitterPositions;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<FVector> LoadedReceiverPositions;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<FVector> FinalReceiverPositions;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<FSonoTraceUEObjectSettingsStruct> ObjectSettings;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<float> Frequencies;

	TArray<TArray<float>> EmitterSignals;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Generatedinput")
	TArray<int32> DefaultEmitterSignalIndexes;

	FSonoTraceUEGeneratedInputStruct()		
	{
	}
};

USTRUCT(BlueprintType)
struct FSonoTraceUECoordinateTable : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positions", meta=(Units="Centimeters"))
	float X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positions", meta=(Units="Centimeters"))
	float Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positions", meta=(Units="Centimeters"))
	float Z;

	FSonoTraceUECoordinateTable()
		: X(0.f), Y(0.f), Z(0.f)
	{
	}
};

USTRUCT(BlueprintType)
struct FSonoTraceUEPointStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	FVector Location = FVector();	

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	FVector ReflectionDirection = FVector();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	FName Label;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	int Index;

	// all emitters are added up
	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	float SummedStrength; 

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	float TotalDistance;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	TArray<float> TotalDistancesFromEmitters;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	float DistanceToSensor;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	int ObjectTypeIndex;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	bool IsHit;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	bool IsLastHit;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	float CurvatureMagnitude;
	
	TArray<float>* SurfaceBRDF;	
	TArray<float>* SurfaceMaterial;	
	TArray<TArray<TArray<float>>> Strengths; // Emitter // Receiver // Frequency
	TArray<TArray<float>> TotalDistancesToReceivers; // Emitter // Receiver

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	bool IsSpecular = true;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	bool IsDiffraction = false;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Point")
	bool IsDirectPath = false;	

	FSonoTraceUEPointStruct(const FVector& Location, const FVector& ReflectionDirection, const FName Label, const int Index,
	                         const float TotalDistance, const TArray<float>& TotalDistancesFromEmitters,
	                         const float DistanceToSensor, const int ObjectTypeIndex, const float CurvatureMagnitude,
	                         TArray<float>* SurfaceBRDF, TArray<float>* SurfaceMaterial):
		Location(Location),
	    ReflectionDirection(ReflectionDirection),
		Label(Label),
		Index(Index),
		SummedStrength(0),
		TotalDistance(TotalDistance),
		TotalDistancesFromEmitters(TotalDistancesFromEmitters),
		DistanceToSensor(DistanceToSensor),
		ObjectTypeIndex(ObjectTypeIndex),
		IsHit(true),
		IsLastHit(false),
		CurvatureMagnitude(CurvatureMagnitude),
		SurfaceBRDF(SurfaceBRDF),
		SurfaceMaterial(SurfaceMaterial)
	{
	}

	FSonoTraceUEPointStruct():
		Index(-1),
		SummedStrength(0),
		TotalDistance(0),
		DistanceToSensor(0),
		ObjectTypeIndex(0),
		IsHit(false),
		IsLastHit(false),
		CurvatureMagnitude(0),
		SurfaceBRDF(nullptr),
		SurfaceMaterial(nullptr),
		IsSpecular(false)
	{
	}

	FSonoTraceUEPointStruct(const FVector& Location, const FVector& ReflectionDirection, const FName Label, const int Index,
							 const float TotalDistance, const float DistanceToSensor, const float SummedStrength, const TArray<TArray<float>>& TotalDistancesToReceivers,
							 const TArray<TArray<TArray<float>>>& Strengths):
		Location(Location),
		ReflectionDirection(ReflectionDirection),
		Label(Label),
		Index(Index),
		SummedStrength(SummedStrength),
		TotalDistance(TotalDistance),
		DistanceToSensor(DistanceToSensor),
		ObjectTypeIndex(-1),
		IsHit(true),
		IsLastHit(false),
		CurvatureMagnitude(0),
		SurfaceBRDF(nullptr),
		SurfaceMaterial(nullptr),
		Strengths(Strengths),
		TotalDistancesToReceivers(TotalDistancesToReceivers),
		IsSpecular(false),
		IsDirectPath(true)
	{
	}
};

USTRUCT(BlueprintType)
struct FSonoTraceUESubOutputStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	TArray<FSonoTraceUEPointStruct> ReflectedPoints;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	TArray<float> ReflectedStrengths;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	TArray<int32> HitPersistentPrimitiveIndexes;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	double Timestamp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	float MaximumStrength = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	float MaximumCurvature = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|SubOutput")
	float MaximumTotalDistance = 0;
	
	FSonoTraceUESubOutputStruct()
	{
	}
};

USTRUCT(BlueprintType)
struct FSonoTraceUEOutputStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FSonoTraceUESubOutputStruct SpecularSubOutput;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FSonoTraceUESubOutputStruct DiffractionSubOutput;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FSonoTraceUESubOutputStruct DirectPathSubOutput;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	TArray<FSonoTraceUEPointStruct> ReflectedPoints;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	float MaximumStrength = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	float MaximumCurvature = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	float MaximumTotalDistance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	TArray<int32> EmitterSignalIndexes;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FVector SensorLocation = FVector();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FRotator SensorRotation = FRotator();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FVector SensorToOwnerTranslation = FVector();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FRotator SensorToOwnerRotation = FRotator();
	
	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FVector OwnerLocation = FVector();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FRotator OwnerRotation = FRotator();

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	TArray<FTransform> EmitterPoses;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	TArray<FTransform> ReceiverPoses;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	TArray<bool> DirectPathLOS;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	double Timestamp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SonoTraceUE|Output")
	int32 Index = -1;	
	
	FSonoTraceUEOutputStruct()		
	{
	}
};

USTRUCT(NotBlueprintType)
struct FSonoTraceUEDataMessage
{
	GENERATED_BODY()
	
	int32 Type;
	TArray<int32> Order;
	TArray<FString> Strings;
	TArray<int32> Integers;
	TArray<float> Floats;	
	
	FSonoTraceUEDataMessage(): Type(0)
	{
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FInterfaceDataMessageReceivedEvent, int32, Type, const TArray<int32>&, Order, const TArray<FString>&, Strings, const TArray<int32>&, Integers, const TArray<float>&, Floats);

UCLASS()
class SONOTRACEUE_API ASonoTraceUEActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ASonoTraceUEActor();
	virtual void Tick(float DeltaTime) override;
	virtual void BeginDestroy() override;

	/**
	* An event that can be registered to when the interface API receives a message. It can contain various datatypes.
	* @param Type An int32 that describes what type of message it is. This helps identify the message variant. 
	* @param Order An Array of int32 that describes the order of the data.
	*        When it is a 0 it means the next data element is a String, 1 means an Integer, and 2 means a float is next.
	* @param Strings An Array of String that holds the String-based data.
	* @param Integers An Array of int32 that holds the Integer-based data.
	* @param Floats An Array of float that holds the Float-based data.
	*/
	UPROPERTY(BlueprintAssignable, Category = "SonoTraceUE")
	FInterfaceDataMessageReceivedEvent InterfaceDataMessageReceivedEvent;

	/**
	* A function that allows sending a message containing various data over the interface API.
	* @param Type Describes what type of message it is. This helps identify the message variant. 
	* @param Order An Array that describes the order of the data.
	*        When it is a 0 it means the next data element is a String, 1 means an Integer, and 2 means a float is next.
	* @param Strings An Array of String that holds the String-based data.
	* @param Integers An Array of int32 that holds the Integer-based data.
	* @param Floats An Array of float that holds the Float-based data.
	* @return Returns true if the data message was sent successfully.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SendInterfaceDataMessage(const int32 Type, const TArray<int32> Order, const TArray<FString> Strings, const TArray<int32> Integers, const TArray<float> Floats);


	/**
	* Trigger a single execution of the simulation.
	* Requires EnableRunSimulationOnlyOnTrigger to be enabled in the input settings.
	* @return Returns true if the simulation trigger was executed successfully.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool TriggerSimulation();
	
	/**
	* Trigger a single execution of the simulation. Requires EnableRunSimulationOnlyOnTrigger to be enabled in the input settings.
	* @param OverrideEmitterSignalIndexes If given, these indexes set the emitter signal to be used by each emitter in order.
	*                                     Should match the size of the provided emitter coordinates.
	*                                     This is only temporary for this run and does not get saved for future measurements.
	* @return Returns true if the simulation trigger was executed successfully.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool TriggerSimulationOverrideEmitterSignals(const TArray<int32> OverrideEmitterSignalIndexes);

	/**
	* Set the current active and default emitter signal indexes to be used by each emitter in order. 
	* @param EmitterSignalIndexes The new active emitter signal indexes. Should match the size of the provided emitter coordinates.
	* @return Returns true if the emitter signal was correctly set to the requested indexes for all emitters.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetCurrentEmitterSignalIndexes(TArray<int32> EmitterSignalIndexes);

	/**
	* Get the indexes of the current active and default emitter signals for each emitter.
	* @return The indexes of the current active and default emitter signals for each emitter in the same order.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	TArray<int32> GetCurrentEmitterSignalIndexes();

	/**
	* Set the current active and default emitter signal index for a specific emitter.
	* @param EmitterIndex The index of the emitter to set.
	* @param EmitterSignalIndex The new active emitter signal index.
	* @return Returns true if the emitter signal was correctly set.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetCurrentEmitterSignalIndexForSpecificEmitter(int32 EmitterIndex, int32 EmitterSignalIndex);

	/**
	* Get the index of the current active and default emitter signal for a specific emitter.
	* @param EmitterIndex The index of the emitter to get the active emitter signal index for.
	* @return The index of the current active and default emitter signal for the requested emitter.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	int32 GetCurrentEmitterSignalIndexForSpecificEmitter(int32 EmitterIndex);

	/**
	* Get the number of available emitter signals.
	* @return The number of available emitter signals.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	int32 GetEmitterSignalCount() const;

	/**
	* Add an Actor to the SonoTraceUE mesh analysis system. Individual child components will be automatically parsed.
	* If this contains any new mesh resource that is the first instance in the scene, it will load and parse this mesh data.
	* @param Actor The new Actor to add.
	* @return Returns true if the actor was successfully added.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE", meta=(HidePin = "OverrideInitialization, UpdateTable"))
	bool AddActor(AActor* Actor, const bool OverrideInitialization = false, const bool UpdateTable = true);
	
	/**
	* Add a StaticMeshComponent to the SonoTraceUE mesh analysis system.
	* If this mesh resource is the first instance in the scene, it will load and parse this mesh data.
	* @param MeshComponent The new StaticMeshComponent to add.
	* @param ObjectNamePrefix The prefix to add to the label of this mesh. Usually this is the name of the Actor that owns this component.
	* @return Returns true if the mesh component was successfully added.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE", meta=(HidePin = "OverrideInitialization, UpdateTable, OverrideAddingToLoadList, PreviousAttempts"))
	bool AddStaticMeshComponent(UStaticMeshComponent* MeshComponent, FString ObjectNamePrefix, const bool OverrideInitialization = false, const bool UpdateTable = true, const bool OverrideAddingToLoadList = false, const int32 PreviousAttempts = 0);

	/**
	* Add a SkeletalMeshComponent to the SonoTraceUE mesh analysis system.
	* If this mesh resource is the first instance in the scene, it will load and parse this mesh data.
	* @param MeshComponent The new SkeletalMeshComponent to add.
	* @param ObjectNamePrefix The prefix to add to the label of this mesh. Usually this is the name of the Actor that owns this component.
	* @return Returns true if the mesh component was successfully added.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE", meta=(HidePin = "OverrideInitialization, UpdateTable, OverrideAddingToLoadList, PreviousAttempts"))
	bool AddSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, FString ObjectNamePrefix, const bool OverrideInitialization = false, const bool UpdateTable = true, const bool OverrideAddingToLoadList = false, const int32 PreviousAttempts = 0);

	/**
	* Remove an Actor from the SonoTraceUE mesh analysis system.
	* If this contains any mesh resource that is the last instance in the scene, it will unload this mesh data.
	* @param Actor The Actor to remove.
	* @return Returns true if the mesh component was successfully removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE", meta=(HidePin = "UpdateTable"))
	bool RemoveActor(AActor* Actor, const bool UpdateTable = true);

	/**
	* Remove a StaticMeshComponent of the SonoTraceUE mesh analysis system.
	* If this mesh resource is the last instance in the scene, it will unload this mesh data.
	* @param MeshComponent The StaticMeshComponent to remove.
	* @return Returns true if the mesh component was successfully removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE", meta=(HidePin = "UpdateTable"))
	bool RemoveStaticMeshComponent(UStaticMeshComponent* MeshComponent, const bool UpdateTable = true);

	/**
	* Remove a SkeletalMeshComponent of the SonoTraceUE mesh analysis system.
	* If this mesh resource is the last instance in the scene, it will unload this mesh data.
	* @param MeshComponent The SkeletalMeshComponent to remove.
	* @return Returns true if the mesh component was successfully removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool RemoveSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, const bool UpdateTable = true);

	/**
	* Set a new position coordinate for the emitters of the sensor. 
	* @param EmitterIndexes The indexes of the emitters to alter the position of.
	* @param NewEmitterPositions The new position coordinates to use in centimeters and using a left-handed coordinate system.
	* @param RelativeTransform Enable to make the new emitter position be added to the previous set position and use the input given as a translation.
	* @param ReApplyOffset Enable to re-apply the original global emitter offset to the new emitter position if not using relative transform.
	* @return Returns true if all requested emitters positions have successfully been updated.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetNewEmitterPositions(const TArray<int32>& EmitterIndexes, const TArray<FVector>& NewEmitterPositions, const bool RelativeTransform = false, const bool ReApplyOffset = true);

	/**
	* Set a new position coordinate for the receivers of the sensor.
	* Note that it uses the final generated receiver positions, so those after optionally generated the emitter pattern.
	* Note that if static receivers are enabled and as well as the usage of world coordinates, this will interpret the new positions as world coordinates and not relative to the original starting position.
    * @param ReceiverIndexes The indexes of the receivers to alter the position of.
    * @param NewReceiverPositions The new position coordinates to use in centimeters and using a left-handed coordinate system.
    * @param RelativeTransform Enable to make the new receiver position be added to the previous set position and use the input given as a translation.
    * @param ReApplyOffset Enable to re-apply the original global receiver offset to the new receiver position if not using relative transform.
    * @return Returns true if all requested emitters positions have successfully been updated.
    */
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetNewReceiverPositions(const TArray<int32>& ReceiverIndexes, const TArray<FVector>& NewReceiverPositions, const bool RelativeTransform = false, const bool ReApplyOffset = true);

	/**
	* Set a new relative transformation of the sensor with respect to its owner if it exists
	* This will update the transform used to position the sensor in relation to the owner's transform.
	* @param NewSensorToOwnerTranslation The new relative translation to set.
	* @param NewSensorToOwnerRotator The new relative rotation to set.
	* @return Returns true if the relative transform was successfully updated and an owner is available.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetNewSensorRelativeTransform(const FVector& NewSensorToOwnerTranslation, const FRotator& NewSensorToOwnerRotator);

	/**
	* Set a new absolute transformation for the sensor.
	* This will update the sensor's transform in the world coordinate space.
	* @param NewSensorTranslation The new absolute world location to set for the sensor actor.
	* @param NewSensorRotator The new absolute world rotation to set for the sensor actor. 
	* @param Teleport			Whether to teleport the physics state (if physics collision is enabled for this object).
	 *							If true, physics velocity for this object is unchanged (so ragdoll parts are not affected by change in location).
	 *							If false, physics velocity is updated based on the change in position (affecting ragdoll parts).
	 *							If CCD is on and not teleporting, this will affect objects along the entire swept volume.
	 *                          Note that when teleporting, any child/attached components will be teleported too,
	 *                          maintaining their current offset even if they are being simulated.
	 *                          Setting the location without teleporting will not update the location of simulated child/attached components.
	* @return Returns true if the absolute transform was successfully updated.
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetNewSensorWorldTransform(const FVector& NewSensorTranslation, const FRotator& NewSensorRotator, const ETeleportType Teleport = ETeleportType::None);

	/**
	* Set a new absolute transformation for the sensor's owner if it is available. 
	* This will update the owner's transform in the world coordinate space.
	* @param NewOwnerTranslation The new absolute world location to set for the owner of the sensor.
	* @param NewOwnerRotator The new absolute world rotation to set for the owner of the sensor.
	* @param Teleport			Whether to teleport the physics state (if physics collision is enabled for this object).
	 *							If true, physics velocity for this object is unchanged (so ragdoll parts are not affected by change in location).
	 *							If false, physics velocity is updated based on the change in position (affecting ragdoll parts).
	 *							If CCD is on and not teleporting, this will affect objects along the entire swept volume.
	 *                          Note that when teleporting, any child/attached components will be teleported too,
	 *                          maintaining their current offset even if they are being simulated.
	 *                          Setting the location without teleporting will not update the location of simulated child/attached components.
	* @return Returns true if the owner's absolute transform was successfully updated (if the owner available).
	*/
	UFUNCTION(BlueprintCallable, Category = "SonoTraceUE")
	bool SetNewSensorOwnerWorldTransform(const FVector& NewOwnerTranslation, const FRotator& NewOwnerRotator, const ETeleportType Teleport = ETeleportType::None);

	UFUNCTION()
	void InterfaceOnConnect(const UObjectDelivererProtocol* ClientSocket);

	UFUNCTION()
	void InterfaceOnDisconnect(const UObjectDelivererProtocol* ClientSocket);

	UFUNCTION()
	void InterfaceOnReceive(const UObjectDelivererProtocol* ClientSocket, const TArray<uint8>& Buffer);

	UFUNCTION()
	void InterfaceOnReceiveString(const FString& ReceivedString, const UObjectDelivererProtocol* FromObject);

	// When this is true, the EnableSimulation variable overrides the Input Settings Data Table mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Input")
	bool EnableSimulationEnableOverride = false;

	// Available when setting EnableSimulationEnableOverride to true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Input", meta=(EditCondition = "EnableSimulationEnableOverride", EditConditionHides))
	bool EnableSimulation = true;

	// Input settings data table, when it is not assigned and not retrieved from the interfaceAPI will use default settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Input")
	USonoTraceUEInputSettingsData* InputSettings;

	// When this is true, the EnableInterface, InterfaceIP, and InterfacePort variables override the Interface Settings Data Table values
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Interface")
	bool EnableInterfaceEnableOverride = false;

	// Available when setting EnableInterfaceEnableOverride to true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Interface", meta=(EditCondition = "EnableInterfaceEnableOverride", EditConditionHides))
	bool EnableInterface = false;

	// If not set, it will use the default settings and the interface will be disabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SonoTraceUE|Interface")
	USonoTraceUEInterfaceSettingsData* InterfaceSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FSonoTraceUEGeneratedInputStruct GeneratedSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SonoTraceUE|Output")
	FSonoTraceUEOutputStruct CurrentOutput;	
	
protected:
	virtual void BeginPlay() override;

	void GenerateAllInitialMeshData();
	void UpdateScenePrimitiveIndexToPersistentPrimitiveIndexTable();
	void UpdateTransformations();
	void UpdateInterface();
	void SendInterfaceSettings();
	void SendInterfaceData();
	void SendInterfaceMeasurement();
	void UpdateShaderParameters();
	bool ExecuteRayTracingOnce(const TArray<int32> OverrideEmitterSignalIndexes);
	void ParseRayTracing();	
	void RunSimulation(const TArray<int32> OverrideEmitterSignalIndexes);
	void PrepareInterfaceMeasurementData(const FSonoTraceUEOutputStruct& Output);
	void DrawSimulationResult();
	void DrawSimulationDebug();
	void DrawMeshDebug(const UMeshComponent* MeshComponent, FSonoTraceUEMeshDataStruct& NewMeshData) const;

	static void MergeEmitterPatternImpulseResponses(const int32 OriginalReceiverCount, const int32 NewReceiverCount, const int NumberOfIRSamples, TArray<TArray<float>>* ImpulseResponses);
	static TArray<float> Interpolate(const TArray<float>& X, const TArray<float>& Y, const TArray<float>& Xq);
	static TArray<float> NormLog(const TArray<float>& MatIn, float ThreshDB);
	static TArray<float> Convolve(const TArray<float>& Signal1, const TArray<float>& Signal2, bool bSame);
	static void CircShift(TArray<float>& Signal, int32 Shift);	
	static float SigmoidMix(const float X, const float Slope, const float Center, const float Value1, const float Value2);
	static void GenerateBRDFAndMaterial(const FSonoTraceUEObjectSettingsStruct* ObjectSettings, FSonoTraceUEMeshDataStruct* MeshData);
	static void CalculateMeshCurvature(UMeshComponent* MeshComponent, FSonoTraceUEMeshDataStruct& OutMeshData, const float CurvatureScaleFactor = 1, const bool EnableCurvatureTriangleSizeBasedScaler = true,
	                                   const float CurvatureScalerMinimumEffect = 0.05, const float CurvatureScalerMaximumEffect = 2, const float CurvatureScalerLowerTriangleSizeThreshold = 0.45, const float CurvatureScalerUpperTriangleSizeThreshold = 2, const float DiffractionTriangleSizeThreshold = 4);
	static FSonoTraceUEGeneratedInputStruct GenerateInputSettings(const USonoTraceUEInputSettingsData* InputSettings, TMap<UObject*, int32>* AssetToObjectTypeIndexSettings);
	static TArray<FSonoTraceUEObjectSettingsStruct> PopulateObjectSettings(const USonoTraceUEInputSettingsData* InputSettings, TMap<UObject*, int32>* AssetToObjectTypeIndexSettings);
	static TArray<FVector> PopulatePositions(const bool EnableTable, const UDataTable* DataTable, const TArray<FVector>& Positions, const FString LogString, const int32 MaxCount = 0);
	static void SampleHorizontalSlice(int NumRays, const float LowerAzimuthLimit, const float UpperAzimuthLimit, TArray<float> &AzimuthAngles, TArray<float> &ElevationAngles, const bool EnableRadians = true);
	static void SampleSphereCap(int NumRays, const float LowerAzimuthLimit, const float UpperAzimuthLimit, const float LowerElevationLimit, const float UpperElevationLimit, TArray<float> &AzimuthAngles, TArray<float> &ElevationAngles, const bool EnableRadians = true);
	static TArray<FVector> GenerateCircularArray(float DistanceMicrophones, float ArrayRadius, bool HexagonalLattice, ESonoTraceUEArrayPlaneEnum ArrayPlane);
	static TArray<float> GenerateLinearSpacedArray(float Start, float End, int32 NumValues);
	static FVector CalculateTrianglePosition(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3);
	static FVector CalculateTriangleNormal(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3);
	static float CalculateTriangleCurvature(const FVector3f& Vertex1, const FVector3f& Vertex2, const FVector3f& Vertex3, const FVector3f& Normal1, const FVector3f& Normal2, const FVector3f& Normal3);
	static TArray<uint8> SerializeObjectSettingsStruct(FSonoTraceUEObjectSettingsStruct* ObjectSettingsStruct);
	static TArray<uint8> SerializePointStruct(FSonoTraceUEPointStruct* PointStruct);
	static void DrawDebugNonSymmetricalFrustum(const UWorld* InWorld, const FTransform& StartTransform, const float LowerAzimuthLimit, const float UpperAzimuthLimit, const float LowerElevationLimit, const float UpperElevationLimit, const float Distance, FColor const& Color, bool bPersistentLines = false, float LifeTime=-1.f, uint8 DepthPriority = 0, float Thickness = 0.f);

	float TranscurredTime = 0;
	bool Initialized = false;
	bool AwaitingRayTracingResult = false;
	TArray<int32> TriggerTemporaryEmitterSignalIndexes;
	bool ReadyToUseRayTracingResult = false;
	bool CurrentlyParsingRaytracing = false;
	TArray<int32> CurrentEmitterSignalIndexes;
	
	FSonoTrace SonoTrace;
	FRandomStream RandomStream;
	FRHIGPUBufferReadback* GPUReadback = nullptr;
	FStructuredOutputBufferElem* RayTracingRawOutput = nullptr;
	TArray<TTuple<bool, FVector>> DirectPathReceiverOutput;
	FSonoTraceUESubOutputStruct RayTracingSubOutput;
	uint64 SonoTracePreviousIndex = 0;	
	double RayTracingLastLoggedTime = 0.0;        
	int32 RayTracingExecutionCount = 0;

	// Tuple of object name, mesh component, current attempts
	TArray<TTuple<FString, UStaticMeshComponent*, int32>> StaticMeshComponentsToLoad;
	TArray<TTuple<FString, USkeletalMeshComponent*, int32>> SkeletalMeshComponentsToLoad;
	
	UPROPERTY()
	TMap<UObject*, int32> AssetToObjectTypeIndexSettings;
	TMap<int32, TTuple<FName, int32>> PersistentPrimitiveIndexToLabelsAndObjectTypes;
	UPROPERTY()
	TMap<int32, UPrimitiveComponent*> PersistentPrimitiveIndexToPrimitiveComponent;
	TMap<int32, int32> PersistentPrimitiveIndexToMeshDataIndex;
	TArray<FSonoTraceUEMeshDataStruct> MeshData;
	UPROPERTY()
	TMap<UStaticMesh*, int32> StaticMeshToMeshDataIndex;
	UPROPERTY()
	TMap<USkeletalMesh*, int32> SkeletalMeshToMeshDataIndex;
	UPROPERTY()
	TMap<UStaticMesh*, int32> StaticMeshCounter;
	UPROPERTY()
	TMap<USkeletalMesh*, int32> SkeletalMeshCounter;
	TMap<int32, int32> ScenePrimitiveIndexToPersistentPrimitiveIndex;

	
	TArray<FTransform> EmitterPoses;
	TArray<FTransform> ReceiverPoses;
	FVector SensorLocation;
	FRotator SensorRotation;
	FVector SensorToOwnerTranslation;
	FRotator SensorToOwnerRotation;
	FVector OwnerLocation;
	FRotator OwnerRotation;
	TArray<float> DirectPathAzimuthAngles;
	TArray<float> DirectPathElevationAngles;
	FTransform StartingActorTransform;

    UPROPERTY()
	UObjectDelivererManager* ObjectDelivererManager;
	UPROPERTY()
	UUtf8StringDeliveryBox* Utf8StringDeliveryBox;
	bool InterfaceConnected = false;
	bool InterfaceSettingsEnabled = false;
	bool InterfaceReadyForSettings = false;
	bool InterfaceSettingsMessageAnnouncementSent = false;
	bool InterfaceSettingsMessageAnnouncementAck = false;
	bool InterfaceSettingsParsedAnnouncementAck = false;
	bool InterfaceSettingsDataSent = false;	
	bool InterfaceReadyForMessages = false;
	bool InterfaceMeasurementMessageAnnouncementSent = false;
	bool InterfaceMeasurementMessageAnnouncementAck = false;
	bool InterfaceDataMessageAnnouncementSent = false;
	bool InterfaceDataMessageAnnouncementAck = false;

	int32 LatestInterfaceMessageDataType;
	TArray<int32> LatestInterfaceMessageDataOrder;
	TArray<FString> LatestInterfaceMessageDataStrings;
	TArray<int32> LatestInterfaceMessageDataIntegers;
	TArray<float> LatestInterfaceMessageDataFloats;

	TArray<FSonoTraceUEOutputStruct> InterfaceMeasurementDataBuffer;
	TArray<FSonoTraceUEDataMessage> InterfaceDataMessageDataBuffer;
};
