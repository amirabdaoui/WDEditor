#pragma once

#include "PCGSettings.h"
#include "PCGPointsToDynamicMeshGrid.generated.h"

/**
 * Builds a Dynamic Mesh grid from input points, writing into an input DynamicMeshData.
 * Thread-safe: does NOT spawn components. Use Epic's Spawn Dynamic Mesh node after this.
 *
 * Expected graph flow:
 *   Points -> [this node] -> DynamicMeshData -> Spawn Dynamic Mesh
 *            ^ input mesh should come from Create Empty Dynamic Mesh
 *
 * Topology behaviour:
 * - UniformGrid:
 *     Always emits a regular grid (masked vertices are removed).
 *
 * - LandscapeParity:
 *     Emits a uniform grid where fully solid,
 *     emits nothing where fully empty,
 *     and uses marching-squares topology ONLY where the mask crosses the threshold.
 *
 * Mask semantics match Landscape visibility:
 *     Value >= Threshold  -> solid
 *     Value <  Threshold  -> hole
 */
UENUM(BlueprintType)
enum class EPCGGridTopologyMode : uint8
{
	UniformGrid      UMETA(DisplayName = "Uniform Grid"),
	LandscapeParity  UMETA(DisplayName = "Landscape Parity (Hybrid Marching Squares)")
};

UCLASS(
	BlueprintType,
	ClassGroup = (PCG),
	meta = (DisplayName = "Points to Dynamic Mesh Grid (Data)")
)
class WDEDITOR_API UPCGPointsToDynamicMeshGridSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGPointsToDynamicMeshGridSettings();

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override
	{
		return TEXT("PointsToDynamicMeshGridData");
	}

	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCG", "PointsToDynamicMeshGridData_Title", "Points → Dynamic Mesh Grid (Data)");
	}

	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT(
			"PCG",
			"PointsToDynamicMeshGridData_Tooltip",
			"Build a DynamicMesh grid from point input. Supports Landscape-parity holes. "
			"Feed result into Spawn Dynamic Mesh."
		);
	}
#endif // WITH_EDITOR

	// ---- Pins ----
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	static const FName PointsPinLabel;
	static const FName DynamicMeshPinLabel;

	// ============================================================
	// Topology
	// ============================================================

	/** Controls whether boundary quads use Landscape-style marching squares */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Topology",
		meta = (PCG_Overridable)
	)
	EPCGGridTopologyMode TopologyMode = EPCGGridTopologyMode::UniformGrid;

	// ============================================================
	// Masking / Visibility
	// ============================================================

	/** Attribute used to determine visibility (e.g. Landscape Visibility, mask texture, etc.) */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Mask",
		meta = (PCG_Overridable, DisplayName = "Visibility Mask Attribute")
	)
	FName KeepMaskAttribute = NAME_None;

	/**
	 * Visibility threshold.
	 * Matches Landscape semantics:
	 *   Value >= Threshold -> solid
	 *   Value <  Threshold -> hole
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Mask",
		meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0")
	)
	float MaskThreshold = 0.5f;

	/** Invert the visibility test */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Mask",
		meta = (PCG_Overridable, DisplayName = "Invert Mask")
	)
	bool bInvertMask = false;

	// ============================================================
	// Mesh cleanup
	// ============================================================

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Mesh",
		meta = (PCG_Overridable, DisplayName = "Remove Isolated Vertices")
	)
	bool bRemoveIsolatedVertices = true;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Mesh",
		meta = (PCG_Overridable, DisplayName = "Compact Mesh At End")
	)
	bool bCompactAtEnd = true;
};
