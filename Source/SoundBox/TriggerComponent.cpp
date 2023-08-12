// Fill out your copyright notice in the Description page of Project Settings.

#include "TriggerComponent.h"
#include "Turner.h"
// #include "Components/BoxComponent.h"

UTriggerComponent::UTriggerComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void UTriggerComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UTriggerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Turner == nullptr)
	{
		return;
	}

	AActor *Actor = GetAcceptableActor();
	if (Actor != nullptr)
	{

		UPrimitiveComponent *Component = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		bool IsGrabbed = Actor->ActorHasTag("Grabbed");
		if (Component != nullptr && !IsGrabbed)
		{
			Component->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECollisionResponse::ECR_Ignore);
			Component->SetSimulatePhysics(false);
			Turner->SetShouldTurn(true);
		}
	}
}

void UTriggerComponent::SetTurner(UTurner *NewTurner)
{
	Turner = NewTurner;
}

void UTriggerComponent::RemoveTurner()
{
	Turner->SetShouldTurn(false);
	Turner = nullptr;
}

void UTriggerComponent::SetPlatformRTPC()
{
	Turner->SetPlatformRTPC(PlatformRTPC);
}

void UTriggerComponent::RemovePlatformRTPC()
{
	PlatformRTPC = nullptr;
	Turner->SetPlatformRTPC(PlatformRTPC);
}

AActor *UTriggerComponent::GetAcceptableActor() const
{
	TArray<AActor *> Actors;
	GetOverlappingActors(Actors);
	for (AActor *Actor : Actors)
	{
		bool HasAcceptableTag = Actor->ActorHasTag("SoundBox");
		bool IsGrabbed = Actor->ActorHasTag("Grabbed");
		if (HasAcceptableTag && !IsGrabbed)
		{
			return Actor;
		}
	}
	return nullptr;
}

void UTriggerComponent::ReactivateActor()
{
	AActor *Actor = GetAcceptableActor();
	if (Actor != nullptr)
	{
		UPrimitiveComponent *Component = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (Component != nullptr)
		{
			Component->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECollisionResponse::ECR_Block);
			Component->SetSimulatePhysics(true);
		}
	}
}