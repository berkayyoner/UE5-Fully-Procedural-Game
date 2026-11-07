#include "Npc/AedirTrainingDummy.h"

#include "Aedir.h"
#include "Engine/World.h"
#include "Graphics/AedirScannedMaterial.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr int32 DummySides = 20;
	constexpr float DummyTiltStiffness = 70.f;
	constexpr float DummyTiltDamping = 3.5f;
	constexpr float DummyTiltPerImpulse = 0.0012f;
	constexpr float DummyMaxTiltDegrees = 14.f;
	constexpr float DummyCutJoules = 45.f;
	constexpr float DummyRebuildSeconds = 25.f;
	constexpr float DummyTexturePerCm = 1.f / 60.f;
}

AAedirTrainingDummy::AAedirTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Base = CreateDefaultSubobject<USceneComponent>(TEXT("Base"));
	SetRootComponent(Base);
	Rocker = CreateDefaultSubobject<USceneComponent>(TEXT("Rocker"));
	Rocker->SetupAttachment(Base);
}

void AAedirTrainingDummy::BeginPlay()
{
	Super::BeginPlay();

	WoodMaterial = AedirScannedMaterial::Make(this, TEXT("OldWoodenPole"));
	CutMaterial = AedirScannedMaterial::Make(this, TEXT("OldWoodenBoards"));
	if (!WoodMaterial)
	{
		UE_LOG(LogAedir, Warning, TEXT("Training dummy: the wood maps are missing."));
	}
	Rebuild();
}

UProceduralMeshComponent* AAedirTrainingDummy::AddTimber(FName Name, const FVector& From, const FVector& To, float Radius)
{
	UProceduralMeshComponent* Timber = NewObject<UProceduralMeshComponent>(this, Name);
	Timber->bUseComplexAsSimpleCollision = false;
	Timber->bUseAsyncCooking = false;
	Timber->SetupAttachment(Rocker);
	Timber->RegisterComponent();

	// A rough round log: the radius wanders a little around and along it, so it does not look turned on a lathe.
	const FVector Axis = (To - From).GetSafeNormal();
	const FVector Side = FVector::CrossProduct(FMath::Abs(Axis.Z) > 0.9 ? FVector::ForwardVector : FVector::UpVector, Axis).GetSafeNormal();
	const FVector Other = FVector::CrossProduct(Axis, Side);
	const double Length = FVector::Dist(From, To);
	const int32 Rings = FMath::Max(2, FMath::CeilToInt(Length / 12.0) + 1);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<FProcMeshTangent> Tangents;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		const double Along = Length * Ring / (Rings - 1);
		for (int32 Index = 0; Index <= DummySides; ++Index)
		{
			const double Angle = UE_TWO_PI * Index / DummySides;
			const double Rough = Radius * (1.0 + 0.035 * FMath::Sin(Angle * 3.0 + Along * 0.09) + 0.02 * FMath::Sin(Angle * 7.0 - Along * 0.05));
			const FVector Out = Side * FMath::Cos(Angle) + Other * FMath::Sin(Angle);
			Vertices.Add(From + Axis * Along + Out * Rough);
			Normals.Add(Out);
			UV.Add(FVector2D(static_cast<double>(Index) / DummySides * UE_TWO_PI * Radius * DummyTexturePerCm, Along * DummyTexturePerCm));
			Tangents.Add(FProcMeshTangent(Axis, false));
		}
	}
	for (int32 Ring = 0; Ring + 1 < Rings; ++Ring)
	{
		for (int32 Index = 0; Index < DummySides; ++Index)
		{
			const int32 A = Ring * (DummySides + 1) + Index;
			const int32 B = A + DummySides + 1;
			Triangles.Append({ A, B, A + 1, B, B + 1, A + 1 });
		}
	}
	// The sawn ends.
	for (const int32 Ring : { 0, Rings - 1 })
	{
		const FVector Normal = Ring == 0 ? -Axis : Axis;
		const int32 Center = Vertices.Add(From + Axis * (Length * Ring / (Rings - 1)));
		Normals.Add(Normal);
		UV.Add(FVector2D(0.5, 0.5));
		Tangents.Add(FProcMeshTangent(Side, false));
		const int32 First = Vertices.Num();
		for (int32 Index = 0; Index <= DummySides; ++Index)
		{
			const FVector RingPoint = Vertices[Ring * (DummySides + 1) + Index];
			Vertices.Add(RingPoint);
			Normals.Add(Normal);
			const double Angle = UE_TWO_PI * Index / DummySides;
			UV.Add(FVector2D(0.5 + 0.5 * FMath::Cos(Angle), 0.5 + 0.5 * FMath::Sin(Angle)));
			Tangents.Add(FProcMeshTangent(Side, false));
		}
		for (int32 Index = 0; Index < DummySides; ++Index)
		{
			if (Ring == 0)
			{
				Triangles.Append({ Center, First + Index, First + Index + 1 });
			}
			else
			{
				Triangles.Append({ Center, First + Index + 1, First + Index });
			}
		}
	}

	Timber->CreateMeshSection(0, Vertices, Triangles, Normals, UV, TArray<FColor>(), Tangents, false);
	Timber->SetMaterial(0, WoodMaterial);

	TArray<FVector> Hull;
	for (const FVector& End : { From, To })
	{
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const double Angle = UE_TWO_PI * Index / 10;
			Hull.Add(End + (Side * FMath::Cos(Angle) + Other * FMath::Sin(Angle)) * Radius);
		}
	}
	Timber->AddCollisionConvexMesh(Hull);
	Timber->SetCollisionObjectType(ECC_Pawn);
	Timber->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Timber->SetCollisionResponseToAllChannels(ECR_Block);
	Timber->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Timber->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	return Timber;
}

void AAedirTrainingDummy::Rebuild()
{
	for (UProceduralMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	ClearPieces();
	Tilt = FVector2D::ZeroVector;
	TiltSpeed = FVector2D::ZeroVector;

	Parts.Add(AddTimber(TEXT("FootA"), FVector(-45.0, 0.0, 6.0), FVector(45.0, 0.0, 6.0), 6.f));
	Parts.Add(AddTimber(TEXT("FootB"), FVector(0.0, -45.0, 7.0), FVector(0.0, 45.0, 7.0), 6.f));
	Parts.Add(AddTimber(TEXT("Post"), FVector(0.0, 0.0, 6.0), FVector(0.0, 0.0, 150.0), 9.f));
	Parts.Add(AddTimber(TEXT("Head"), FVector(0.0, 0.0, 150.0), FVector(0.0, 0.0, 178.0), 11.f));
	Parts.Add(AddTimber(TEXT("ArmLeft"), FVector(0.0, 0.0, 125.0), FVector(8.0, -62.0, 128.0), 4.5f));
	Parts.Add(AddTimber(TEXT("ArmRight"), FVector(0.0, 0.0, 125.0), FVector(8.0, 62.0, 128.0), 4.5f));
	Parts.Add(AddTimber(TEXT("Waist"), FVector(-22.0, 0.0, 88.0), FVector(22.0, 0.0, 88.0), 4.f));
}

void AAedirTrainingDummy::ClearPieces()
{
	for (UProceduralMeshComponent* Piece : Pieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	Pieces.Reset();
}

void AAedirTrainingDummy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// It stands on a stiff peg: struck, it rocks and settles.
	const float Step = FMath::Min(DeltaSeconds, 1.f / 30.f);
	TiltSpeed += (-Tilt * DummyTiltStiffness - TiltSpeed * DummyTiltDamping) * Step;
	Tilt += TiltSpeed * Step;
	Tilt = Tilt.ClampAxes(-DummyMaxTiltDegrees, DummyMaxTiltDegrees);
	Rocker->SetRelativeRotation(FRotator(Tilt.X, 0.0, Tilt.Y));

	if (HasAuthority() && RebuildAt > 0.f && GetWorld()->GetTimeSeconds() >= RebuildAt)
	{
		RebuildAt = 0.f;
		MulticastStrike(nullptr, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 0.f, false);
	}
}

void AAedirTrainingDummy::ReceiveStrike(UPrimitiveComponent* Part, const FVector& Location, const FVector& TipVelocity, const FVector& BladeAxis, float EnergyJoules, bool bCuts)
{
	LastStrikeJoules = EnergyJoules;
	UE_LOG(LogAedir, Log, TEXT("Training dummy struck on %s: %.0f J"), *GetNameSafe(Part), EnergyJoules);

	const FVector Normal = FVector::CrossProduct(TipVelocity.GetSafeNormal(), BladeAxis.GetSafeNormal()).GetSafeNormal();
	const bool bFoot = Part && Part->GetName().StartsWith(TEXT("Foot"));
	const bool bSlice = bCuts && EnergyJoules >= DummyCutJoules && !Normal.IsNearlyZero() && !bFoot;
	if (bSlice)
	{
		RebuildAt = GetWorld()->GetTimeSeconds() + DummyRebuildSeconds;
	}
	MulticastStrike(Part, Location, Normal, TipVelocity, EnergyJoules, bSlice);
}

void AAedirTrainingDummy::MulticastStrike_Implementation(UPrimitiveComponent* Part, FVector Location, FVector Normal, FVector Push, float EnergyJoules, bool bSlice)
{
	if (EnergyJoules <= 0.f && !Part)
	{
		Rebuild();
		return;
	}

	const FVector LocalPush = GetActorTransform().InverseTransformVectorNoScale(Push.GetSafeNormal()) * EnergyJoules;
	TiltSpeed += FVector2D(-LocalPush.X, LocalPush.Y) * (DummyTiltPerImpulse * DummyTiltStiffness);

	if (bSlice)
	{
		if (UProceduralMeshComponent* Timber = Cast<UProceduralMeshComponent>(Part))
		{
			SlicePart(Timber, Location, Normal, Push);
		}
	}
}

void AAedirTrainingDummy::SlicePart(UProceduralMeshComponent* Part, const FVector& Location, const FVector& Normal, const FVector& Push)
{
	// What stays is the side towards the middle of the pell.
	const FVector Keep = Base->GetComponentLocation();
	const FVector PlaneNormal = FVector::DotProduct(Keep - Location, Normal) >= 0.0 ? Normal : -Normal;

	UProceduralMeshComponent* Piece = nullptr;
	UKismetProceduralMeshLibrary::SliceProceduralMesh(Part, Location, PlaneNormal, true, Piece, EProcMeshSliceCapOption::CreateNewSectionForCap, CutMaterial ? CutMaterial.Get() : WoodMaterial.Get());
	if (!Piece)
	{
		return;
	}

	Pieces.Add(Piece);
	Piece->SetCollisionObjectType(ECC_PhysicsBody);
	Piece->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Piece->SetSimulatePhysics(true);
	Piece->AddImpulse(Push.GetClampedToMaxSize(500.0) * 0.4, NAME_None, true);

	// What was held up by the part that came off comes down with it.
	if (Part->GetName() == TEXT("Post"))
	{
		for (UProceduralMeshComponent* Other : Parts)
		{
			if (Other && Other != Part && Other->Bounds.Origin.Z > Location.Z)
			{
				Other->SetCollisionObjectType(ECC_PhysicsBody);
				Other->SetSimulatePhysics(true);
			}
		}
	}
}
