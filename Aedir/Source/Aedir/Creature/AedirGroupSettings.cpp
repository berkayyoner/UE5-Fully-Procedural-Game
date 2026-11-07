#include "AedirGroupSettings.h"

namespace
{
	FAedirGroupRelationEntry GroupSettingsMakeRelation(const TCHAR* GroupA, const TCHAR* GroupB, EAedirRelation Relation)
	{
		FAedirGroupRelationEntry Entry;
		Entry.GroupA = FName(GroupA);
		Entry.GroupB = FName(GroupB);
		Entry.Relation = Relation;
		return Entry;
	}
}

UAedirGroupSettings::UAedirGroupSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Aedir Groups");

	if (Groups.IsEmpty())
	{
		Groups = { FName(TEXT("Heroes")), FName(TEXT("Bandits")), FName(TEXT("Wildlife")), FName(TEXT("Guards")), FName(TEXT("Villagers")) };
		Relations =
		{
			GroupSettingsMakeRelation(TEXT("Heroes"), TEXT("Bandits"), EAedirRelation::Enemy),
			GroupSettingsMakeRelation(TEXT("Heroes"), TEXT("Guards"), EAedirRelation::Ally),
			GroupSettingsMakeRelation(TEXT("Guards"), TEXT("Bandits"), EAedirRelation::Enemy),
			GroupSettingsMakeRelation(TEXT("Heroes"), TEXT("Wildlife"), EAedirRelation::Neutral)
		};
	}
}
