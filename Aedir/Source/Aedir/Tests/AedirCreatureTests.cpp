#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AedirProceduralAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Game/AedirGameMode.h"
#include "HUD/AedirHUD.h"
#include "Misc/AutomationTest.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Player/AedirPlayerCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirPlayerDefaultsTest, "Aedir.Creature.PlayerDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirPlayerDefaultsTest::RunTest(const FString& Parameters)
{
	const AAedirPlayerCharacter* Player = GetDefault<AAedirPlayerCharacter>();
	TestNotNull(TEXT("Player CDO exists"), Player);
	if (!Player)
	{
		return false;
	}

	USkeletalMeshComponent* Body = Player->GetMesh();
	TestNotNull(TEXT("Body mesh component"), Body);
	if (Body)
	{
		TestNotNull(TEXT("Male skeletal mesh is assigned by default"), Body->GetSkeletalMeshAsset());
		TestTrue(TEXT("Procedural anim instance is used"), Body->GetAnimClass() == UAedirProceduralAnimInstance::StaticClass());
	}

	TestTrue(TEXT("Aedir movement component is used"), Cast<UAedirCharacterMovementComponent>(Player->GetCharacterMovement()) != nullptr);

	const AAedirGameMode* GameMode = GetDefault<AAedirGameMode>();
	TestTrue(TEXT("Game mode spawns the Aedir player"), GameMode->DefaultPawnClass == AAedirPlayerCharacter::StaticClass());
	TestTrue(TEXT("Game mode uses the Aedir HUD"), GameMode->HUDClass == AAedirHUD::StaticClass());
	return true;
}

#endif
