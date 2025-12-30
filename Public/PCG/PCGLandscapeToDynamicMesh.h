 #pragma once
 
 #include "PCGSettings.h"
 #include "PCGLandscapeToDynamicMesh.generated.h"
 
 UENUM(BlueprintType)
 enum class EPCGLandscapeBoundsIntersectMode : uint8
 {
         Ignore    UMETA(DisplayName="Ignore Bounds"),
         Intersect UMETA(DisplayName="Intersect"),
         Strict    UMETA(DisplayName="Strict (Bounds Only)")
 };
 
 UCLASS(
         BlueprintType,
         ClassGroup = (PCG),
         meta = (DisplayName = "Landscape To Dynamic Mesh (Data)")
 )
 class WDEDITOR_API UPCGLandscapeToDynamicMeshSettings : public UPCGSettings
 {
         GENERATED_BODY()
 
 public:
         UPCGLandscapeToDynamicMeshSettings();
 
         virtual EPCGSettingsType GetType() const override
         {
                 return EPCGSettingsType::Spatial;
         }
 
 #if WITH_EDITOR
         virtual FName GetDefaultNodeName() const override
         {
                 return TEXT("LandscapeToDynamicMeshData");
         }
 
         virtual FText GetDefaultNodeTitle() const override
         {
                 return NSLOCTEXT(
                         "WDEditor",
                         "LandscapeToDynamicMeshData_Title",
                         "Landscape To Dynamic Mesh (Data)");
         }
 #endif
 
         // ============================================================
         // Pins
         // ============================================================
 
         static const FName LandscapePinLabel;
         static const FName BoundsPinLabel;
         static const FName DynamicMeshPinLabel;
 
         virtual TArray<FPCGPinProperties> InputPinProperties() const override;
         virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
 
 protected:
         virtual FPCGElementPtr CreateElement() const override;
 
 public:
         // ============================================================
         // Sampling
         // ============================================================
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling",
                 meta=(PCG_Overridable, ClampMin="1.0"))
         double CellSize = 100.0;
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling",
                 meta=(PCG_Overridable, ClampMin="0"))
         int32 OverscanCells = 1;
 
         // ============================================================
         // Bounds
         // ============================================================
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds",
                 meta=(PCG_Overridable))
         EPCGLandscapeBoundsIntersectMode BoundsMode =
                 EPCGLandscapeBoundsIntersectMode::Intersect;
 
         // ============================================================
         // Mask / Topology
         // ============================================================
 
         /** Optional landscape layer to sample as the mask.  If None, the density (visibility) is used. */
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask",
                 meta=(PCG_Overridable,
                           ToolTip="NAME_None uses Landscape Visibility (Density)"))
         FName MaskLayerName = NAME_None;
 
         /** Threshold at which samples are considered solid.  Values >= threshold are kept. */
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask",
                 meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0"))
         float MaskThreshold = 0.5f;
 
         /** Whether to use marching squares for mixed cells (holes and polygons). */
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask",
                 meta=(PCG_Overridable))
         bool bUseMarchingSquares = true;
 
         /** Invert the sampled mask (1 – weight) before thresholding. */
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask",
                 meta=(PCG_Overridable))
         bool bInvertMask = false;
 
         // ============================================================
         // Subdivision
         // ============================================================
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision",
                 meta=(PCG_Overridable))
         bool bEnableSubdivision = false;
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision",
                 meta=(PCG_Overridable, ClampMin="0"))
         int32 SubdivisionLevels = 0;
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision",
                 meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0"))
         float PNStrength = 0.25f;
 
         // ============================================================
         // Cleanup
         // ============================================================
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh",
                 meta=(PCG_Overridable))
         bool bRemoveIsolatedVertices = true;
 
         UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh",
                 meta=(PCG_Overridable))
         bool bCompactAtEnd = true;
 };