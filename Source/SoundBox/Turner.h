// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// #include "../Plugins/Wwise/Source/AkAudio/Classes/AkGameplayStatics.h"
// #include "../Plugins/Wwise/Source/AkAudio/Classes/AkGameplayStatics.h"
#include "Turner.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SOUNDBOX_API UTurner : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UTurner();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	void ReceiveTargetTransform(FTransform Transform);

	UFUNCTION(BlueprintCallable)
	void IsPlatformOverlapping(bool IsPlatformOverlapping);

	void SetShouldTurn(bool ShouldTurn);

	void SetPlatformRTPC(FString PlatformRTPC);

	UFUNCTION(BlueprintCallable)
	float GetRotationYaw();

	UPROPERTY(BlueprintReadWrite)
	float RotationYaw;

	UPROPERTY(BlueprintReadWrite)
	bool IsPlatformOverlappingTurner;

	UPROPERTY(BlueprintReadWrite)
	bool TransformationDone;

	UPROPERTY(BlueprintReadWrite)
	FString PlatformRTPCTurner;

	void CalcRotDiffRTPC(FRotator &CurrentRotator, FRotator &TargetRotator);

	UFUNCTION(BlueprintCallable)
	void ReceiveTogglePlayState(bool TogglePlayState);

	UPROPERTY(BlueprintReadWrite)
	bool TogglePlayStateTurner;

private:
	UPROPERTY(EditAnywhere)
	FTransform TargetTransform;

	UPROPERTY(EditAnywhere)
	FTransform StartTransform;

	bool TurnSemaphore = false;
};
