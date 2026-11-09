#include "Wounds/AedirSever.h"

#include "Aedir.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Wounds/AedirBloodSubsystem.h"

namespace
{
	constexpr float SeverPieceLifeSeconds = 240.f;
	constexpr float SeverPushShare = 0.5f;
	constexpr int32 SeverSpurtDrops = 14;
	constexpr int32 SeverHullPoints = 48;
	const TCHAR* const SeverFleshPath = TEXT("/Game/Aedir/Materials/M_AedirFlesh.M_AedirFlesh");

	struct FSeverJoint
	{
		const TCHAR* Bone;
		float Joules;
	};

	const FSeverJoint SeverJoints[] = {
		{ TEXT("hand"), 120.f },
		{ TEXT("lowerarm"), 150.f },
		{ TEXT("upperarm"), 210.f },
		{ TEXT("foot"), 150.f },
		{ TEXT("calf"), 200.f },
		{ TEXT("thigh"), 280.f },
		{ TEXT("neck"), 220.f },
		{ TEXT("head"), 220.f }
	};

	UMaterialInterface* SeverFlesh()
	{
		return LoadObject<UMaterialInterface>(nullptr, SeverFleshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

FName AedirSever::FindSeverBone(FName HitBone)
{
	const FString Name = HitBone.ToString().ToLower();
	if (Name.Contains(TEXT("head")) || Name.Contains(TEXT("neck")))
	{
		return FName(TEXT("neck_01"));
	}

	const TCHAR* Suffix = Name.EndsWith(TEXT("_l")) ? TEXT("_l") : (Name.EndsWith(TEXT("_r")) ? TEXT("_r") : nullptr);
	if (!Suffix)
	{
		return NAME_None;
	}
	for (const TCHAR* Part : { TEXT("hand"), TEXT("lowerarm"), TEXT("upperarm"), TEXT("foot"), TEXT("calf"), TEXT("thigh") })
	{
		if (Name.Contains(Part))
		{
			return FName(*(FString(Part) + Suffix));
		}
	}
	// A finger comes off with its hand.
	for (const TCHAR* Finger : { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") })
	{
		if (Name.Contains(Finger))
		{
			return FName(*(FString(TEXT("hand")) + Suffix));
		}
	}
	return NAME_None;
}

float AedirSever::GetSeverJoules(FName SeverBone)
{
	const FString Name = SeverBone.ToString();
	for (const FSeverJoint& Joint : SeverJoints)
	{
		if (Name.StartsWith(Joint.Bone))
		{
			return Joint.Joules;
		}
	}
	return BIG_NUMBER;
}

AActor* AedirSever::CutLimb(USkeletalMeshComponent* Body, FName SeverBone, const FVector& PlanePoint, const FVector& PlaneNormal, const FVector& Push)
{
	UWorld* World = Body ? Body->GetWorld() : nullptr;
	USkeletalMesh* Asset = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	FSkeletalMeshRenderData* RenderData = Asset ? Asset->GetResourceForRendering() : nullptr;
	const int32 RootIndex = Body ? Body->GetBoneIndex(SeverBone) : INDEX_NONE;
	if (!World || !RenderData || RenderData->LODRenderData.IsEmpty() || RootIndex == INDEX_NONE)
	{
		return nullptr;
	}

	// What is worn over the limb goes with it: every piece of armour or clothing that follows the body's pose.
	TArray<USkeletalMeshComponent*> Layers = { Body };
	for (USceneComponent* Child : Body->GetAttachChildren())
	{
		USkeletalMeshComponent* Worn = Cast<USkeletalMeshComponent>(Child);
		if (Worn && Worn->LeaderPoseComponent.Get() == Body && Worn->IsVisible() && Worn->GetSkeletalMeshAsset())
		{
			Layers.Add(Worn);
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The piece is placed without the body's size as a scale: the physics takes a scaled hull only in a wrapper, and a bare
	// one with a scale breaks its queries, so the size goes into the vertices instead.
	FTransform PieceTransform = Body->GetComponentTransform();
	const FVector BodySize = PieceTransform.GetScale3D();
	PieceTransform.SetScale3D(FVector::OneVector);
	AActor* Piece = World->SpawnActor<AActor>(AActor::StaticClass(), PieceTransform, Params);
	if (!Piece)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Piece);
	Mesh->bUseComplexAsSimpleCollision = false;
	Mesh->bUseAsyncCooking = false;
	Piece->SetRootComponent(Mesh);
	Mesh->SetWorldTransform(PieceTransform);
	Mesh->RegisterComponent();

	TArray<FVector> HullPoints;
	int32 MadeSections = 0;
	for (USkeletalMeshComponent* Layer : Layers)
	{
	USkeletalMesh* LayerAsset = Layer->GetSkeletalMeshAsset();
	FSkeletalMeshRenderData* LayerRender = LayerAsset ? LayerAsset->GetResourceForRendering() : nullptr;
	const int32 LayerRoot = LayerAsset ? LayerAsset->GetRefSkeleton().FindBoneIndex(SeverBone) : INDEX_NONE;
	if (!LayerRender || LayerRender->LODRenderData.IsEmpty() || LayerRoot == INDEX_NONE)
	{
		continue;
	}
	const FSkeletalMeshLODRenderData& Lod = LayerRender->LODRenderData[0];
	const FReferenceSkeleton& Skeleton = LayerAsset->GetRefSkeleton();

	// Which bones go with the limb.
	TArray<bool> InLimb;
	InLimb.Init(false, Skeleton.GetNum());
	for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
	{
		InLimb[Bone] = Bone == LayerRoot || (Skeleton.GetParentIndex(Bone) != INDEX_NONE && InLimb[Skeleton.GetParentIndex(Bone)]);
	}

	// The vertices as the body stands this very moment, put into the frame of the body.
	TArray<FMatrix44f> RefToLocals;
	Layer->CacheRefToLocalMatrices(RefToLocals);
	TArray<FVector3f> Skinned;
	USkeletalMeshComponent::ComputeSkinnedPositions(Layer, Skinned, RefToLocals, Lod, Lod.SkinWeightVertexBuffer);
	if (Skinned.IsEmpty())
	{
		UE_LOG(LogAedir, Warning, TEXT("Sever: %s cannot be read on the CPU, run the content commandlet."), *LayerAsset->GetName());
		continue;
	}
	const FTransform LayerToBody = Layer->GetComponentTransform().GetRelativeTransform(Body->GetComponentTransform());
	for (FVector3f& Point : Skinned)
	{
		Point = FVector3f(LayerToBody.TransformPosition(FVector(Point)) * BodySize);
	}

	const FRawStaticIndexBuffer16or32Interface* Indices = Lod.MultiSizeIndexContainer.GetIndexBuffer();
	const FSkinWeightVertexBuffer& Weights = Lod.SkinWeightVertexBuffer;
	const FStaticMeshVertexBuffer& Static = Lod.StaticVertexBuffers.StaticMeshVertexBuffer;

	for (int32 SectionIndex = 0; SectionIndex < Lod.RenderSections.Num(); ++SectionIndex)
	{
		const FSkelMeshRenderSection& Section = Lod.RenderSections[SectionIndex];
		TMap<int32, int32> Remap;
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Triangles;

		// A vertex belongs to the limb when the bone that moves it most does.
		const auto IsInLimb = [&](int32 Vertex)
		{
			int32 BestBone = INDEX_NONE;
			uint16 BestWeight = 0;
			const int32 LocalVertex = Vertex - Section.BaseVertexIndex;
			for (uint32 Influence = 0; Influence < Weights.GetMaxBoneInfluences(); ++Influence)
			{
				const uint16 Weight = Weights.GetBoneWeight(Vertex, Influence);
				if (Weight > BestWeight)
				{
					BestWeight = Weight;
					BestBone = Section.BoneMap.IsValidIndex(Weights.GetBoneIndex(Vertex, Influence)) ? Section.BoneMap[Weights.GetBoneIndex(Vertex, Influence)] : INDEX_NONE;
				}
			}
			return LocalVertex >= 0 && BestBone != INDEX_NONE && InLimb.IsValidIndex(BestBone) && InLimb[BestBone];
		};

		for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
		{
			const uint32 First = Section.BaseIndex + Triangle * 3;
			const int32 Corners[3] = { static_cast<int32>(Indices->Get(First)), static_cast<int32>(Indices->Get(First + 1)), static_cast<int32>(Indices->Get(First + 2)) };
			if (!IsInLimb(Corners[0]) && !IsInLimb(Corners[1]) && !IsInLimb(Corners[2]))
			{
				continue;
			}
			for (const int32 Corner : Corners)
			{
				int32* Known = Remap.Find(Corner);
				if (!Known)
				{
					const int32 New = Vertices.Add(FVector(Skinned[Corner]));
					Normals.Add(LayerToBody.TransformVectorNoScale(FVector(FVector3f(Static.VertexTangentZ(Corner)))));
					UV.Add(FVector2D(Static.GetVertexUV(Corner, 0)));
					Tangents.Add(FProcMeshTangent(LayerToBody.TransformVectorNoScale(FVector(FVector3f(Static.VertexTangentX(Corner)))), false));
					Known = &Remap.Add(Corner, New);
				}
				Triangles.Add(*Known);
			}
		}

		if (!Triangles.IsEmpty())
		{
			Mesh->CreateMeshSection(MadeSections, Vertices, Triangles, Normals, UV, TArray<FColor>(), Tangents, false);
			Mesh->SetMaterial(MadeSections, Layer->GetMaterial(Section.MaterialIndex));
			++MadeSections;
			const int32 Step = FMath::Max(Vertices.Num() / SeverHullPoints, 1);
			for (int32 Index = 0; Index < Vertices.Num(); Index += Step)
			{
				HullPoints.Add(Vertices[Index]);
			}
		}
	}
	}

	if (MadeSections == 0)
	{
		Piece->Destroy();
		return nullptr;
	}

	// The cut itself: the plane of the blow through the joint, keeping the side away from the body.
	const FTransform& ToBody = PieceTransform;
	const FVector JointWorld = Body->GetBoneLocation(SeverBone);
	const FVector ParentWorld = Body->GetBoneLocation(Body->GetParentBone(SeverBone));
	FVector Normal = (JointWorld - ParentWorld).GetSafeNormal();
	// The blade decides the tilt of the cut, but never so far that it runs along the limb.
	if (!PlaneNormal.IsNearlyZero())
	{
		const FVector Blade = FVector::DotProduct(PlaneNormal, Normal) >= 0.0 ? PlaneNormal : -PlaneNormal;
		Normal = (Normal + Blade.GetSafeNormal() * 0.6).GetSafeNormal();
	}
	Mesh->AddCollisionConvexMesh(HullPoints);
	UProceduralMeshComponent* Other = nullptr;
	UKismetProceduralMeshLibrary::SliceProceduralMesh(Mesh, JointWorld, Normal, false, Other, EProcMeshSliceCapOption::CreateNewSectionForCap, SeverFlesh());

	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// What the part weighs on a grown man: a whole arm, a forearm, a leg, a head and so on.
	const FString Part = SeverBone.ToString();
	const float PartKg = Part.StartsWith(TEXT("upperarm")) ? 4.f : Part.StartsWith(TEXT("lowerarm")) ? 2.2f : Part.StartsWith(TEXT("hand")) ? 0.5f
		: Part.StartsWith(TEXT("thigh")) ? 12.f : Part.StartsWith(TEXT("calf")) ? 4.5f : Part.StartsWith(TEXT("foot")) ? 1.2f : Part.StartsWith(TEXT("neck")) || Part.StartsWith(TEXT("head")) ? 5.5f : 3.f;
	Mesh->SetMassOverrideInKg(NAME_None, PartKg, true);
	Mesh->SetLinearDamping(0.1f);
	Mesh->SetAngularDamping(0.6f);
	Mesh->SetSimulatePhysics(true);
	Mesh->AddImpulseAtLocation(Push.GetClampedToMaxSize(600.0) * SeverPushShare * Mesh->GetMass(), JointWorld);
	Piece->SetLifeSpan(SeverPieceLifeSeconds);

	// What stays on the body ends in flesh too, where the bone was.
	const FName ParentBone = Body->GetParentBone(SeverBone);
	double Radius = 0.0;
	int32 Near = 0;
	for (const FVector& Point : HullPoints)
	{
		const double Off = FVector::VectorPlaneProject(ToBody.TransformPosition(Point) - JointWorld, Normal).Size();
		if (FVector::Dist(ToBody.TransformPosition(Point), JointWorld) < 16.0)
		{
			Radius += Off;
			++Near;
		}
	}
	Radius = Near > 0 ? FMath::Clamp(Radius / Near, 2.5, 8.0) : 4.0;
	if (UProceduralMeshComponent* Stump = NewObject<UProceduralMeshComponent>(Body->GetOwner()))
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		const int32 Sides = 14;
		const FVector AxisX = FVector::CrossProduct(Normal, FMath::Abs(Normal.Z) > 0.9 ? FVector::ForwardVector : FVector::UpVector).GetSafeNormal();
		const FVector AxisY = FVector::CrossProduct(Normal, AxisX);
		const FTransform& Parent = Body->GetSocketTransform(ParentBone);
		Vertices.Add(Parent.InverseTransformPositionNoScale(JointWorld));
		Normals.Add(Parent.InverseTransformVectorNoScale(Normal));
		UV.Add(FVector2D(0.5, 0.5));
		for (int32 Index = 0; Index <= Sides; ++Index)
		{
			const double Angle = UE_TWO_PI * Index / Sides;
			const double Rough = Radius * (1.0 + 0.12 * FMath::Sin(Angle * 5.0));
			Vertices.Add(Parent.InverseTransformPositionNoScale(JointWorld + (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle)) * Rough - Normal * 1.5));
			Normals.Add(Parent.InverseTransformVectorNoScale(Normal));
			UV.Add(FVector2D(0.5 + 0.5 * FMath::Cos(Angle), 0.5 + 0.5 * FMath::Sin(Angle)));
		}
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			Triangles.Append({ 0, Index + 1, Index + 2, 0, Index + 2, Index + 1 });
		}
		Stump->SetupAttachment(Body, ParentBone);
		Stump->RegisterComponent();
		Stump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stump->CreateMeshSection(0, Vertices, Triangles, Normals, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		Stump->SetMaterial(0, SeverFlesh());
	}

	if (UAedirBloodSubsystem* Blood = World->GetSubsystem<UAedirBloodSubsystem>())
	{
		for (int32 Index = 0; Index < SeverSpurtDrops; ++Index)
		{
			Blood->SpawnDrop(JointWorld, (Normal * FMath::FRandRange(120.f, 380.f)) + FMath::VRand() * 90.f + FVector(0.f, 0.f, 90.f));
		}
	}

	// Whatever is strapped onto a bone of the limb (a bracer, a greave, what the hand holds) goes with the limb.
	TArray<USceneComponent*> OnLimb;
	for (USceneComponent* Child : Body->GetAttachChildren())
	{
		const FName Socket = Child ? Child->GetAttachSocketName() : NAME_None;
		const FName SocketBone = Socket.IsNone() ? NAME_None : Body->GetSocketBoneName(Socket);
		if (!SocketBone.IsNone() && (SocketBone == SeverBone || Body->BoneIsChildOf(SocketBone, SeverBone)))
		{
			OnLimb.Add(Child);
		}
	}
	for (USceneComponent* Strapped : OnLimb)
	{
		Strapped->AttachToComponent(Mesh, FAttachmentTransformRules::KeepWorldTransform);
	}

	Body->HideBoneByName(SeverBone, PBO_Term);
	// The armour over the lost limb went with it, and is gone from the body.
	for (USkeletalMeshComponent* Layer : Layers)
	{
		if (Layer != Body && Layer->GetBoneIndex(SeverBone) != INDEX_NONE)
		{
			Layer->HideBoneByName(SeverBone, PBO_None);
		}
	}
	return Piece;
}
