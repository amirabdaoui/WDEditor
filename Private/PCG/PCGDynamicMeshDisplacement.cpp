// Copyright BULKHEAD Limited. All Rights Reserved.

// This file implements a custom PCG node that applies a triplanar
// displacement to a dynamic mesh.  The node samples a Texture2D's
// alpha channel at each vertex using a simple triplanar projection
// and displaces vertices along their vertex normals.  Optional
// slope masking can attenuate the displacement based on the angle of
// the surface relative to the world up vector.

// Include the public header for the displacement node.  The file is located
// in the Public/PCG folder of the module so we prefix with "PCG/" to
// resolve correctly in include paths.
// Include our public settings header.  This file lives in the Public/PCG folder
// so we prefix with "PCG/" to resolve correctly.
#include "PCG/PCGDynamicMeshDisplacement.h"

// PCG includes
#include "PCGContext.h"
#include "Data/PCGDynamicMeshData.h"
#include "PCGPin.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

// Geometry includes
#include "DynamicMesh/DynamicMesh3.h"

// Engine includes
#include "Engine/Texture2D.h"
#include "Math/Vector.h"
#include "Math/UnrealMathUtility.h"
#include "UDynamicMesh.h" // for UDynamicMesh and related enums
// Use TArray64 for GetMipData; defined in Containers/Array.h
#include "Containers/Array.h"

using UE::Geometry::FDynamicMesh3;

namespace
{
    /**
     * PCG element responsible for displacing a dynamic mesh based on a
     * triplanar texture sample.  This element derives from
     * IPCGDynamicMeshBaseElement so that it can work with dynamic mesh
     * inputs and use the CopyOrSteal helper.  Only ExecuteInternal is
     * implemented because ProcessDynamicMesh is not part of the base class
     * in UE 5.6.
     */
    class FPCGDynamicMeshDisplacementElement final : public IPCGDynamicMeshBaseElement
    {
    protected:
        virtual bool ExecuteInternal(FPCGContext* Context) const override;
    };
}

UPCGDynamicMeshDisplacementSettings::UPCGDynamicMeshDisplacementSettings() {} // default


FPCGElementPtr UPCGDynamicMeshDisplacementSettings::CreateElement() const
{
    return MakeShared<FPCGDynamicMeshDisplacementElement>();
}

bool FPCGDynamicMeshDisplacementElement::ExecuteInternal(FPCGContext* Context) const
{
    // Get settings
    const UPCGDynamicMeshDisplacementSettings* Settings = Context ? Context->GetInputSettings<UPCGDynamicMeshDisplacementSettings>() : nullptr;
    if (!Settings)
    {
        return true;
    }

    // Iterate over each dynamic mesh input connected to the default input pin
    for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
    {
        // Copy or steal the mesh using the base class helper.  This duplicates the mesh
        // if it is still needed elsewhere or steals it if it is safe to do so.
        UPCGDynamicMeshData* OutMeshData = CopyOrSteal(Input, Context);
        if (!OutMeshData)
        {
            continue;
        }
        // Apply displacement if we have a valid texture and intensity
        if (Settings->DisplacementTexture && Settings->DisplacementIntensity != 0.0f)
        {
            UTexture2D* Texture = Settings->DisplacementTexture;
            // Use the texture source dimensions instead of GetSizeX/GetSizeY which
            // require a rendering thread context.  Source dimensions are safe to
            // read from worker threads.
            const int32 SizeX = Texture->Source.GetSizeX();
            const int32 SizeY = Texture->Source.GetSizeY();
            // Read pixel data from the texture source using TArray64
            TArray64<uint8> PixelBytes;
            const bool bGotData = Texture->Source.GetMipData(PixelBytes, 0, 0, 0);
            const FColor* Pixels = nullptr;
            const int64 ExpectedBytes = static_cast<int64>(SizeX) * SizeY * static_cast<int64>(sizeof(FColor));
            if (bGotData && static_cast<int64>(PixelBytes.Num()) >= ExpectedBytes)
            {
                Pixels = reinterpret_cast<const FColor*>(PixelBytes.GetData());
            }
            // Allow negative projection sizes to flip the projection.  Avoid
            // divide-by-zero by clamping very small values.  We don't take
            // absolute value here so that the sign of the scale is preserved.
            float Scale = Settings->ProjectionWorldSize;
            if (FMath::IsNearlyZero(Scale))
            {
                Scale = (Scale >= 0.0f ? 1.0f : -1.0f) * KINDA_SMALL_NUMBER;
            }
            const float InvScale = 1.0f / Scale;
            const bool bSlopeMask = Settings->bUseSlopeMask && (Settings->MaxSlopeDot > Settings->MinSlopeDot);
            const float SlopeRangeInv = bSlopeMask ? (1.0f / FMath::Max(KINDA_SMALL_NUMBER, Settings->MaxSlopeDot - Settings->MinSlopeDot)) : 1.0f;
            UDynamicMesh* DynMesh = OutMeshData->GetMutableDynamicMesh();
            if (DynMesh)
            {
                FDynamicMesh3& Mesh = DynMesh->GetMeshRef();
                // Compute vertex normals manually
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
                // Lambda to sample the alpha channel at world-space UV coordinates via triplanar projection
                auto SampleAlpha = [&](float U, float V) -> float
                {
                    if (!Pixels)
                    {
                        return 0.0f;
                    }
                    float UWrapped = FMath::Frac(U);
                    if (UWrapped < 0.0f) UWrapped += 1.0f;
                    float VWrapped = FMath::Frac(V);
                    if (VWrapped < 0.0f) VWrapped += 1.0f;
                    const int32 X = FMath::Clamp(static_cast<int32>(UWrapped * SizeX), 0, SizeX - 1);
                    const int32 Y = FMath::Clamp(static_cast<int32>(VWrapped * SizeY), 0, SizeY - 1);
                    return static_cast<float>(Pixels[Y * SizeX + X].A) / 255.0f;
                };
                // Iterate vertices and displace
                for (int32 Vid : Mesh.VertexIndicesItr())
                {
                    if (!Mesh.IsVertex(Vid))
                    {
                        continue;
                    }
                    FVector3d P = Mesh.GetVertex(Vid);
                    const FVector3d& N = VertexNormals[Vid];
                    float AbsNX = FMath::Abs((float)N.X);
                    float AbsNY = FMath::Abs((float)N.Y);
                    float AbsNZ = FMath::Abs((float)N.Z);
                    float Sum = AbsNX + AbsNY + AbsNZ;
                    if (Sum > 0.0f)
                    {
                        AbsNX /= Sum;
                        AbsNY /= Sum;
                        AbsNZ /= Sum;
                    }
                    float AlphaX = AbsNX > 0.0f ? SampleAlpha((float)(P.Y * InvScale), (float)(P.Z * InvScale)) : 0.0f;
                    float AlphaY = AbsNY > 0.0f ? SampleAlpha((float)(P.X * InvScale), (float)(P.Z * InvScale)) : 0.0f;
                    float AlphaZ = AbsNZ > 0.0f ? SampleAlpha((float)(P.X * InvScale), (float)(P.Y * InvScale)) : 0.0f;
                    float Height = AbsNX * AlphaX + AbsNY * AlphaY + AbsNZ * AlphaZ;
                    if (bSlopeMask)
                    {
                        float DotUp = FMath::Clamp((float)N.Z, 0.0f, 1.0f);
                        float T = (DotUp - Settings->MinSlopeDot) * SlopeRangeInv;
                        T = FMath::Clamp(T, 0.0f, 1.0f);
                        Height *= T;
                    }
                    const float Displacement = Height * Settings->DisplacementIntensity;
                    if (Displacement != 0.0f)
                    {
                        P += N * (double)Displacement;
                        Mesh.SetVertex(Vid, P);
                    }
                }
            }
        }
        // Tag output and pass along other attributes
        FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
        Output.Data = OutMeshData;
        Output.Pin = PCGPinConstants::DefaultOutputLabel;
    }
    return true;
}

// Helper to sample alpha from a UTexture2D at UV coordinates with wrap
static float SampleTextureAlpha(const UTexture2D* Texture, const FIntPoint& TexSize, const FColor* Pixels, float U, float V)
{
    // Wrap UV into [0,1]
    U = FMath::Frac(U);
    if (U < 0.0f) U += 1.0f;
    V = FMath::Frac(V);
    if (V < 0.0f) V += 1.0f;

    // Convert UV to pixel coordinates
    const int32 X = FMath::Clamp(static_cast<int32>(U * TexSize.X), 0, TexSize.X - 1);
    const int32 Y = FMath::Clamp(static_cast<int32>(V * TexSize.Y), 0, TexSize.Y - 1);
    const int32 Index = Y * TexSize.X + X;
    // Read alpha channel (0-255) and normalize
    return (float)Pixels[Index].A / 255.0f;
}

