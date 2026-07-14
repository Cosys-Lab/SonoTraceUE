// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details. 

using System.IO;
using UnrealBuildTool;

public class SonoTraceUE : ModuleRules
{
	public SonoTraceUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module reaches into Renderer-internal ray tracing code (RayTracingScene, RayTracingSBT,
		// ScenePrivate.h, NaniteRayTracing.h). UE 5.8 introduced an "Internal" header visibility tier
		// (Engine/Source/Runtime/<Module>/Internal/) between Public and Private; access to another
		// module's Internal headers is only granted to modules opted into bTreatAsEngineModule.
		bTreatAsEngineModule = true;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// Renderer/Private headers we reach into (ScenePrivate.h, NaniteRayTracing.h) pull in
				// further Renderer-private headers via bare quoted includes resolved relative to this
				// root (e.g. "InstanceCulling/InstanceCullingLoadBalancer.h"), which only resolve if
				// Renderer/Private itself is on the include search path.
				Path.Combine(EngineDirectory, "Source", "Runtime", "Renderer", "Private"),
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"RenderCore",
				"RHI",
				"Renderer",
				"SignalProcessing",
				"ObjectDeliverer",
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				// ... add other public dependencies that you statically link with here ...
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"RenderCore",
				"RHI",
				"Renderer",
				"Projects",
				"SignalProcessing",
				"ObjectDeliverer",
				"DynamicMesh",
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				// ... add private dependencies that you statically link with here ...	
			}
		);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}