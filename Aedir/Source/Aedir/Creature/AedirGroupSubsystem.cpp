#include "AedirGroupSubsystem.h"
#include "AedirGroupSettings.h"

EAedirRelation UAedirGroupSubsystem::GetRelation(FName GroupA, FName GroupB) const
{
	const UAedirGroupSettings* Settings = GetDefault<UAedirGroupSettings>();

	if (GroupA.IsNone() || GroupB.IsNone())
	{
		return Settings->DefaultRelation;
	}

	if (GroupA == GroupB)
	{
		return Settings->SameGroupRelation;
	}

	for (const FAedirGroupRelationEntry& Entry : Settings->Relations)
	{
		const bool bMatchesForward = Entry.GroupA == GroupA && Entry.GroupB == GroupB;
		const bool bMatchesReverse = Entry.GroupA == GroupB && Entry.GroupB == GroupA;
		if (bMatchesForward || bMatchesReverse)
		{
			return Entry.Relation;
		}
	}

	return Settings->DefaultRelation;
}

bool UAedirGroupSubsystem::AreEnemies(FName GroupA, FName GroupB) const
{
	return GetRelation(GroupA, GroupB) == EAedirRelation::Enemy;
}
