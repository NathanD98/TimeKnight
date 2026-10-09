// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeKnightCharacter.h"
#include "TimeKnight.h"
#include "PaladinGearComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/DamageType.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"

ATimeKnightCharacter::ATimeKnightCharacter()
{
	// Set size for player capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;

	// Create the camera boom component
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));

	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.f;
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;

	// Create the camera component
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));

	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	// Paladin look (skin, hair, armour, shield, sword)
	PaladinGear = CreateDefaultSubobject<UPaladinGearComponent>(TEXT("PaladinGear"));

	// Default attack animations: the mannequin's attack set that ships with the project
	AttackAnimations.Add(TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"))));
	AttackAnimations.Add(TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02"))));
	AttackAnimations.Add(TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03"))));

	// Activate ticking in order to update the cursor every frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATimeKnightCharacter::BeginPlay()
{
	Super::BeginPlay();

	// stub
}

void ATimeKnightCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HitTimerHandle);
	GetWorldTimerManager().ClearTimer(EndTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void ATimeKnightCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

	// stub
}

// ------------------------------------------------------------------------------------------------ input

void ATimeKnightCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTimeKnight, Error, TEXT("'%s' Failed to find an Enhanced Input Component, sword attack input not bound."), *GetNameSafe(this));
		return;
	}

	UInputAction* Action = AttackAction;
	UInputMappingContext* Context = AttackMappingContext;

	if (!Action)
	{
		// No asset assigned: build a tiny input action + mapping context in code so it works out of the box.
		RuntimeAttackAction = NewObject<UInputAction>(this, TEXT("IA_SwordAttack_Runtime"));
		RuntimeAttackAction->ValueType = EInputActionValueType::Boolean;

		RuntimeAttackContext = NewObject<UInputMappingContext>(this, TEXT("IMC_SwordAttack_Runtime"));
		RuntimeAttackContext->MapKey(RuntimeAttackAction, EKeys::RightMouseButton);
		RuntimeAttackContext->MapKey(RuntimeAttackAction, EKeys::SpaceBar);
		RuntimeAttackContext->MapKey(RuntimeAttackAction, EKeys::Gamepad_FaceButton_Right);

		Action = RuntimeAttackAction;
		Context = RuntimeAttackContext;
	}

	EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, &ATimeKnightCharacter::OnAttackInput);

	if (Context)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					Subsystem->AddMappingContext(Context, 1);
				}
			}
		}
	}
}

void ATimeKnightCharacter::OnAttackInput()
{
	StartSwordAttack();
}

// ------------------------------------------------------------------------------------------------ attack

void ATimeKnightCharacter::FaceCursor()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	FHitResult Hit;
	if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		FVector Dir = Hit.Location - GetActorLocation();
		Dir.Z = 0.f;
		if (!Dir.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		}
	}
}

float ATimeKnightCharacter::PlayAttackAnimation(UAnimationAsset* Asset)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || !Asset)
	{
		return 0.f;
	}

	const float Rate = FMath::Max(AttackPlayRate, 0.1f);

	// Montage: needs a slot node in the Anim Blueprint, plays through the normal anim graph
	if (UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
	{
		if (UAnimInstance* AnimInstance = Body->GetAnimInstance())
		{
			AnimInstance->Montage_Play(Montage, Rate);
			bUsingSingleNodeAnim = false;
			return Montage->GetPlayLength() / Rate;
		}
		return 0.f;
	}

	// Plain animation sequence: play it directly on the mesh, then hand control back to the Anim Blueprint when it's done
	if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
	{
		SavedAnimClass = Body->GetAnimClass();
		Body->PlayAnimation(Asset, false);
		Body->SetPlayRate(Rate);
		bUsingSingleNodeAnim = true;
		return Sequence->GetPlayLength() / Rate;
	}

	return 0.f;
}

bool ATimeKnightCharacter::StartSwordAttack()
{
	UWorld* World = GetWorld();
	if (!World || bIsAttacking)
	{
		return false;
	}

	// continue the combo if the last attack was recent, otherwise start over
	if (World->GetTimeSeconds() - LastAttackEndTime > ComboResetTime)
	{
		ComboIndex = 0;
	}
	CurrentComboStep = ComboIndex;
	ComboIndex = (ComboIndex + 1) % FMath::Max(1, ComboLength);

	// aim at the mouse cursor, stop walking
	FaceCursor();
	if (AController* MyController = GetController())
	{
		MyController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	bIsAttacking = true;

	// animation
	float Duration = 0.f;
	if (AttackAnimations.Num() > 0)
	{
		UAnimationAsset* Asset = AttackAnimations[CurrentComboStep % AttackAnimations.Num()].LoadSynchronous();
		Duration = PlayAttackAnimation(Asset);
	}
	if (Duration <= 0.f)
	{
		Duration = FallbackAttackDuration;
	}

	// the blade swing is procedural, so the sword moves even if the body animation is a punch
	if (PaladinGear)
	{
		PaladinGear->PlaySwordSwing(CurrentComboStep, Duration);
	}

	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.SetTimer(HitTimerHandle, this, &ATimeKnightCharacter::DealSwordDamage, FMath::Max(Duration * HitTimeFraction, 0.02f), false);
	TimerManager.SetTimer(EndTimerHandle, this, &ATimeKnightCharacter::FinishSwordAttack, FMath::Max(Duration, 0.05f), false);

	OnSwordAttackStarted(CurrentComboStep);
	return true;
}

void ATimeKnightCharacter::DealSwordDamage()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(AttackHalfAngle));

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SwordAttack), false, this);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(AttackReach), QueryParams);

	if (bDebugDrawAttack)
	{
		const float HalfAngleRad = FMath::DegreesToRadians(AttackHalfAngle);
		DrawDebugCone(World, Origin, Forward, AttackReach, HalfAngleRad, HalfAngleRad, 24, FColor::Red, false, 0.5f, 0, 2.f);
		DrawDebugSphere(World, Origin, CloseRange, 16, FColor::Orange, false, 0.5f);
	}

	const float Damage = SwordDamage * ((CurrentComboStep == ComboLength - 1 && ComboLength > 1) ? FinisherMultiplier : 1.f);

	TSet<AActor*> AlreadyHit;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		if (!IsValid(Victim) || Victim == this || AlreadyHit.Contains(Victim))
		{
			continue;
		}

		const FVector ToVictim = Victim->GetActorLocation() - Origin;
		const FVector ToVictim2D = ToVictim.GetSafeNormal2D();
		const bool bInCone = FVector::DotProduct(Forward.GetSafeNormal2D(), ToVictim2D) >= CosHalfAngle;
		const bool bClose = ToVictim.Size2D() <= CloseRange;
		if (!bInCone && !bClose)
		{
			continue;
		}

		AlreadyHit.Add(Victim);

		// knockback first: the victim may be destroyed by the damage below
		if (ACharacter* VictimChar = Cast<ACharacter>(Victim))
		{
			VictimChar->LaunchCharacter(ToVictim2D * KnockbackStrength + FVector(0.f, 0.f, KnockbackUp), true, true);
		}

		UGameplayStatics::ApplyDamage(Victim, Damage, GetController(), this, UDamageType::StaticClass());
		OnSwordHit(Victim, Damage);
	}
}

void ATimeKnightCharacter::FinishSwordAttack()
{
	USkeletalMeshComponent* Body = GetMesh();

	// give the Anim Blueprint back to the mesh
	if (bUsingSingleNodeAnim && Body)
	{
		Body->Stop();
		if (SavedAnimClass)
		{
			Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Body->SetAnimInstanceClass(SavedAnimClass);
		}
		bUsingSingleNodeAnim = false;
	}

	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	bIsAttacking = false;
	LastAttackEndTime = GetWorld()->GetTimeSeconds();
}
