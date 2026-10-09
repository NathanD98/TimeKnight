// TimeKnight - paladin gear kit

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PaladinGearComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** One piece of gear (sword, shield, hair...) that gets bolted to a bone of the character's skeletal mesh. */
USTRUCT(BlueprintType)
struct FPaladinGearItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	bool bEnabled = true;

	/** Static mesh imported from Content/Paladin/Meshes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Bone the item is attached to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	FName Bone;

	/** Optional: the item pivot is placed between Bone and AnchorBone (e.g. palm = hand_r -> middle_01_r) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	FName AnchorBone;

	/** 0 = at Bone, 1 = at AnchorBone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AnchorAlpha = 0.f;

	/** Offset in CHARACTER space (X forward, Y right, Z up), in cm, measured in the skeleton's reference pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	FVector Offset = FVector::ZeroVector;

	/** Rotation in CHARACTER space, measured in the skeleton's reference pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	FVector Scale = FVector::OneVector;

	/** The weapon is the item that gets the procedural slash animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gear")
	bool bIsWeapon = false;
};

/** Colour/metal/roughness applied to every mesh material slot whose name contains SlotKeyword */
USTRUCT(BlueprintType)
struct FPaladinMaterialPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material")
	FString SlotKeyword;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Metallic = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Roughness = 0.5f;
};

/**
 *  Turns the owning ACharacter into a paladin at BeginPlay:
 *   - paints the body with a brown skin material
 *   - attaches the black hair, cuirass, tabard, pauldrons, shield and longsword
 *
 *  Gear is positioned from the skeleton's REFERENCE POSE, so it does not depend on how the bones' local axes are
 *  oriented. Tune everything through the Items array (Offset / Rotation / Scale) on the component.
 */
UCLASS(ClassGroup=(TimeKnight), meta=(BlueprintSpawnableComponent))
class UPaladinGearComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPaladinGearComponent();

	/** Equip automatically on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin")
	bool bAutoEquip = true;

	/** Replace the body's materials with a plain skin-coloured material */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Skin")
	bool bApplySkin = true;

	/** Brown skin tone (linear colour) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Skin")
	FLinearColor SkinColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Skin", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SkinRoughness = 0.55f;

	/** Parent material with Color / Metallic / Roughness parameters. Created by Content/Paladin/Python/SetupPaladinMaterials.py.
	 *  If it is missing, the engine's BasicShapeMaterial is used (colour only, no metal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Materials")
	TSoftObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Materials")
	TArray<FPaladinMaterialPreset> MaterialPresets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Gear")
	TArray<FPaladinGearItem> Items;

	/** Print bone reference positions and gear fitting to the Output Log (helps with tuning) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Paladin|Debug")
	bool bLogFitting = false;

	UFUNCTION(BlueprintCallable, Category="Paladin")
	void Equip();

	UFUNCTION(BlueprintCallable, Category="Paladin")
	void Unequip();

	/** Procedural slash for the sword on top of whatever body animation is playing. Combo 0/1 = horizontal, 2 = overhead */
	UFUNCTION(BlueprintCallable, Category="Paladin")
	void PlaySwordSwing(int32 ComboIndex, float Duration);

	UFUNCTION(BlueprintPure, Category="Paladin")
	UStaticMeshComponent* GetSwordComponent() const { return SwordComp; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SpawnedGear;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SwordComp;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** Sword rest pose, expressed in character space, plus the transform that maps character space to bone space */
	FQuat SwordBaseQuat = FQuat::Identity;
	FVector SwordBaseLoc = FVector::ZeroVector;
	FVector SwordBaseScale = FVector::OneVector;
	FTransform SwordFixup = FTransform::Identity;

	bool bSwinging = false;
	float SwingElapsed = 0.f;
	float SwingDuration = 0.f;
	int32 SwingCombo = 0;

	UMaterialInterface* ResolveBaseMaterial() const;
	UMaterialInstanceDynamic* MakeMaterial(UMaterialInterface* Base, const FPaladinMaterialPreset& Preset);
	void ApplySkin(USkeletalMeshComponent* Body, UMaterialInterface* Base);
	void SpawnItem(const FPaladinGearItem& Item, USkeletalMeshComponent* Body, UMaterialInterface* Base);
	void ApplySwordPose(float U);
};
