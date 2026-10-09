// TimeKnight - paladin gear kit

#include "PaladinGearComponent.h"
#include "TimeKnight.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ReferenceSkeleton.h"

namespace
{
	/** Component-space transform of a bone in the skeleton's reference pose (independent of any animation) */
	FTransform GetRefBoneComponentSpace(const FReferenceSkeleton& Ref, int32 BoneIndex)
	{
		const TArray<FTransform>& Pose = Ref.GetRefBonePose();
		FTransform Result = FTransform::Identity;
		while (BoneIndex != INDEX_NONE && Pose.IsValidIndex(BoneIndex))
		{
			Result = Result * Pose[BoneIndex];
			BoneIndex = Ref.GetParentIndex(BoneIndex);
		}
		return Result;
	}

	TSoftObjectPtr<UStaticMesh> GearMesh(const TCHAR* Name)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/Paladin/Meshes/%s.%s"), Name, Name)));
	}

	FPaladinMaterialPreset MakePreset(const TCHAR* Keyword, FColor SRGB, float Metallic, float Roughness)
	{
		FPaladinMaterialPreset P;
		P.SlotKeyword = Keyword;
		P.Color = FLinearColor::FromSRGBColor(SRGB);
		P.Metallic = Metallic;
		P.Roughness = Roughness;
		return P;
	}
}

UPaladinGearComponent::UPaladinGearComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// warm brown skin (sRGB 141, 85, 36)
	SkinColor = FLinearColor::FromSRGBColor(FColor(141, 85, 36));

	BaseMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Paladin/Materials/M_PaladinFlat.M_PaladinFlat")));

	// first entry doubles as the fallback for unknown slot names
	MaterialPresets.Add(MakePreset(TEXT("Steel"),   FColor(184, 189, 199), 0.90f, 0.35f));
	MaterialPresets.Add(MakePreset(TEXT("Gold"),    FColor(242, 184,  56), 0.90f, 0.30f));
	MaterialPresets.Add(MakePreset(TEXT("Leather"), FColor( 66,  36,  22), 0.00f, 0.80f));
	MaterialPresets.Add(MakePreset(TEXT("Cloth"),   FColor( 22,  42, 130), 0.00f, 0.85f));
	MaterialPresets.Add(MakePreset(TEXT("Hair"),    FColor( 10,  10,  12), 0.00f, 0.45f));

	// ---- default gear layout (character space: X forward, Y right, Z up, cm). First-pass values for the UE5 mannequin. ----
	auto Add = [this](const TCHAR* MeshName, FName Bone, FVector Offset, FRotator Rotation = FRotator::ZeroRotator, FName AnchorBone = NAME_None, float AnchorAlpha = 0.f, bool bWeapon = false)
	{
		FPaladinGearItem Item;
		Item.Mesh = GearMesh(MeshName);
		Item.Bone = Bone;
		Item.Offset = Offset;
		Item.Rotation = Rotation;
		Item.AnchorBone = AnchorBone;
		Item.AnchorAlpha = AnchorAlpha;
		Item.bIsWeapon = bWeapon;
		Items.Add(Item);
	};

	Add(TEXT("SM_Paladin_Hair"),     TEXT("head"),      FVector(1.5f, 0.f, 9.5f));
	Add(TEXT("SM_Paladin_Chest"),    TEXT("spine_04"),  FVector(0.5f, 0.f, 0.f));
	Add(TEXT("SM_Paladin_Tabard"),   TEXT("spine_01"),  FVector(12.5f, 0.f, 2.f));
	Add(TEXT("SM_Paladin_Pauldron"), TEXT("upperarm_r"), FVector(0.f,  1.f, -1.f));
	Add(TEXT("SM_Paladin_Pauldron"), TEXT("upperarm_l"), FVector(0.f, -1.f, -1.f));
	Add(TEXT("SM_Paladin_Shield"),   TEXT("lowerarm_l"), FVector(3.f, -8.f, 0.f), FRotator::ZeroRotator, TEXT("hand_l"), 0.5f);
	// blade tilted 50 degrees forward from vertical, gripped in the middle of the palm
	Add(TEXT("SM_Paladin_Sword"),    TEXT("hand_r"),    FVector::ZeroVector, FRotator(-50.f, 0.f, 0.f), TEXT("middle_01_r"), 0.5f, true);
}

void UPaladinGearComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoEquip)
	{
		Equip();
	}
}

void UPaladinGearComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unequip();
	Super::EndPlay(EndPlayReason);
}

UMaterialInterface* UPaladinGearComponent::ResolveBaseMaterial() const
{
	UMaterialInterface* Base = BaseMaterial.LoadSynchronous();
	if (!Base)
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		UE_LOG(LogTimeKnight, Warning, TEXT("PaladinGear: %s not found, falling back to BasicShapeMaterial (colour only). Run SetupPaladinMaterials.py for metal/roughness."), *BaseMaterial.ToString());
	}
	return Base;
}

UMaterialInstanceDynamic* UPaladinGearComponent::MakeMaterial(UMaterialInterface* Base, const FPaladinMaterialPreset& Preset)
{
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
	MID->SetVectorParameterValue(TEXT("Color"), Preset.Color);
	MID->SetScalarParameterValue(TEXT("Metallic"), Preset.Metallic);
	MID->SetScalarParameterValue(TEXT("Roughness"), Preset.Roughness);
	DynamicMaterials.Add(MID);
	return MID;
}

void UPaladinGearComponent::ApplySkin(USkeletalMeshComponent* Body, UMaterialInterface* Base)
{
	if (!bApplySkin || !Base)
	{
		return;
	}

	FPaladinMaterialPreset Skin;
	Skin.SlotKeyword = TEXT("Skin");
	Skin.Color = SkinColor;
	Skin.Metallic = 0.f;
	Skin.Roughness = SkinRoughness;

	for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
	{
		Body->SetMaterial(i, MakeMaterial(Base, Skin));
	}
}

void UPaladinGearComponent::Equip()
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = OwnerChar ? OwnerChar->GetMesh() : nullptr;
	if (!Body || !Body->GetSkeletalMeshAsset())
	{
		UE_LOG(LogTimeKnight, Warning, TEXT("PaladinGear: owner has no skeletal mesh, nothing to equip"));
		return;
	}

	Unequip();

	UMaterialInterface* Base = ResolveBaseMaterial();
	ApplySkin(Body, Base);

	for (const FPaladinGearItem& Item : Items)
	{
		if (Item.bEnabled)
		{
			SpawnItem(Item, Body, Base);
		}
	}
}

void UPaladinGearComponent::Unequip()
{
	bSwinging = false;
	SetComponentTickEnabled(false);

	for (UStaticMeshComponent* Comp : SpawnedGear)
	{
		if (IsValid(Comp))
		{
			Comp->DestroyComponent();
		}
	}
	SpawnedGear.Reset();
	SwordComp = nullptr;
	DynamicMaterials.Reset();
}

void UPaladinGearComponent::SpawnItem(const FPaladinGearItem& Item, USkeletalMeshComponent* Body, UMaterialInterface* Base)
{
	UStaticMesh* Mesh = Item.Mesh.LoadSynchronous();
	if (!Mesh)
	{
		UE_LOG(LogTimeKnight, Warning, TEXT("PaladinGear: mesh '%s' not found - import the .glb files into /Game/Paladin/Meshes"), *Item.Mesh.ToString());
		return;
	}

	const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 BoneIdx = Ref.FindBoneIndex(Item.Bone);
	if (BoneIdx == INDEX_NONE)
	{
		UE_LOG(LogTimeKnight, Warning, TEXT("PaladinGear: bone '%s' does not exist on the skeleton, skipping '%s'"), *Item.Bone.ToString(), *Mesh->GetName());
		return;
	}

	// ---- work out where the item has to sit, in the reference pose, in character space ----
	const FTransform MeshRel = Body->GetRelativeTransform();                        // mesh component -> actor
	const FTransform BoneCS = GetRefBoneComponentSpace(Ref, BoneIdx);              // bone -> mesh component (ref pose)
	FVector Anchor = (BoneCS * MeshRel).GetLocation();

	if (!Item.AnchorBone.IsNone())
	{
		const int32 AnchorIdx = Ref.FindBoneIndex(Item.AnchorBone);
		if (AnchorIdx != INDEX_NONE)
		{
			const FVector AnchorLoc = (GetRefBoneComponentSpace(Ref, AnchorIdx) * MeshRel).GetLocation();
			Anchor = FMath::Lerp(Anchor, AnchorLoc, Item.AnchorAlpha);
		}
	}

	const FQuat Quat = Item.Rotation.Quaternion();
	const FVector Loc = Anchor + Item.Offset;
	const FTransform Desired(Quat, Loc, Item.Scale);

	// character space -> bone space, so that the item stays glued to the bone whatever the animation does
	const FTransform Fixup = MeshRel.Inverse() * BoneCS.Inverse();

	if (bLogFitting)
	{
		UE_LOG(LogTimeKnight, Log, TEXT("PaladinGear: %-24s bone %-12s ref pos (char space) = %s  -> placed at %s"),
			*Mesh->GetName(), *Item.Bone.ToString(), *(BoneCS * MeshRel).GetLocation().ToString(), *Loc.ToString());
	}

	// ---- create and attach the component ----
	AActor* OwnerActor = GetOwner();
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCanEverAffectNavigation(false);
	Comp->RegisterComponent();
	OwnerActor->AddInstanceComponent(Comp);
	Comp->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, Item.Bone);
	Comp->SetRelativeTransform(Desired * Fixup);

	// ---- swap the imported materials for the parameterised ones, matched by slot name ----
	if (Base)
	{
		const TArray<FName> SlotNames = Comp->GetMaterialSlotNames();
		for (int32 Slot = 0; Slot < SlotNames.Num(); ++Slot)
		{
			const FPaladinMaterialPreset* Match = nullptr;
			for (const FPaladinMaterialPreset& Preset : MaterialPresets)
			{
				if (SlotNames[Slot].ToString().Contains(Preset.SlotKeyword, ESearchCase::IgnoreCase))
				{
					Match = &Preset;
					break;
				}
			}
			if (!Match && MaterialPresets.Num() > 0)
			{
				Match = &MaterialPresets[0];
			}
			if (Match)
			{
				Comp->SetMaterial(Slot, MakeMaterial(Base, *Match));
			}
		}
	}

	SpawnedGear.Add(Comp);

	if (Item.bIsWeapon)
	{
		SwordComp = Comp;
		SwordBaseQuat = Quat;
		SwordBaseLoc = Loc;
		SwordBaseScale = Item.Scale;
		SwordFixup = Fixup;
	}
}

void UPaladinGearComponent::PlaySwordSwing(int32 ComboIndex, float Duration)
{
	if (!SwordComp)
	{
		return;
	}

	SwingCombo = FMath::Max(0, ComboIndex);
	SwingDuration = FMath::Max(Duration, 0.05f);
	SwingElapsed = 0.f;
	bSwinging = true;
	SetComponentTickEnabled(true);
	ApplySwordPose(0.f);
}

void UPaladinGearComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bSwinging || !SwordComp)
	{
		SetComponentTickEnabled(false);
		return;
	}

	SwingElapsed += DeltaTime;
	const float U = SwingElapsed / SwingDuration;
	if (U >= 1.f)
	{
		bSwinging = false;
		ApplySwordPose(-1.f);
		SetComponentTickEnabled(false);
	}
	else
	{
		ApplySwordPose(U);
	}
}

void UPaladinGearComponent::ApplySwordPose(float U)
{
	if (!SwordComp)
	{
		return;
	}

	FQuat Extra = FQuat::Identity;
	if (U >= 0.f)
	{
		// hold the wind-up for the first 10%, sweep for 55%, hold the follow-through
		const float T = FMath::Clamp((U - 0.10f) / 0.55f, 0.f, 1.f);
		const float S = T * T * (3.f - 2.f * T);

		float Pitch = 0.f;
		float Yaw = 0.f;
		switch (SwingCombo % 3)
		{
		case 0:  Yaw = FMath::Lerp(70.f, -80.f, S); break;     // right -> left
		case 1:  Yaw = FMath::Lerp(-80.f, 70.f, S); break;     // left -> right
		default: Pitch = FMath::Lerp(75.f, -85.f, S); break;   // overhead chop
		}
		Extra = FRotator(Pitch, Yaw, 0.f).Quaternion();
	}

	const FTransform Desired(Extra * SwordBaseQuat, SwordBaseLoc, SwordBaseScale);
	SwordComp->SetRelativeTransform(Desired * SwordFixup);
}
