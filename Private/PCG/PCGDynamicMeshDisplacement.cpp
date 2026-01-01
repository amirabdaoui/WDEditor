// Copyright BULKHEAD Limited. All Rights Reserved.

// Implements the Dynamic Mesh Displacement PCG node.  This node deforms a
// dynamic mesh by sampling a height map from an external texture provided
// through PCG's Base Texture Data system.  The height map is sampled using
// triplanar projection in world space: three planar projections along the
// X‑, Y‑ and Z‑axes are blended based on the vertex normal.  The resulting
// height value is remapped from the [0,1] range to [−1,1], optionally
// attenuated by a slope mask, multiplied by the user‑specified intensity,
// and applied along the vertex normal.

// Include the public settings header.  This file resides in the Public/PCG
// directory of the WDEditor module, so we prefix with "PCG/".  The settings
// declare the TextureInputLabel and the properties controlling projection,
// intensity and slope masking.
#include "PCG/PCGDynamicMeshDisplacement.h"

// PCG framework includes
#include "PCGContext.h"
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

// PCG texture includes.  UPCGBaseTextureData exposes SamplePointLocal() for
// CPU‑accessible texture sampling.  We use this interface instead of
// directly sampling a UTexture2D.
#include "Data/PCGTextureData.h"

// Geometry includes
#include "DynamicMesh/DynamicMesh3.h"

// Forward declare and include UDynamicMesh.  This header defines the
// UDynamicMesh class used to access the underlying FDynamicMesh3 via
// GetMeshRef().  Without including this, the compiler would treat
// UDynamicMesh as an undefined type when dereferencing the pointer returned
// from UPCGDynamicMeshData::GetMutableDynamicMesh().
#include "UDynamicMesh.h"

// Engine includes
#include "Math/Vector.h"
#include "Math/UnrealMathUtility.h"

using UE::Geometry::FDynamicMesh3;

namespace
{
    /**
     * Implementation of the displacement element.  Derives from
     * IPCGDynamicMeshBaseElement so that we can work with dynamic mesh
     * inputs and use the CopyOrSteal() helper.  Only ExecuteInternal() is
     * implemented; other virtual methods use defaults from the base class.
     */
    class FPCGDynamicMeshDisplacementElement final : public IPCGDynamicMeshBaseElement
    {
    protected:
        virtual bool ExecuteInternal(FPCGContext* Context) const override;

        /**
         * Override execution loop mode so that this element iterates only on
         * the primary input pin (the dynamic mesh).  Secondary pins (such
         * as the TextureData pin) are treated as static resources and do
         * not require filtering by data type.  Without this override the
         * PCG graph will insert a Filter by Data Type node when connecting
         * a GetTextureData node, because the default execution loop mode
         * attempts to build a Cartesian product of all inputs.
         */
        virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* /*InSettings*/) const override
        {
            return EPCGElementExecutionLoopMode::SinglePrimaryPin;
        }
    };
}

// Define the static input pin label.  Must match the value declared in the
// settings header.  The pin name is "TextureData" to align with Epic's
// PCG Sample Texture node.  Users connect a GetTextureData or SampleTexture
// node to this pin to provide the height map.
const FName UPCGDynamicMeshDisplacementSettings::TextureInputLabel(TEXT("TextureData"));

UPCGDynamicMeshDisplacementSettings::UPCGDynamicMeshDisplacementSettings() = default;

FPCGElementPtr UPCGDynamicMeshDisplacementSettings::CreateElement() const
{
    return MakeShared<FPCGDynamicMeshDisplacementElement>();
}

#if WITH_EDITOR
TArray<FPCGPinProperties> UPCGDynamicMeshDisplacementSettings::InputPinProperties() const
{
    // Begin with the default pin properties provided by the base class.  This
    // includes the dynamic mesh input pin and any additional pins defined by
    // ancestor classes.
    TArray<FPCGPinProperties> Pins = Super::InputPinProperties();

    // Add an optional pin that accepts Base Texture Data.  Setting only the
    // label and AllowedTypes ensures the PCG system treats this as an input
    // pin for texture data.  Other pin properties (PinType,
    // bAllowMultipleConnections, etc.) are private in the Wardogs PCG build,
    // so we leave them at their default values.
    FPCGPinProperties& TexturePin = Pins.AddDefaulted_GetRef();
    TexturePin.Label = TextureInputLabel;
    TexturePin.AllowedTypes = EPCGDataType::Texture;
    return Pins;
}
#endif // WITH_EDITOR

bool FPCGDynamicMeshDisplacementElement::ExecuteInternal(FPCGContext* Context) const
{
    check(Context);

    // Retrieve the node settings.  Use GetInputSettings() rather than
    // GetOutputSettings() because the latter may not be available in all
    // versions of the PCG framework.
    const UPCGDynamicMeshDisplacementSettings* Settings = Context->GetInputSettings<UPCGDynamicMeshDisplacementSettings>();
    if (!Settings)
    {
        return true;
    }

    // Fetch the optional texture data input.  Only the first connected input
    // is considered.  If none is connected, TexData remains null and no
    // displacement is applied.
    const UPCGBaseTextureData* TexData = nullptr;
    {
        const TArray<FPCGTaggedData>& TexInputs = Context->InputData.GetInputsByPin(UPCGDynamicMeshDisplacementSettings::TextureInputLabel);
        if (TexInputs.Num() > 0)
        {
            TexData = Cast<const UPCGBaseTextureData>(TexInputs[0].Data);
        }
    }

    // Precompute parameters used by all meshes.  Scale controls the size of
    // the triplanar projection; Intensity multiplies the height; slope mask
    // parameters control attenuation based on vertex normal Z component.
    float Scale = Settings->ProjectionSize;
    if (FMath::IsNearlyZero(Scale))
    {
        Scale = (Scale >= 0.0f) ? SMALL_NUMBER : -SMALL_NUMBER;
    }
    const float InvScale = 1.0f / Scale;
    const float Intensity = Settings->DisplacementIntensity;
    const bool bSlopeMask = Settings->bEnableSlopeMask;
    const float MinDot = Settings->MinSlopeDot;
    const float MaxDot = Settings->MaxSlopeDot;
    const float SlopeRangeInv = (MaxDot - MinDot) > KINDA_SMALL_NUMBER ? 1.0f / (MaxDot - MinDot) : 0.0f;

    // We only want to operate on dynamic mesh inputs from the default pin.
    // Any other inputs (including texture data) are consumed and not
    // forwarded downstream.  This avoids emitting texture data on the
    // dynamic mesh output pin, which would produce warnings and errors.

    // Loop over the default input pin, which contains dynamic mesh data
    // (UPCGDynamicMeshData) and potentially other spatial types.  Skip any
    // entries that are not dynamic meshes.
    for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
    {
        if (!Input.Data)
        {
            continue;
        }

        // Cast the input data to dynamic mesh data.  If the cast fails,
        // ignore this input entirely (do not forward non-mesh data on the
        // mesh output pin).
        const UPCGDynamicMeshData* InMeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
        if (!InMeshData)
        {
            continue;
        }

        // Copy or steal the incoming mesh so we can mutate it safely.  This
        // helper returns a new UPCGDynamicMeshData with an editable
        // UDynamicMesh inside.  If it returns nullptr, skip this input.
        UPCGDynamicMeshData* OutMeshData = CopyOrSteal(Input, Context);
        if (!OutMeshData)
        {
            continue;
        }

        // If no texture data was provided, simply forward the mesh without
        // modification.  Tag it with the same metadata as the input.
        if (!TexData || FMath::IsNearlyZero(Intensity))
        {
            FPCGTaggedData& OutTagged = Context->OutputData.TaggedData.Add_GetRef(Input);
            OutTagged.Data = OutMeshData;
            continue;
        }

        // Retrieve the dynamic mesh reference.  Use GetMutableDynamicMesh()
        // rather than directly accessing DynamicMesh to ensure bounds and
        // octree are marked dirty.  Then grab the FDynamicMesh3 for editing.
        UDynamicMesh* DynMesh = OutMeshData->GetMutableDynamicMesh();
        if (!DynMesh)
        {
            // Should not happen, but if so just forward the data.
            FPCGTaggedData& OutTagged = Context->OutputData.TaggedData.Add_GetRef(Input);
            OutTagged.Data = OutMeshData;
            continue;
        }
        FDynamicMesh3& Mesh = DynMesh->GetMeshRef();

        // Compute per‑vertex normals.  Accumulate face normals for each
        // incident triangle and normalize after the accumulation.  Allocate
        // an array sized by the max vertex ID (not count) because vertex
        // indices in FDynamicMesh3 are not guaranteed to be contiguous.
        TArray<FVector3d> VertexNormals;
        VertexNormals.SetNumZeroed(Mesh.MaxVertexID());
        for (int32 Tid : Mesh.TriangleIndicesItr())
        {
            if (!Mesh.IsTriangle(Tid))
            {
                continue;
            }
            const UE::Geometry::FIndex3i Tri = Mesh.GetTriangle(Tid);
            const FVector3d A = Mesh.GetVertex(Tri.A);
            const FVector3d B = Mesh.GetVertex(Tri.B);
            const FVector3d C = Mesh.GetVertex(Tri.C);
            FVector3d TriNormal = FVector3d::CrossProduct(B - A, C - A);
            const double LenSq = TriNormal.SquaredLength();
            if (LenSq > 0.0)
            {
                TriNormal /= FMath::Sqrt(LenSq);
                VertexNormals[Tri.A] += TriNormal;
                VertexNormals[Tri.B] += TriNormal;
                VertexNormals[Tri.C] += TriNormal;
            }
        }
        for (int32 Vid : Mesh.VertexIndicesItr())
        {
            FVector3d& N = VertexNormals[Vid];
            const double Len = N.Length();
            if (Len > SMALL_NUMBER)
            {
                N /= Len;
            }
            else
            {
                N = FVector3d(0.0, 0.0, 1.0);
            }
        }

        // Lambda to sample the alpha channel from the texture data.  UVs
        // passed are in local 0..1 space derived from world coordinates and
        // projection size.  If sampling fails, returns 0.
        auto SampleAlpha = [&](float U, float V) -> float
        {
            FVector4 OutColor;
            float OutDensity;
            if (TexData->SamplePointLocal(FVector2D(U, V), OutColor, OutDensity))
            {
                return static_cast<float>(OutColor.W);
            }
            return 0.0f;
        };

        // Apply displacement to each vertex.  For each vertex we compute
        // triplanar UVs from its world position scaled by InvScale, sample
        // the height along each axis, blend by the normal components, remap
        // to [−1,1], optionally apply slope mask, and scale by Intensity.
        for (int32 Vid : Mesh.VertexIndicesItr())
        {
            if (!Mesh.IsVertex(Vid))
            {
                continue;
            }
            FVector3d P = Mesh.GetVertex(Vid);
            const FVector3d& N = VertexNormals[Vid];

            // Compute local UVs for each projection axis.  P is in world
            // coordinates; multiply by InvScale to normalize.  Use
            // components as per triplanar mapping.
            const float Ux = static_cast<float>(P.Y * InvScale);
            const float Vx = static_cast<float>(P.Z * InvScale);
            const float Uy = static_cast<float>(P.X * InvScale);
            const float Vy = static_cast<float>(P.Z * InvScale);
            const float Uz = static_cast<float>(P.X * InvScale);
            const float Vz = static_cast<float>(P.Y * InvScale);

            const float AlphaX = SampleAlpha(Ux, Vx);
            const float AlphaY = SampleAlpha(Uy, Vy);
            const float AlphaZ = SampleAlpha(Uz, Vz);

            const float AbsNX = FMath::Abs(static_cast<float>(N.X));
            const float AbsNY = FMath::Abs(static_cast<float>(N.Y));
            const float AbsNZ = FMath::Abs(static_cast<float>(N.Z));
            const float SumAbs = AbsNX + AbsNY + AbsNZ + KINDA_SMALL_NUMBER;
            const float WNX = AbsNX / SumAbs;
            const float WNY = AbsNY / SumAbs;
            const float WNZ = AbsNZ / SumAbs;

            // Compute a weighted alpha value using the normal weights.  Then
            // remap from [0,1] by subtracting the user‑defined
            // DisplacementCenter and multiplying by 2.0.  A center of 0.5
            // yields the original behaviour; other values bias the zero
            // displacement point.
            float WeightedAlpha = WNX * AlphaX + WNY * AlphaY + WNZ * AlphaZ;
            float Height = (WeightedAlpha - Settings->DisplacementCenter) * 2.0f;

            if (bSlopeMask)
            {
                const float DotUp = FMath::Clamp(static_cast<float>(N.Z), 0.0f, 1.0f);
                float Mask = (DotUp - MinDot) * SlopeRangeInv;
                Mask = FMath::Clamp(Mask, 0.0f, 1.0f);
                Height *= Mask;
            }

            const float Disp = Height * Intensity;
            if (!FMath::IsNearlyZero(Disp))
            {
                P += N * static_cast<double>(Disp);
                Mesh.SetVertex(Vid, P);
            }
        }

        // Add the modified mesh to the output.  Use the same tag as the
        // input.  Only assign Data; the other fields are copied from
        // Input by Emplace_GetRef().
        FPCGTaggedData& OutTagged = Context->OutputData.TaggedData.Add_GetRef(Input);
        OutTagged.Data = OutMeshData;
    }

    return true;
}