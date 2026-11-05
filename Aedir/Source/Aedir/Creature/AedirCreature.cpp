#include "AedirCreature.h"

#include "AedirGroupSubsystem.h"
#include "AedirStatsComponent.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"

AAedirCreature::AAedirCreature()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Stats = CreateDefaultSubobject<UAedirStatsComponent>(TEXT("Stats"));
	Balance = CreateDefaultSubobject<UAedirBalanceComponent>(TEXT("Balance"));
}

void AAedirCreature::BeginPlay()
{
	Super::BeginPlay();

	if (Stats)
	{
		Stats->OnStrengthChanged.AddDynamic(this, &AAedirCreature::ApplySizeScale);
		ApplySizeScale(Stats->GetSizeScale());
	}
}

void AAedirCreature::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirCreature, GroupId);
}

void AAedirCreature::SetGroupId_Implementation(FName NewGroupId)
{
	GroupId = NewGroupId;
	OnGroupIdChanged.Broadcast(GroupId);
}

EAedirRelation AAedirCreature::GetRelationTo(const AAedirCreature* Other) const
{
	const UWorld* World = GetWorld();
	const UAedirGroupSubsystem* Subsystem = World ? World->GetSubsystem<UAedirGroupSubsystem>() : nullptr;
	if (!Other || !Subsystem)
	{
		return EAedirRelation::Neutral;
	}

	return Subsystem->GetRelation(GroupId, Other->GroupId);
}

bool AAedirCreature::IsEnemyOf(const AAedirCreature* Other) const
{
	return GetRelationTo(Other) == EAedirRelation::Enemy;
}

float AAedirCreature::ReceiveCreatureDamage(float Amount)
{
	if (!HasAuthority() || !Stats)
	{
		return 0.f;
	}

	return Stats->ApplyDamage(Amount);
}

void AAedirCreature::ApplySizeScale(float NewSizeScale)
{
	SetActorScale3D(FVector(NewSizeScale));
}

void AAedirCreature::OnRep_GroupId()
{
	OnGroupIdChanged.Broadcast(GroupId);
}
