#include "Game/AedirGameMode.h"

#include "HUD/AedirHUD.h"
#include "Player/AedirPlayerCharacter.h"

AAedirGameMode::AAedirGameMode()
{
	DefaultPawnClass = AAedirPlayerCharacter::StaticClass();
	HUDClass = AAedirHUD::StaticClass();
}
