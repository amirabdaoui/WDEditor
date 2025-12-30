// UpdatedLandscapeSettings.h
// This file provides the complete updated definitions for the sampling settings
// and node settings used to convert landscape data to a dynamic mesh.  It
// introduces support for sampling a user-selected landscape layer as a mask,
// optional mask inversion, and control over the marching squares hybrid topology.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "PCGSettings.h"

/**
 * FUpdatedLandscapeSamplingSettings encapsulates all parameters
 * required to sample a landscape into a regular grid prior to mesh
 * generation.  The CellSize controls the size of each sample; masks
 * can be derived from either a named landscape layer or the default
 * visibility/density.  Additional flags govern marching squares and
 * mask inversion.
 */
USTRUCT(BlueprintType)
struct FUpdatedLandscapeSamplingSettings
{
    GENERATED_BODY()

    /** World units between grid samples along X and Y.  Lower values
     *  produce denser meshes at the cost of performance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling")
    double CellSize = 100.0;

    /** Whether to sample landscape vertex normals.  When disabled the
     *  generated mesh will use flat shading. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling")
    bool bSampleNormals = true;

    /** Optional landscape layer name to drive the mask.  If NAME_None,
     *  the sampler falls back to the point’s density/visibility. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    FName MaskLayerName = NAME_None;

    /** Threshold used to classify samples as solid or empty.  Samples
     *  with Mask >= Threshold are considered solid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaskThreshold = 0.5f;

    /** When true, mixed cells will be triangulated using a marching squares
     *  algorithm to better approximate curved boundaries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    bool bUseMarchingSquares = true;

    /** Invert the sampled mask value (1 - Mask) before applying the threshold.
     *  Useful for carving holes where the selected layer is absent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    bool bInvertMask = false;
};

/**
 * UUpdatedLandscapeToDynamicMeshSettings is the configurable settings
 * asset for a PCG node that converts landscape data to a dynamic mesh.
 * It exposes high-level options for sampling resolution, mask layer,
 * marching squares toggles, inversion, and so forth.
 */
UCLASS(BlueprintType)
class UUpdatedLandscapeToDynamicMeshSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    /** World units between grid samples used when sampling the landscape. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling")
    double CellSize = 100.0;

    /** Number of cells to extend beyond the requested bounds when sampling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling", meta=(ClampMin="0"))
    int32 Overscan = 1;

    /** Optional landscape layer used to derive the sampling mask.  If unset
     *  (NAME_None) the sampler falls back to the landscape’s visibility. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    FName MaskLayerName = NAME_None;

    /** Threshold applied to the sampled mask to determine whether a cell
     *  is solid.  Values below the threshold are treated as empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaskThreshold = 0.5f;

    /** Enable marching squares hybrid topology when encountering mixed cells. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    bool bUseMarchingSquares = true;

    /** Invert the sampled mask value before thresholding.  This is useful
     *  when you wish to carve holes instead of generating solid areas. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mask")
    bool bInvertMask = false;

    /** Additional settings controlling mesh subdivision, cleanup, and normals
     *  may be added here as needed. */
};