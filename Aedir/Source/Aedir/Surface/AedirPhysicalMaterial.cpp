#include "Surface/AedirPhysicalMaterial.h"

void UAedirPhysicalMaterial::PostLoad()
{
	Super::PostLoad();

	ApplySlipperinessToPhysics();
}

#if WITH_EDITOR
void UAedirPhysicalMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ApplySlipperinessToPhysics();
}
#endif

void UAedirPhysicalMaterial::ApplySlipperinessToPhysics()
{
	const float ClampedSlipperiness = FMath::Clamp(Slipperiness, 0.f, 1.f);

	Friction = FMath::Lerp(0.9f, 0.05f, ClampedSlipperiness);
	StaticFriction = Friction * 1.2f;
}
