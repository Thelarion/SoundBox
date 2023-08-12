// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Turner.h"
#include "TriggerComponent.generated.h"

/**
 *
 */

class UBoxComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SOUNDBOX_API UTriggerComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UTriggerComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	UTurner *Turner;

	UFUNCTION(BlueprintCallable)
	void SetTurner(UTurner *Turner);

	UFUNCTION(BlueprintCallable)
	void RemoveTurner();

	UFUNCTION(BlueprintCallable)
	void SetPlatformRTPC();

	UFUNCTION(BlueprintCallable)
	void RemovePlatformRTPC();

	AActor *GetAcceptableActor() const;

	UFUNCTION(BlueprintCallable)
	void ReactivateActor();

	UPROPERTY(BlueprintReadWrite)
	FString PlatformRTPC;
};
