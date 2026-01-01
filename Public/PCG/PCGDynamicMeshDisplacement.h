#pragma once

#include "CoreMinimal.h"

/* PCG GeometryScript Interop */
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "PCGCommon.h"

#include "PCGDynamicMeshDisplacement.generated.h"

class UTexture2D;

// ClassGroup is set to PCG so the node appears in the PCG palette rather
// than the generic Procedural category.  DisplayName and Category control
// how the node is listed in the editor.
// Use MinimalAPI to ensure the class is properly exported from the module and
// appears in the PCG node registry.  ClassGroup is PCG so it appears under
// PCG nodes in the editor.  DisplayName controls the user-facing label.
UCLASS(
    BlueprintType,
    ClassGroup=(PCG),
    meta=(
        DisplayName="Dynamic Mesh Displacement",
        Category="PCG|Dynamic Mesh"
    )
)
class WDEDITOR_API UPCGDynamicMeshDisplacementSettings
	: public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynamicMeshDisplacementSettings();

	/** Element factory */
	virtual FPCGElementPtr CreateElement() const override;

	/** Displacement texture (alpha channel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement", meta=(PCG_Overridable))
	TObjectPtr<UTexture2D> DisplacementTexture = nullptr;

    /**
     * World-space projection size.  Negative values are allowed to flip the
     * projection orientation.  No clamp is applied here; a value of zero
     * will be clamped internally to a small epsilon to avoid division by
     * zero when computing the inverse scale.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
        meta=(PCG_Overridable))
    float ProjectionWorldSize = 1000.0f;

	/** Displacement strength */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
		meta=(PCG_Overridable))
	float DisplacementIntensity = 100.0f;

	/** Enable slope-based masking */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement")
bool bUseSlopeMask = false;


	/** Min dot(N, Up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
		meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bUseSlopeMask"))
	float MinSlopeDot = 0.0f;

	/** Max dot(N, Up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
		meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bUseSlopeMask"))
	float MaxSlopeDot = 1.0f;

#if WITH_EDITOR
    /** Override default node name shown in the graph. */
    virtual FName GetDefaultNodeName() const override
    {
        return TEXT("DynamicMeshDisplacement");
    }
    /** Override default node title shown in the details panel. */
    virtual FText GetDefaultNodeTitle() const override
    {
        return NSLOCTEXT("WDEditor", "DynamicMeshDisplacement_Title", "Dynamic Mesh Displacement");
    }
#endif
};
