// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimeKnightCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UPaladinGearComponent;
class UAnimationAsset;
class UAnimInstance;
class UInputAction;
class UInputMappingContext;

/**
 *  A controllable top-down perspective character.
 *  Dresses itself as a paladin (UPaladinGearComponent) and can swing a sword (right mouse button / space / gamepad B).
 */
UCLASS(abstract)
class ATimeKnightCharacter : public ACharacter
{
	GENERATED_BODY()

private:

	/** Top down camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> TopDownCameraComponent;

	/** Camera boom positioning the camera above the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Paladin look: brown skin, black hair, armour, shield and the sword in the right hand */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPaladinGearComponent> PaladinGear;

public:

	/** Constructor */
	ATimeKnightCharacter();

	/** Initialization */
	virtual void BeginPlay() override;

	/** Cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Update */
	virtual void Tick(float DeltaSeconds) override;

	/** Binds the sword attack input */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Returns the camera component **/
	UCameraComponent* GetTopDownCameraComponent() const { return TopDownCameraComponent.Get(); }

	/** Returns the Camera Boom component **/
	USpringArmComponent* GetCameraBoom() const { return CameraBoom.Get(); }

	UPaladinGearComponent* GetPaladinGear() const { return PaladinGear.Get(); }

	/** Starts the next sword attack of the combo. Returns false if one is already in progress. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	bool StartSwordAttack();

	UFUNCTION(BlueprintPure, Category="Combat")
	bool IsAttacking() const { return bIsAttacking; }

	/** Blueprint hook: an attack just started (use for sounds / VFX) */
	UFUNCTION(BlueprintImplementableEvent, Category="Combat")
	void OnSwordAttackStarted(int32 ComboIndex);

	/** Blueprint hook: the sword just damaged HitActor */
	UFUNCTION(BlueprintImplementableEvent, Category="Combat")
	void OnSwordHit(AActor* HitActor, float DamageDealt);

protected:

	// ---- input ----

	/** Optional: your own Input Action asset. Leave empty to use the built-in bindings (RMB, Space, Gamepad Face Button Right). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Input")
	TObjectPtr<UInputAction> AttackAction;

	/** Optional: mapping context that contains AttackAction. Only needed if you set AttackAction yourself and haven't mapped it already. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Input")
	TObjectPtr<UInputMappingContext> AttackMappingContext;

	// ---- animation ----

	/** One entry per combo step. Defaults to the mannequin's unarmed attacks. Replace with sword animations as soon as you have them (sequences or montages). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation")
	TArray<TSoftObjectPtr<UAnimationAsset>> AttackAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.1", ClampMax="4.0"))
	float AttackPlayRate = 1.4f;

	/** Used when no animation could be loaded */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.1", Units="s"))
	float FallbackAttackDuration = 0.6f;

	/** Point of the swing (0..1 of the animation) at which the sword actually hits */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.05", ClampMax="0.95"))
	float HitTimeFraction = 0.4f;

	/** Seconds after an attack ends in which the next attack continues the combo */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.0", Units="s"))
	float ComboResetTime = 0.8f;

	/** Number of steps in the combo */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="1", ClampMax="3"))
	int32 ComboLength = 3;

	// ---- damage ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage")
	float SwordDamage = 25.f;

	/** Damage multiplier for the last hit of the combo */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage")
	float FinisherMultiplier = 1.5f;

	/** How far in front of the knight the blade reaches (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="10.0", Units="cm"))
	float AttackReach = 190.f;

	/** Half-angle of the cone in front of the knight that gets hit (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="5.0", ClampMax="180.0", Units="deg"))
	float AttackHalfAngle = 75.f;

	/** Anything closer than this is hit regardless of the cone (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0", Units="cm"))
	float CloseRange = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage")
	float KnockbackStrength = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Damage")
	float KnockbackUp = 120.f;

	/** Draw the hit cone for a moment on every swing */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugDrawAttack = false;

private:

	/** Built at runtime when no AttackAction asset was assigned */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RuntimeAttackAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeAttackContext;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> SavedAnimClass;

	FTimerHandle HitTimerHandle;
	FTimerHandle EndTimerHandle;

	bool bIsAttacking = false;
	bool bUsingSingleNodeAnim = false;
	int32 ComboIndex = 0;
	int32 CurrentComboStep = 0;
	float LastAttackEndTime = -1000.f;

	void OnAttackInput();
	void FaceCursor();
	float PlayAttackAnimation(UAnimationAsset* Asset);
	void DealSwordDamage();
	void FinishSwordAttack();
};
