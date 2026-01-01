// Copyright BULKHEAD Limited. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/* PCG GeometryScript Interop */
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "PCGCommon.h"

// Include PCGPin prior to including the generated header.  The .generated.h must
// always be the last include in a header file according to Unreal's build rules.
#if WITH_EDITOR
#include "PCGPin.h"
#endif

#include "PCGDynamicMeshDisplacement.generated.h"

/**
 * Settings for the Dynamic Mesh Displacement node.  This node displaces a
 * dynamic mesh using a height map sampled from a Base Texture Data input.
 * Unlike earlier versions, there is no DisplacementTexture property; the
 * height map must be provided via the optional Texture input pin.  If no
 * texture data is connected, the mesh will pass through unchanged.
 */
UCLASS(
    BlueprintType,
    ClassGroup=(PCG),
    meta=(
        DisplayName="Dynamic Mesh Displacement",
        Category="PCG|Dynamic Mesh"
    )
)
class WDEDITOR_API UPCGDynamicMeshDisplacementSettings : public UPCGDynamicMeshBaseSettings
{
    GENERATED_BODY()

public:
    UPCGDynamicMeshDisplacementSettings();

    /** Factory that creates the element corresponding to this settings class. */
    virtual FPCGElementPtr CreateElement() const override;

    /**
     * Label of the optional input pin that accepts Base Texture Data.  When
     * connected, the node samples the height map from this data; otherwise
     * the mesh is output unchanged.  This value is used both in the
     * InputPinProperties() override and in ExecuteInternal() to locate
     * texture data in the input context.  The pin label matches Epic's
     * PCG Sample Texture node ("TextureData").
     */
    static const FName TextureInputLabel;

    /**
     * World‑space projection size.  The size of the box in world units used
     * to scale the triplanar UVs.  Negative values flip the orientation.  A
     * value of zero will be clamped internally to a small epsilon to avoid
     * division by zero.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
        meta=(ClampMin="-10000.0", ClampMax="10000.0", Delta="0.1"))
    float ProjectionSize = 100.0f;

    /**
     * Multiplier applied to the height map.  Values greater than 1 amplify
     * displacement; values between 0 and 1 attenuate it.  Negative values
     * invert the direction of displacement.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
        meta=(ClampMin="-10000.0", ClampMax="10000.0", Delta="0.1"))
    float DisplacementIntensity = 1.0f;

    /**
     * Mid‑point of the displacement map before remapping to [-1,1].
     * The default value of 0.5 yields a symmetric mapping where
     * input 0.5 maps to zero displacement, 0.0 maps to -1 and 1.0
     * maps to +1.  Adjust this value when the height map’s mid
     * value differs from 0.5.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Displacement",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float DisplacementCenter = 0.5f;

    /**
     * Whether to use slope masking.  When enabled, displacement is attenuated
     * based on the vertex normal's alignment with the world up vector.  Faces
     * pointing upward receive full displacement; vertical faces receive less.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slope Mask")
    bool bEnableSlopeMask = false;

    /**
     * Minimum dot(N, Up) allowed when slope masking is enabled.  Vertices
     * with a dot product below this value receive zero displacement.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slope Mask",
        meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bEnableSlopeMask"))
    float MinSlopeDot = 0.0f;

    /**
     * Maximum dot(N, Up) allowed when slope masking is enabled.  Vertices
     * with a dot product equal to or above this value receive full
     * displacement.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slope Mask",
        meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bEnableSlopeMask"))
    float MaxSlopeDot = 1.0f;

#if WITH_EDITOR
    /**
     * Override default node name shown in the PCG graph.  Returning a
     * non‑empty FName ensures the node is registered correctly and visible
     * in the palette.
     */
    virtual FName GetDefaultNodeName() const override { return TEXT("DynamicMeshDisplacement"); }

    /**
     * Override default node title shown in the details panel.  Provides a
     * localized user‑facing label for the node.
     */
    virtual FText GetDefaultNodeTitle() const override
    {
        return NSLOCTEXT("WDEditor", "DynamicMeshDisplacement_Title", "Dynamic Mesh Displacement");
    }

    /**
     * Define input pins for this node.  In addition to the default dynamic
     * mesh input pin provided by the base class, add an optional pin
     * labelled TextureInputLabel that accepts Base Texture Data.  Pin
     * properties like PinType and bAllowMultipleConnections are left to
     * their defaults since those members are private in this build.
     */
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
#endif
};