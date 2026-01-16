# SonoTraceUE

An Unreal Engine 5 Plugin for in-air acoustic simulation using Hardware-Accelerated Ray Tracing, an
Unreal Engine implementation of the original [SonoTraceLab](https://github.com/Cosys-Lab/SonoTraceLab).

This branch has only the plugin source code. See other branches that also include a sample project. 

SonoTraceUE is a high-fidelity acoustic simulation plugin for Unreal Engine 5 that leverages hardware-accelerated ray tracing to simulate ultrasonic sensor behavior in complex 3D environments. The plugin provides physically-based acoustic propagation modeling, including specular reflection, diffraction components, and direct path transmission, making it suitable for research in robotics, autonomous systems, and acoustic sensor development. It has an API extension so it can be interfaced with from a external API client. 

## Requirements

- **Unreal Engine Version**: 5.4 or higher
- **Hardware Requirements**:
  - GPU with hardware ray tracing support
  - DirectX 12  support
- **Operating System**: Windows for now due to DirectX 12 requirement

Hardware ray tracing **must** be enabled in Unreal Engine for this plugin to function correctly:

1. Open **Project Settings** → **Engine** → **Rendering**
2. Under **Hardware Ray Tracing**, enable:
   - ☑ **Support Hardware Ray Tracing**
   - ☑ **Ray Traced Shadows**
3. Restart the editor after enabling these settings

## Quick Start

### 1. Add one or more SonoTraceUEActor objects to Your Scene

You can add it directly through the Place Actors panel or through a blueprint as a component for example. 

### 2. Configure Input Settings

1. In the **Content Browser**, create a new Data Asset of type `SonoTraceUEInputSettingsData`
2. Configure all settings such as the the emitter and receiver positions (see [Input Settings Configuration](#input-settings-configuration)), simulation parameters, object configuration and optionally visualisation and debugging settings.
3. Assign this Data Asset to the `InputSettings` property of your SonoTraceUEActor.

### 3. Run Simulation

- **Automatic Mode**: Set `EnableRunSimulationOnlyOnTrigger` to `false` for continuous simulation at the specified rate
- **Manual Mode**: Keep `EnableRunSimulationOnlyOnTrigger` as `true` and call `TriggerSimulation()` via Blueprint or C++ or the API.

## Example levels

There are 3 levels in this sample Unreal Project. They can all be found in the _/Content/SonoTraceUE/Levels_ folder.
They all us the same _InterfaceSettings_ and _CustomObjectSettings_ to load in the [API](#api) and [objects settings](#creating-new-object-settings) respectively.

 1. Default: Loaded by default in the Unreal Editor. Mostly tests different mesh types and uses an _active_ sensor controlled by the user input.
 2. Passive: as the name says, it uses only direct mode to sense passive with a moving source and a static receiver array.
 3. Test: a simple scene with some objects types, no simulation is performed. Only to show the curvature calculation for certain object types and how they are made. 

### 1. Default
In this mode the `SonoTraceUEActor` is attached to the player `UPawn`. It is using the _ActiveInputSettings_ as found in the _/Content/SonoTraceUE/_ folder. The environement is filled with objects of different types (static, skeletal, landscape, with/without nanite,etc.) for testing purposes. There are also a few custom blueprints that dynamicly spawn and move objects. 

When pressing play, the user can test a of the functions of the `SonoTraceUEActor` class. For the source code, check the Event Graph of _/Content/SonoTraceUE/Blueprints/SonoTraceUEPawn_.
 - Pressing Y toggles the measurement triggers through mouse clicking
 - Pressing the left mouse button triggers a measurement with the default active emitter
 - Pressing the right mouse button triggers a measurement with the override emitter index
 - Use + & - on the numpad to control the override active emitter index
 - Use / & * on the numpad to control the default active emitter index
 - Pressing U will change the sensor's relative transform with a small fixed offset
 - Pressing I will change the sensor's world transform with a small fixed offset
 - Pressing O will change the sensor's owner (the Pawn) transform with a small fixed offset

### 2. Passive
In this mode the `SonoTraceUEActor` is a component of a blueprint actor in the world, see the Event Graph in the _/Content/SonoTraceUE/Blueprints/PassiveStaticSonoTraceUEObject_ blueprint. It is using the _PassiveInputSettings_ as found in the _/Content/SonoTraceUE/_ folder. 

In this environment, a static receiver array is laying on the ground surface. An emitter source is constantly moving at a fixed rate between a left and right limit next to the cone (which blocks the line of sight between receivers and emitter). In this mode only direct path simulation is done. 
Additional blueprint code makes one single receiver also move up and down to show this blueprint functionality as well.

### 3. Test
In this mode the `SonoTraceUEActor` is directly placed in the world. It is using the _TestInputSettings_ as found in the _/Content/SonoTraceUE/_ folder. The simulation is disabled as this environment is mostly to show the curvature calculation with the debug visualisation of different mesh objects. It shows the limitations of the curvature calculation for very simplistic low resolution meshes compared to more custom-made meshes with more definition near edges. 

## Usage Tips

### Skeletal Meshes and Diffraction

By default, skeletal meshes should have diffraction disabled as the diffraction is based on the initial frame pre-calculation location of all triangles, and animations and deformations are not taking into account. Enable with:
```cpp
InputSettings->EnableDiffractionForDynamicObjects = true;
```

### Coordinate System

While within Unreal Engine (c++ and blueprint), all data uses the Unreal Engine coordinate system and its units:
Left-handed Z-up coordinate system
- **X**: Forward
- **Y**: Right  
- **Z**: Up
All coordinates use **centimeters** unless otherwise specified.

**Sensor Frame vs World Frame**:
- Set `PointsInSensorFrame = true` for ego-centric coordinates (robotics applications)
- Set `PointsInSensorFrame = false` for world-space coordinates


### Directivity Model
We implemented a simple directivity model that accounts for the fact that real-world transducers—whether biological (bat mouths and ears) or technical (speakers and microphones)—do not emit or receive sound equally in all directions. 
Instead of treating them as perfect omnidirectional spheres, the model abstracts it into a single tunable parameter. 
This allows the sensitivity pattern to transition smoothly from a uniform sphere to a focused, forward-facing shape, such as a cardioid pattern.
The Unreal Engine standardizes the X-axis of every actor as its "forward" direction, we do the same for the receiver and emitter. So take note of this if you enable directivity!  
During the raytracing simulation process, a weighting factor is applied based on the alignment between the ray's trajectory and this forward axis. 
For the emitter, this calculation occurs at the moment of launch and the first reflection (the first bounce), attenuating rays that exit sideways or backwards relative to the source. 
For the receiver, the calculation occurs upon arrival, dampening reflections that hit the sensor from extreme angles. 

### Creating new emitter signals

Emitter signals are defined as `FloatCurve` objects. You can load them in from CSV files. To do so, drag in a CSV file into the content browser and choose the `FloatCurve` class. An example CSV file can be found [here](/TestData/EmitterSignal_Generated_eRTIS_Sweep_25_80.csv).

### Creating new emitter and receiver array coordinate tables

To easily load in coordinates one can use our custom `DataTable` structure called `SonoTraceUECoordinateTable`. You can load them in from CSV files. To do so, drag in a CSV file into the content browser and choose the **DataTable** class and set the row type to `SonoTraceUECoordinateTable`. An example CSV file can be found [here](/TestData/ReceiverCoordinates_eRTIS_v3D1.csv).

### Creating new object settings

To define the BRDF properties of objects one can use our custom `DataTable` structure called `FSonoTraceUEObjectSettingsTable`. To create one:
1. Create a `DataTable` with row structure `FSonoTraceUEObjectSettingsTable`
2. Add rows for each mesh asset (StaticMesh or SkeletalMesh)
3. Configure BRDF and material properties per object. You can also set other settings like a description.

all other objects will use the default settings as set in the Input Settings.

## SonoTraceUE Actor

The primary actor class that manages the entire acoustic simulation pipeline. This section will go over its properties and functions.

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `InputSettings` | `USonoTraceUEInputSettingsData*` | Main configuration data asset for simulation parameters |
| `InterfaceSettings` | `USonoTraceUEInterfaceSettingsData*` | Configuration for TCP/IP interface |
| `EnableSimulationEnableOverride` | `bool` | Override simulation enable state from InputSettings |
| `EnableInterfaceEnableOverride` | `bool` | Override interface settings |
| `GeneratedSettings` | `FSonoTraceUEGeneratedInputStruct` | Read-only generated/processed settings |
| `CurrentOutput` | `FSonoTraceUEOutputStruct` | Most recent simulation output |

### Blueprint/C++ Functions

##### Simulation Control

```cpp
bool TriggerSimulation()
```
Triggers a single execution of the simulation. Requires `EnableRunSimulationOnlyOnTrigger` to be enabled.

**Returns**: `true` if simulation was successfully triggered.

---

```cpp
bool TriggerSimulationOverrideEmitterSignals(const TArray<int32> OverrideEmitterSignalIndexes)
```
Triggers simulation with temporary override of which signal each emitter uses. The override is only active for this single measurement.

**Parameters**:
- `OverrideEmitterSignalIndexes`: Array of signal indices matching the number of emitters

**Returns**: `true` if successful.

---

##### Emitter Signal Management

```cpp
bool SetCurrentEmitterSignalIndexes(TArray<int32> EmitterSignalIndexes)
```
Sets the active emitter signal for all emitters persistently.

**Parameters**:
- `EmitterSignalIndexes`: Array of signal indices (must match emitter count)

**Returns**: `true` if all indices were valid and set successfully.

---

```cpp
TArray<int32> GetCurrentEmitterSignalIndexes()
```
Returns the currently active emitter signal index for each emitter.

---

```cpp
bool SetCurrentEmitterSignalIndexForSpecificEmitter(int32 EmitterIndex, int32 EmitterSignalIndex)
```
Sets the emitter signal for a single specific emitter.

**Parameters**:
- `EmitterIndex`: Index of the emitter to modify
- `EmitterSignalIndex`: Index of the signal to use

---

```cpp
int32 GetCurrentEmitterSignalIndexForSpecificEmitter(int32 EmitterIndex)
```
Gets the active signal index for a specific emitter.

---

```cpp
int32 GetEmitterSignalCount() const
```
Returns the total number of available emitter signals defined in InputSettings.

---

##### Scene Management

```cpp
bool AddActor(AActor* Actor)
```
Adds an actor and all its mesh components to the acoustic simulation. The plugin automatically parses child components and generates mesh data for acoustic analysis.

**Parameters**:
- `Actor`: The actor to add to simulation

**Returns**: `true` if successfully added.

---

```cpp
bool AddStaticMeshComponent(UStaticMeshComponent* MeshComponent, FString ObjectNamePrefix)
```
Manually adds a specific static mesh component to the simulation.

**Parameters**:
- `MeshComponent`: The static mesh component to add
- `ObjectNamePrefix`: Prefix for labeling (typically the owning actor's name)

---

```cpp
bool AddSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, FString ObjectNamePrefix)
```
Manually adds a specific skeletal mesh component to the simulation.

---

```cpp
bool RemoveActor(AActor* Actor)
```
Removes an actor and all its mesh components from simulation.

---

```cpp
bool RemoveStaticMeshComponent(UStaticMeshComponent* MeshComponent)
```
Removes a specific static mesh component.

---

```cpp
bool RemoveSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent)
```
Removes a specific skeletal mesh component.

---

##### Transformation and Positioning

```cpp
bool SetNewEmitterPositions(const TArray<int32>& EmitterIndexes, 
                            const TArray<FVector>& NewEmitterPositions, 
                            const bool RelativeTransform = false, 
                            const bool ReApplyOffset = true)
```
Dynamically updates emitter positions during runtime.

**Parameters**:
- `EmitterIndexes`: Indices of emitters to modify
- `NewEmitterPositions`: New positions in centimeters (left-handed UE coordinate system)
- `RelativeTransform`: If `true`, treats positions as translation offsets from current position
- `ReApplyOffset`: If `true`, reapplies the global emitter offset from InputSettings

---

```cpp
bool SetNewReceiverPositions(const TArray<int32>& ReceiverIndexes, 
                             const TArray<FVector>& NewReceiverPositions, 
                             const bool RelativeTransform = false, 
                             const bool ReApplyOffset = true)
```
Dynamically updates receiver positions. Uses final generated receiver positions (after emitter pattern generation if enabled).

---

```cpp
bool SetNewSensorRelativeTransform(const FVector& NewSensorToOwnerTranslation, 
                                   const FRotator& NewSensorToOwnerRotator)
```
Updates the sensor's transform relative to its owner actor.

---

```cpp
bool SetNewSensorWorldTransform(const FVector& NewSensorTranslation, 
                                const FRotator& NewSensorRotator, 
                                const ETeleportType Teleport = ETeleportType::None)
```
Sets the sensor's absolute world transform.

**Parameters**:
- `Teleport`: Physics teleportation behavior (affects physics simulation)

---

```cpp
bool SetNewSensorOwnerWorldTransform(const FVector& NewOwnerTranslation, 
                                     const FRotator& NewOwnerRotator, 
                                     const ETeleportType Teleport = ETeleportType::None)
```
Sets the sensor owner's absolute world transform (if owner exists).

---

##### Interface Communication

See [here](#api) for more information on what the API is.


```cpp
bool SendInterfaceDataMessage(const int32 Type, 
                              const TArray<int32> Order, 
                              const TArray<FString> Strings, 
                              const TArray<int32> Integers, 
                              const TArray<float> Floats)
```
Sends a custom data message over the API. 

**Parameters**:
- `Type`: Message type identifier
- `Order`: Array specifying data order (0=String, 1=Integer, 2=Float)
- `Strings`: String data array
- `Integers`: Integer data array
- `Floats`: Float data array

**Returns**: `true` if message sent successfully.

**Example**:
```cpp
// Send a message with type 100, containing "Hello" and the value 42.5
TArray<int32> Order = {0, 2};  // String first, then Float
TArray<FString> Strings = {"Hello"};
TArray<int32> Integers = {};
TArray<float> Floats = {42.5f};
Actor->SendInterfaceDataMessage(100, Order, Strings, Integers, Floats);
```

---



```cpp
UPROPERTY(BlueprintAssignable)
FInterfaceDataMessageReceivedEvent InterfaceDataMessageReceivedEvent
```

**Delegate Signature**:
```cpp
void OnInterfaceDataMessageReceived(int32 Type, 
                                    const TArray<int32>& Order, 
                                    const TArray<FString>& Strings, 
                                    const TArray<int32>& Integers, 
                                    const TArray<float>& Floats)
```

Broadcasts when a data message is received via the TCP interface. Bind to this event in Blueprint or C++ to handle incoming messages.

## Input Settings

The `USonoTraceUEInputSettingsData` class is the central configuration hub for the simulation. Create instances as Data Assets in the Content Browser.

### Emitter Configuration

#### Signal Definition

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
TArray<FRuntimeFloatCurve> EmitterSignals
```

Defines the emitter signals using **Float Curves**. Each curve represents a signal waveform that can be assigned to emitters.

**Usage**:
1. In the Details panel, add entries to the `EmitterSignals` array
2. For each entry, you can add a reference to a **Float Curve**. Read [here](#creating-new-emitter-signals) on how to create one.
3. The first signal (index 0) is considered the default
4. Signals can be switched per-emitter at runtime using the emitter signal management functions

---

#### Emitter Positions

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
FVector EmitterPositionsOffset
```
Global offset applied to all emitter coordinates (in centimeters, UE left-handed system).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
bool EnableEmitterPositionsDataTable
```
When `true`, loads emitter positions from a DataTable. When `false`, uses manually defined positions.  Read [here](#creating-new-emitter-and-receiver-array-coordinate-tables) on how to create one.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
UDataTable* EmitterPositionsDataTable
```
Reference to a DataTable with row type `FSonoTraceUECoordinateTable` for loading emitter positions. Read [here](#creating-new-emitter-and-receiver-array-coordinate-tables) on how to create one.

**Creating a Coordinate DataTable**:
1. Create a new DataTable asset in the Content Browser
2. Select `SonoTraceUECoordinateTable` as the row structure
3. Populate with coordinate data or import from CSV

**CSV Format**:
```csv
RowName,X,Y,Z
Emitter_0,0.0,5.0,0.0
Emitter_1,0.0,-5.0,0.0
```

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
TArray<FVector> EmitterPositions
```
Manually defined emitter positions (used when `EnableEmitterPositionsDataTable` is `false`).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
TArray<int32> DefaultEmitterSignalIndexes
```
Specifies which signal each emitter uses. Must match the number of emitters. If empty, all emitters default to signal index 0.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter")
bool EnableEmitterDirectivity
```
Toggle the source directivity calculation.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Emitter", meta=(ClampMin=0, ClampMax=1))
TArray<float> EmitterDirectivity
```
Source directivity for each emitter. For example: 0.0=Omni, 0.5=Cardioid, 1.0=Cosine (Spotlight).

### Receiver Configuration

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
bool EnableStaticReceivers
```
When `true`, receiver positions are fixed and not updated every frame (performance optimization).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
bool EnableUseWorldCoordinatesReceivers
```
Applicable only when `EnableStaticReceivers` is `true`. Interprets receiver positions as world coordinates rather than sensor-relative.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
FVector ReceiverPositionsOffset
```
Global offset applied to all receiver coordinates.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
bool EnableReceiverPositionsDataTable
```
Toggle between DataTable-based and manual receiver position definition. Read [here](#creating-new-emitter-and-receiver-array-coordinate-tables) on how to create one.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
UDataTable* ReceiverPositionsDataTable
```
DataTable for loading receiver positions (same structure as emitter positions). Read [here](#creating-new-emitter-and-receiver-array-coordinate-tables) on how to create one.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
TArray<FVector> ReceiverPositions
```
Manually defined receiver positions.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers")
bool EnableReceiverDirectivity = true
```
Toggle the receiver directivity calculation.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers", meta=(ClampMin=0, ClampMax=1))
TArray<float> ReceiverDirectivity
```
Receiver directivity. For example: 0.0=Omni, 0.5=Cardioid, 1.0=Cosine (Spotlight).

---

#### Emitter Pattern Simulation

The plugin can automatically generate a circular array pattern around each loaded receiver position to create a virtual receiver array to simulate the emitter derectivity better. 

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
bool EnableEmitterPatternSimulation
```
Enables circular array pattern generation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
float EmitterPatternRadius
```
Radius of the circular pattern (in centimeters).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
float EmitterPatternSpacing
```
Spacing between generated receiver points (in centimeters).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
bool EmitterPatternHexagonalLattice
```
When `true`, uses hexagonal lattice instead of square lattice for pattern generation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Receivers|EmitterPattern")
ESonoTraceUEArrayPlaneEnum EmitterPatternPlane
```
Defines the plane for circular array generation:
- `YZ`: Y-Z plane (X = 0) — default
- `XZ`: X-Z plane (Y = 0)
- `XY`: X-Y plane (Z = 0)

### Simulation Settings

#### General Configuration

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableSimulation
```
Master toggle for the entire simulation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableRaytracing
```
Enables hardware ray tracing. Automatically enabled if specular component calculation is active.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableSpecularComponentCalculation
```
Calculates specular reflections using ray tracing.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableDiffractionComponentCalculation
```
Calculates diffraction components.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableDirectPathComponentCalculation
```
Calculates direct line-of-sight transmission between emitters and receivers, for passive sensing. 

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableRunSimulationOnlyOnTrigger
```
When `true`, simulation only runs when explicitly triggered via `TriggerSimulation()`. When `false`, runs continuously at `SimulationRate`.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool PointsInSensorFrame
```
When `true`, output point coordinates are in sensor-relative frame. When `false`, uses world frame.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableSimulationSubOutput
```
Enables detailed sub-outputs for individual components (specular, diffraction, direct path). Disabled by default for performance.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float SimulationRate
```
Target simulation rate in Hz when not running on trigger mode.

---

#### Frequency Configuration

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 NumberOfSimFrequencies
```
Number of frequency bins for simulation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 MinimumSimFrequency
```
Lower bound of frequency range in Hz.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 MaximumSimFrequency
```
Upper bound of frequency range in Hz.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 SampleRate
```
Sample rate in Hz for impulse response generation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float SpeedOfSound
```
Speed of sound in m/s (default: 343 m/s for air at 20°C).

---

#### Component-Specific Settings

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float DirectPathStrength
```
Base transmission strength for direct path calculations.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float SpecularMinimumStrength
```
Minimum strength threshold for saving specular reflection points (filters weak reflections).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float DiffractionMinimumStrength
```
Minimum summed strength threshold for diffraction points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
float DiffractionTriangleSizeThreshold
```
Minimum triangle size (cm²) for diffraction consideration.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 DiffractionSimDivisionFactor
```
Division factor for diffraction sampling rate (higher = fewer samples, faster computation).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableDiffractionLineOfSightRequired
```
When `true`, enforces line-of-sight check between diffraction points and emitters.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableDiffractionForDynamicObjects
```
Enables diffraction calculation for skeletal meshes. See [here](#skeletal-meshes-and-diffraction) for info.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
bool EnableSpecularSimulationOnlyOnLastHits
```
Optimization: only calculates BRDF for the final bounce of each ray.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|General")
int32 MeshDataGenerationAttempts
```
Number of ticks to attempt mesh data generation before timeout (default: 5).

### Object Settings Configuration

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
UDataTable* ObjectSettingsDataTable
```

References a DataTable with row type `FSonoTraceUEObjectSettingsTable` for per-object acoustic properties. See [here](#creating-new-object-settings) on how to make one.

**FSonoTraceUEObjectSettingsTable Structure**:

| Field | Type | Description |
|-------|------|-------------|
| `Asset` | `UObject*` | Reference to StaticMesh or SkeletalMesh |
| `Description` | `FString` | Human-readable description |
| `ObjectSettings` | `FSonoTraceUEObjectSettingsOriginStruct` | Acoustic properties (see below) |
| `DrawDebugFirstOccurrence` | `bool` | Enable debug visualization for first instance |

---

**FSonoTraceUEObjectSettingsOriginStruct** (Acoustic Properties):

*BRDF Settings*:
- `BrdfTransitionPosition` (0-1): Transition point between specular and diffraction regimes
- `BrdfTransitionSlope`: Steepness of transition
- `BrdfExponentSpecularStart/End`: Specular BRDF exponent range
- `BrdfExponentDiffractionStart/End`: Diffraction BRDF exponent range

*Material Settings*:
- `MaterialsTransitionPosition`: Similar to BRDF transition
- `MaterialSTransitionSlope`: Transition steepness
- `MaterialStrengthSpecularStart/End`: Specular reflection strength range
- `MaterialStrengthDiffractionStart/End`: Diffraction reflection strength range

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
FSonoTraceUEObjectSettingsOriginStruct ObjectSettingsDefault
```
Default acoustic properties applied to objects not in the ObjectSettingsDataTable.

---

#### Curvature Calculation

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
float CurvatureScale
```
Global scaling factor for curvature magnitude calculations.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
bool EnableCurvatureTriangleSizeBasedScaler
```
Applies size-dependent scaling to curvature calculations.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
float CurvatureScalerMinimumEffect
```
Minimum scaling factor for very small triangles (0-1).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
float CurvatureScalerMaximumEffect
```
Maximum scaling factor for large triangles (≥1).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
float CurvatureScalerLowerTriangleSizeThreshold
```
Triangle size (cm²) below which minimum scaling applies.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Objects")
float CurvatureScalerUpperTriangleSizeThreshold
```
Triangle size (cm²) above which maximum scaling applies.

---

### Ray Tracing Configuration

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
float SensorLowerAzimuthLimit
```
Lower azimuth angle limit in degrees (-90 to 90, left-handed coordinate system).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
float SensorUpperAzimuthLimit
```
Upper azimuth angle limit in degrees.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
float SensorLowerElevationLimit
```
Lower elevation angle limit in degrees (-90 to 90).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
float SensorUpperElevationLimit
```
Upper elevation angle limit in degrees.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
int32 NumberOfInitialRays
```
Number of rays cast per simulation. Higher values increase simulationr resolution but reduce performance.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
float MaximumRayDistance
```
Maximum ray propagation distance in centimeters.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Configuration|Simulation|Raytracing")
int32 MaximumBounces
```
Maximum number of ray bounces/reflections (1-10).

### Visualization Settings

The plugin provides extensive  visualization capabilities.
There is both standard visualisation which happens for the latest triggered (manually or automatic) measurement, as well as debug which is running constantly.

#### Standard Drawing

This shows the result for the latest triggered (manually or automatic) measurement.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDraw
```
Master toggle for standard visualization.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawSensorPose
```
Draws sensor coordinate system.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawSensorFrustum
```
Draws sensor field-of-view frustum.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawAllEmitters
```
Draws all emitter positions as purple points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawLoadedReceivers
```
Draws configured receiver positions as orange points (before pattern generation).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawAllReceivers
```
Draws all receivers including pattern-generated ones as yellow points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawDirectPathLOS
```
Draws green/red points on receivers based on direct path line-of-sight.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool EnableDrawPoints
```
Draws acoustic reflection/diffraction points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
int32 MaximumDrawNumber
```
Maximum number of points/rays to draw (0-10,000) for performance.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
bool RandomizeDrawSelection
```
Randomizes which subset of points/rays are drawn. If `false`, uses fixed step size.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
ESonoTraceUESimulationDrawColorModeEnum DrawPointsColorMode
```

Color mode for point visualization:
- `Static`: Uses `DrawDefaultPointsColor`
- `TotalDistance`: Colors by total acoustic path length
- `SensorDistance`: Colors by distance to sensor
- `Strength`: Colors by reflection strength
- `Curvature`: Colors by surface curvature at reflection point
- 'EmitterDirectivity': Colors by the calculated emitter directivity at the reflection point

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw")
ESonoTraceUESimulationDrawSizeModeEnum DrawPointsSizeMode
```

Point size mode:
- `Static`: Uses `DrawDefaultPointsSize`
- `Strength`: Scales point size by reflection strength

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
int32 DrawDefaultPointsSize
```
Default point size in pixels.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
FColor DrawDefaultPointsColor
```
Default point color (RGB).

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
ESonoTraceUEColorMapEnum DrawPointsColorMap
```

Color map for data-driven coloring:
- `Parula` (default)
- `Hot`
- `Jet`
- `Viridis`
- And others (see `ColorMaps.h`)

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
bool DrawPointsTotalDistanceMaximumAutoScale
```
Enable auto-scaling of the maximum value to normalize the data when in total distance mode for size or color.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
bool DrawPointsCurvatureMaximumAutoScale
```
Enable auto-scaling of the maximum value to normalize the data when in curvature mode for size or color.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
bool DrawPointsStrengthMaximumAutoScale
```
Enable auto-scaling of the maximum value to normalize the data when in strength mode for size or color.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
float DrawPointsTotalDistanceMaximumValue
```
The maximum value in centimeters to normalize the data when in total distance mode for size or color when not auto-scaling.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
float DrawPointsCurvatureMaximumValue
```
The maximum value to normalize the data when in curvature mode for size or color when not auto-scaling.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
float DrawPointsStrengthMaximumValue
```
The maximum value to normalize the data when in strength mode for size or color when not auto-scaling.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Draw|Details")
int DrawPointsDirectivityEmitterIndex
```
The emitter index to use for plotting the emitter directivity when in directivity mode for size or color.

---

#### Debug Drawing

The debug drawing system provides more detailed visualization for development and analysis and is running all the time.

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDebugLogExecutionTimes
```
Logs execution times for performance profiling.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebug
```
Master toggle for debug visualization.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugSensorPose
```
Draws sensor coordinate system.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugSensorFrustum
```
Draws the frustum that describes the measurement region for simulation.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugAllEmitters
```
Draws all the emitters as purple points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugLoadedReceivers
```
Draws configured receiver positions as orange points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugAllReceivers
```
Draws all receivers including pattern-generated ones as yellow points.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugSensorToReceiverLines
```
Draws turquoise lines between the sensor and the receivers, useful when direct path is enabled.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugDirectPathLOS
```
Draws green or red point on each receiver depending on if there is LOS for the direct path component calculation with a direction vector to the sensor. In case of no LOS, also draws an orange point and direction vector on the hit location that blocked that LOS.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugPoints
```
Draws the points of the raytracing.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
int32 MaximumDrawDebugRaysNumber
```
Maximum number of rays and points to draw (0-10,000) for performance.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool RandomizeDrawDebugRaysSelection
```
Randomizes which subset of rays are drawn. If `false`, uses fixed step size.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugRaysHitLines
```
Draws ray paths as green lines.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugRaysHitPoints
```
Draws ray hit points as green dots.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugRaysLastHitReflection
```
Draws blue vectors showing reflection direction from final ray hit.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugRaysInitialMisses
```
Draws rays that missed geometry in red.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool EnableDrawDebugMeshData
```
Visualizes pre-calculated mesh analysis data (curvature, BRDF, etc.). Globally toggles the drawing of pre-calculated mesh data if enabled in the individual object settings.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
ESonoTraceUEMeshModeEnum DrawDebugMeshColorMode
```

Mesh data visualization mode:
- `Curvature`: Shows surface curvature magnitude
- `SurfaceBRDF`: Shows BRDF opening angle
- `SurfaceMaterial`: Shows reflection strength
- `Normal`: Shows surface normals as RGB
- `Size`: Shows triangle size

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
int32 DrawDebugMeshMaximumPoints
```
The maximum points to draw for the mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
bool RandomizeDrawDebugMeshData
```
Randomize the drawing selection for the mesh data. If `false`, uses fixed step size.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
int32 DrawDebugMeshSize
```
Size of the points for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
ESonoTraceUEColorMapEnum DrawDebugMeshColorMap
```
Color map used for the color of the points for mesh data. Does not apply when showing surface normal.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
FVector2D DrawDebugMeshCurvatureLimits
```
The minimum and maximum values to normalize the data when in curvature mode for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
FVector2D DrawDebugMeshOpeningAngleLimits
```
The minimum and maximum values to normalize the data when in opening angle mode for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
FVector2D DrawDebugMeshReflectionStrengthLimits
```
The minimum and maximum values to normalize the data when in reflection strength mode for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
FVector2D DrawDebugMeshTriangleSizeLimits
```
The minimum and maximum values to normalize the data when in surface area mode for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
int32 DrawDebugMeshOpeningAngleFrequencyIndex
```
The frequency bin index to use for the data when in opening angle mode for mesh data.

---

```cpp
UPROPERTY(EditAnywhere, Category = "SonoTraceUE|Debug")
int32 DrawDebugMeshReflectionStrengthFrequencyIndex
```
The frequency bin index to use for the data when in reflection strength mode for mesh data.

---

## Output Data

### FSonoTraceUEOutputStruct

The primary output structure containing simulation results.

#### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `ReflectedPoints` | `TArray<FSonoTraceUEPointStruct>` | All reflection/diffraction points combined |
| `MaximumStrength` | `float` | Maximum reflection strength in output |
| `MaximumCurvature` | `float` | Maximum curvature value in output |
| `MaximumTotalDistance` | `float` | Maximum acoustic path length |
| `EmitterSignalIndexes` | `TArray<int32>` | Active signal index per emitter for this measurement |
| `SensorLocation` | `FVector` | Sensor world location |
| `SensorRotation` | `FRotator` | Sensor world rotation |
| `SensorToOwnerTranslation` | `FVector` | Relative translation to owner |
| `SensorToOwnerRotation` | `FRotator` | Relative rotation to owner |
| `OwnerLocation` | `FVector` | Owner world location |
| `OwnerRotation` | `FRotator` | Owner world rotation |
| `EmitterPoses` | `TArray<FTransform>` | Emitter transforms |
| `ReceiverPoses` | `TArray<FTransform>` | Receiver transforms |
| `DirectPathLOS` | `TArray<bool>` | Line-of-sight status per receiver (when using direct mode) |
| `Timestamp` | `double` | Simulation timestamp |
| `Index` | `int32` | Sequential measurement index |

#### Sub-Outputs

If `EnableSimulationSubOutput` is enabled:
- `SpecularSubOutput`: Specular component only
- `DiffractionSubOutput`: Diffraction component only
- `DirectPathSubOutput`: Direct path component only

### FSonoTraceUEPointStruct

Represents a single acoustic reflection/diffraction/transmission point.

#### Fields

| Field                        | Type            | Description                                                                |
|------------------------------|-----------------|----------------------------------------------------------------------------|
| `Location`                   | `FVector`       | 3D position of point                                                       |
| `ReflectionDirection`        | `FVector`       | Direction of acoustic reflection                                           |
| `Label`                      | `FName`         | Object label (from component name)                                         |
| `Index`                      | `int`           | Sequential point index                                                     |
| `SummedStrength`             | `float`         | Total strength across all frequencies                                      |
| `Strengths`                  | `TArray<TArray<TArray<float>>>` | Strength values per emitter, receiver, and frequency (C++ only)            |
| `TotalDistance`              | `float`         | Total acoustic path length (cm)                                            |
| `TotalDistancesFromEmitters` | `TArray<float>` | Path length per emitter                                                    |
| `TotalDistancesToReceivers`  | `TArray<TArray<float>>` | Path length from this point to each receiver, per emitter (C++ only)       |
| `DistanceToSensor`           | `float`         | Direct distance to sensor (cm)                                             |
| `ObjectTypeIndex`            | `int`           | Index into ObjectSettings array                                            |
| `IsHit`                      | `bool`          | `true` if ray hit geometry                                                 |
| `IsLastHit`                  | `bool`          | `true` if final bounce in ray path                                         |
| `CurvatureMagnitude`         | `float`         | Surface curvature at hit point                                             |
| `SurfaceBRDF`                | `float*`        | BRDF opening angle data pointer (C++ only)                                 |
| `SurfaceMaterial`            | `float*`        | Reflection strength data pointer (C++ only)                                |
| `IsSpecular`                 | `bool`          | `true` if from specular component                                          |
| `IsDiffraction`              | `bool`          | `true` if from diffraction component                                       |
| `IsDirectPath`               | `bool`          | `true` if from direct path component                                       |
| `RayIndex`                   | `int`           | The original index of the raytracing resulting in this point               |
| `BounceIndex`                | `int`           | The bounce index of the multi-path reflections of the rays                 |
| `EmitterDirectivities`       | `TArray<float>` | The calculated source directivity for each emitter to the first reflection |

Note: Advanced fields like `Strengths` (per-emitter, per-receiver, per-frequency), `TotalDistancesToReceivers` ((per-emitter, per-receiver) and `SurfaceBRDF`/`SurfaceMaterial` pointers are available in C++ but not exposed to Blueprint.

### FSonoTraceUEGeneratedInputStruct

Contains processed/generated settings derived from InputSettings.

#### Fields

| Field                         | Type | Description                                         |
|-------------------------------|------|-----------------------------------------------------|
| `AzimuthAngles`               | `TArray<float>` | Generated ray azimuth angles                        |
| `ElevationAngles`             | `TArray<float>` | Generated ray elevation angles                      |
| `LoadedEmitterPositions`      | `TArray<FVector>` | Original loaded emitter positions                   |
| `FinalEmitterPositions`       | `TArray<FVector>` | Final emitter positions (after offsets)             |
| `FinalEmitterDirectivities`   | `TArray<FVector>` | Final emitter directivities                         |
| `LoadedReceiverPositions`     | `TArray<FVector>` | Original loaded receiver positions                  |
| `FinalReceiverPositions`      | `TArray<FVector>` | Final receiver positions (after pattern generation) |
| `FinalReceiverDirectivities`  | `TArray<FVector>` | Final receiver directivities  (after pattern generation)                       |
| `ObjectSettings`              | `TArray<FSonoTraceUEObjectSettingsStruct>` | Processed object acoustic settings                  |
| `Frequencies`                 | `TArray<float>` | Simulation frequency bins                           |
| `DefaultEmitterSignalIndexes` | `TArray<int32>` | Active signal index per emitter                     |


## API

A socket-based API is available to access SonoTraceUE with third-party tools. 
Currently a Matlab Client API is available [here](https://github.com/Cosys-Lab/SonoTraceUE-Matlab-Toolbox). You will find more information there on how to control the simulation from the API client.
The _Default_ example level in the sample project is ideal for testing with example code in the Client API tools.

The `USonoTraceUEInterfaceSettingsData` class configures the TCP/IP network interface for external control and data acquisition.

### Properties

```cpp
UPROPERTY(EditAnywhere, Category = "Connection")
bool EnableInterface
```
Enables the TCP interface. Must be `true` for remote communication.

---

```cpp
UPROPERTY(EditAnywhere, Category = "Connection")
FString InterfaceIP
```
IP address for the TCP server (default: `"localhost"`). Use `"0.0.0.0"` to listen on all interfaces.

---

```cpp
UPROPERTY(EditAnywhere, Category = "Connection")
int32 InterfacePort
```
TCP port number (1024-65535, default: 9099).

---

```cpp
UPROPERTY(EditAnywhere, Category = "Connection")
bool EnableSubOutput
```
Enables detailed sub-component output over the interface. Increases data throughput.

---

### Interface Overview

The TCP interface supports the following operations:

1. **Settings Retrieval**: Client can request and receive full input settings configuration
2. **Measurement Triggering**: Remotely trigger simulations and receive output data
3. **Transformation Control**: Set sensor/emitter/receiver positions remotely
4. **Custom Data Messages**: Bidirectional communication of custom data structures using the data message system

### Data Message System

The data message system allows flexible communication of mixed-type data:

**Structure**:
- **Type** (`int32`): Self-chosen message identifier
- **Order** (`TArray<int32>`): Specifies data sequence (0=String, 1=Integer, 2=Float)
- **Strings** (`TArray<FString>`): String payloads
- **Integers** (`TArray<int32>`): Integer payloads
- **Floats** (`TArray<float>`): Float payloads

**Sending from UE** (Blueprint/C++):
```cpp
TArray<int32> Order = {0, 1, 2};  // String, Integer, Float
TArray<FString> Strings = {"Status"};
TArray<int32> Integers = {42};
TArray<float> Floats = {3.14f};
SonoTraceActor->SendInterfaceDataMessage(100, Order, Strings, Integers, Floats);
```

**Receiving in UE** (Blueprint):
Bind to the `InterfaceDataMessageReceivedEvent` delegate on the SonoTraceUEActor.

## Licensing

This project is released under the [MIT License](/LICENSE).

The SonoTraceUE Unreal Engine Plugin uses code from the [ObjectDeliverer](https://github.com/AyumaxSoft/ObjectDeliverer) plugin v1.8.0 which also uses the MIT License.
