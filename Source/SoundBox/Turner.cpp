// Fill out your copyright notice in the Description page of Project Settings.

#include "Turner.h"
#include "Kismet/KismetMathLibrary.h"
// #include "AkAudio.h"
// #include "AkGameplayStatics.h"
// #include "UAkAudioEvent"

// Sets default values for this component's properties
UTurner::UTurner()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void UTurner::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UTurner::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsPlatformOverlappingTurner)
	{
		FRotator CurrentRotator = GetOwner()->GetActorRotation();

		FRotator TargetRotator = TargetTransform.GetRotation().Rotator();

		// Check if SoundBox is in Trigger and not grabbed
		if (!TurnSemaphore)
		{
			return;
		}

		CalcRotDiffRTPC(CurrentRotator, TargetRotator);

		// Rotation
		float SpeedRotation = 4;
		FRotator NewRotator = UKismetMathLibrary::RInterpTo(CurrentRotator, TargetRotator, DeltaTime, SpeedRotation);
		GetOwner()->SetActorRotation(NewRotator);

		// Location
		float SpeedLocation = 10;
		FVector TargetLocation = TargetTransform.GetLocation();
		FVector FloatingTargetLocation = FVector(TargetLocation.X, TargetLocation.Y, 85);
		GetOwner()->SetActorLocation(FloatingTargetLocation);
	}
}

void UTurner::CalcRotDiffRTPC(FRotator &CurrentRotator, FRotator &TargetRotator)
{

	FRotator DifferenceRotator = (TargetRotator - CurrentRotator);

	RotationYaw = DifferenceRotator.Yaw;
	RotationYaw /= 2;

	if (RotationYaw < 0)
	{
		RotationYaw *= (-1);
	}
}

void UTurner::ReceiveTargetTransform(FTransform NewTransform)
{
	TargetTransform = NewTransform;
}

void UTurner::SetShouldTurn(bool ShouldTurn)
{
	TurnSemaphore = ShouldTurn;
}

void UTurner::SetPlatformRTPC(FString PlatformRTPC)
{
	PlatformRTPCTurner = PlatformRTPC;
}

float UTurner::GetRotationYaw()
{
	return RotationYaw;
}

void UTurner::IsPlatformOverlapping(bool IsPlatformOverlapping)
{
	IsPlatformOverlappingTurner = IsPlatformOverlapping;
}

void UTurner::ReceiveTogglePlayState(bool TogglePlayState)
{
	TogglePlayStateTurner = TogglePlayState;
}