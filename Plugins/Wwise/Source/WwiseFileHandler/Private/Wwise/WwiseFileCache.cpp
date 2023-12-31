/*******************************************************************************
The content of this file includes portions of the proprietary AUDIOKINETIC Wwise
Technology released in source code form as part of the game integration package.
The content of this file may not be used without valid licenses to the
AUDIOKINETIC Wwise Technology.
Note that the use of the game engine is subject to the Unreal(R) Engine End User
License Agreement at https://www.unrealengine.com/en-US/eula/unreal
 
License Usage
 
Licensees holding valid licenses to the AUDIOKINETIC Wwise Technology may use
this file in accordance with the end user license agreement provided with the
software or, alternatively, in accordance with the terms contained
in a written agreement between you and Audiokinetic Inc.
Copyright (c) 2023 Audiokinetic Inc.
*******************************************************************************/

#include "Wwise/WwiseFileCache.h"

#include "Wwise/WwiseExecutionQueue.h"
#include "Wwise/WwiseFileHandlerModule.h"
#include "Wwise/Stats/AsyncStats.h"
#include "Wwise/Stats/FileHandler.h"
#include "WwiseDefines.h"

#include "Async/Async.h"
#include "Async/AsyncFileHandle.h"
#if UE_5_0_OR_LATER
#include "HAL/PlatformFileManager.h"
#else
#include "HAL/PlatformFilemanager.h"
#endif

#include <inttypes.h>

FWwiseFileCache* FWwiseFileCache::Get()
{
	if (auto* Module = IWwiseFileHandlerModule::GetModule())
	{
		if (auto* FileCache = Module->GetFileCache())
		{
			return FileCache;
		}
	}
	return nullptr;
}

FWwiseFileCache::FWwiseFileCache()
{
}

FWwiseFileCache::~FWwiseFileCache()
{
}

void FWwiseFileCache::CreateFileCacheHandle(
	FWwiseFileCacheHandle*& OutHandle,
	const FString& Pathname,
	FWwiseFileOperationDone&& OnDone)
{
	OutHandle = new FWwiseFileCacheHandle(Pathname);
	if (UNLIKELY(!OutHandle))
	{
		OnDone(false);
	}
	OutHandle->Open(MoveTemp(OnDone));
}

FWwiseFileCacheHandle::FWwiseFileCacheHandle(const FString& InPathname) :
	Pathname { InPathname },
	FileHandle { nullptr },
	FileSize { 0 },
	InitializationStat { nullptr }
{
}

FWwiseFileCacheHandle::~FWwiseFileCacheHandle()
{
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::~FWwiseFileCacheHandle"));

	const auto* FileHandleToDestroy = FileHandle; FileHandle = nullptr;

	if (UNLIKELY(RequestsInFlight.Load() > 0))
	{
		UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: Closing %s with %" PRIi32 " operations left to process."), *Pathname, RequestsInFlight.Load());
		auto* CanDestroyEvent = FPlatformProcess::GetSynchEventFromPool(false);
		CanDestroy.Store(CanDestroyEvent, EMemoryOrder::SequentiallyConsistent);
		while (RequestsInFlight.Load(EMemoryOrder::SequentiallyConsistent) > 0)
		{
			CanDestroyEvent->Wait(FTimespan::FromMilliseconds(1));
		}
		CanDestroy.Store(nullptr);
		FFunctionGraphTask::CreateAndDispatchWhenReady([CanDestroyEvent]
		{
			FPlatformProcess::ReturnSynchEventToPool(CanDestroyEvent);
		});
	}

	UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: Closing %s."), *Pathname);
	delete FileHandleToDestroy;
	DEC_DWORD_STAT(STAT_WwiseFileHandlerOpenedStreams);
}

void FWwiseFileCacheHandle::Open(FWwiseFileOperationDone&& OnDone)
{
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::Open"));
	check(!InitializationStat);
	check(!FileHandle);

	InitializationStat = new FWwiseAsyncCycleCounter(GET_STATID(STAT_WwiseFileHandlerFileOperationLatency));
	InitializationDone = MoveTemp(OnDone);

	FWwiseAsyncCycleCounter Stat(GET_STATID(STAT_WwiseFileHandlerFileOperationLatency));

	const auto FileCache = FWwiseFileCache::Get();
	if (UNLIKELY(!FileCache))
	{
		UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: FileCache not available while opening %s."), *Pathname);
		delete InitializationStat; InitializationStat = nullptr;
		CallDone(false, MoveTemp(InitializationDone));
		return;
	}

	++RequestsInFlight;
	FileCache->OpenQueue.Async([this, OnDone = MoveTemp(OnDone)]() mutable
	{
		SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::Open Async"));
		check(!FileHandle);

		UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: Opening %s."), *Pathname);
		IAsyncReadFileHandle* CurrentFileHandle;
		ASYNC_INC_DWORD_STAT(STAT_WwiseFileHandlerOpenedStreams);
		{
			SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::Open OpenAsyncRead"));
			CurrentFileHandle = FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*Pathname);
		}
		if (UNLIKELY(!CurrentFileHandle))
		{
			UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: OpenAsyncRead %s failed instantiating."), *Pathname);
			delete InitializationStat; InitializationStat = nullptr;
			CallDone(false, MoveTemp(InitializationDone));
			RemoveRequestInFlight();
			return;
		}

		FAsyncFileCallBack SizeCallbackFunction = [this](bool bWasCancelled, IAsyncReadRequest* Request) mutable
		{
			OnSizeRequestDone(bWasCancelled, Request);
		};
		IAsyncReadRequest* Request;
		{
			SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::Open SizeRequest"));
			// ++RequestsInFlight; already done
			Request = CurrentFileHandle->SizeRequest(&SizeCallbackFunction);
		}
		if (UNLIKELY(!Request))
		{
			UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle: SizeRequest %s failed instantiating."), *Pathname);
			delete InitializationStat; InitializationStat = nullptr;
			CallDone(false, MoveTemp(InitializationDone));
			RemoveRequestInFlight();
		}
	});
}

void FWwiseFileCacheHandle::OnSizeRequestDone(bool bWasCancelled, IAsyncReadRequest* Request)
{
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::OnSizeRequestDone"));
	FileSize = Request->GetSizeResults();

	const bool bSizeOpSuccess = LIKELY(FileSize > 0);

	UE_CLOG(!bSizeOpSuccess, LogWwiseFileHandler, Log, TEXT("FWwiseFileCacheHandle: Streamed file \"%s\" could not be opened."), *Pathname);
	UE_CLOG(bSizeOpSuccess, LogWwiseFileHandler, VeryVerbose, TEXT("FWwiseFileCacheHandle: Initializing %s succeeded."), *Pathname);
	delete InitializationStat; InitializationStat = nullptr;
	CallDone(bSizeOpSuccess, MoveTemp(InitializationDone));
	DeleteRequest(Request);
}

void FWwiseFileCacheHandle::CallDone(bool bResult, FWwiseFileOperationDone&& OnDone)
{
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::CallDone Callback"));
	OnDone(bResult);
}

void FWwiseFileCacheHandle::RemoveRequestInFlight()
{
	auto* CanDestroyEvent = CanDestroy.Load(EMemoryOrder::SequentiallyConsistent);
	--RequestsInFlight;
	if (CanDestroyEvent)
	{
		CanDestroyEvent->Trigger();
	}
}

void FWwiseFileCacheHandle::DeleteRequest(IAsyncReadRequest* Request)
{
	if (!Request || Request->PollCompletion())
	{
		SCOPED_WWISEFILEHANDLER_EVENT_4(TEXT("FWwiseFileCacheHandle::DeleteRequest"));
		delete Request;
		RemoveRequestInFlight();
	}
	else
	{
		const auto FileCache = FWwiseFileCache::Get();
		if (LIKELY(FileCache))
		{
			FileCache->DeleteRequestQueue.AsyncAlways([this, Request]() mutable
			{
				DeleteRequest(Request);
			});
		}
		else
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady([this, Request]() mutable
			{
				DeleteRequest(Request);
			});
		}
	}
};

void FWwiseFileCacheHandle::ReadData(uint8* OutBuffer, int64 Offset, int64 BytesToRead,
	EAsyncIOPriorityAndFlags Priority, FWwiseFileOperationDone&& OnDone)
{
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::ReadData"));
	FWwiseAsyncCycleCounter Stat(GET_STATID(STAT_WwiseFileHandlerFileOperationLatency));
	++RequestsInFlight;

	IAsyncReadFileHandle* CurrentFileHandle = FileHandle;
	if (UNLIKELY(!CurrentFileHandle))
	{
		UE_LOG(LogWwiseFileHandler, Error, TEXT("FWwiseFileCacheHandle::ReadData: Trying to read in file %s while it was not properly initialized."), *Pathname);
		OnReadDataDone(false, MoveTemp(OnDone));
		return;
	}

	UE_LOG(LogWwiseFileHandler, VeryVerbose, TEXT("FWwiseFileCacheHandle::ReadData: %" PRIi64 "@%" PRIi64 " in %s"), BytesToRead, Offset, *Pathname);
	FAsyncFileCallBack ReadCallbackFunction = [this, OnDone = new FWwiseFileOperationDone(MoveTemp(OnDone)), BytesToRead, Stat = MoveTemp(Stat)](bool bWasCancelled, IAsyncReadRequest* Request) mutable
	{
		SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::ReadData Callback"));
		if (!bWasCancelled && Request)	// Do not add Request->GetReadResults() since it will break subsequent results retrievals.
		{
			ASYNC_INC_FLOAT_STAT_BY(STAT_WwiseFileHandlerTotalStreamedMB, static_cast<float>(BytesToRead) / 1024 / 1024);
		}
		OnReadDataDone(bWasCancelled, Request, MoveTemp(*OnDone));
		delete OnDone;
		DeleteRequest(Request);
	};
	ASYNC_INC_FLOAT_STAT_BY(STAT_WwiseFileHandlerStreamingKB, static_cast<float>(BytesToRead) / 1024);
	check(BytesToRead > 0);

#if STATS
	switch (Priority & EAsyncIOPriorityAndFlags::AIOP_PRIORITY_MASK)
	{
	case EAsyncIOPriorityAndFlags::AIOP_CriticalPath: INC_DWORD_STAT(STAT_WwiseFileHandlerCriticalPriority); break;
	case EAsyncIOPriorityAndFlags::AIOP_High: INC_DWORD_STAT(STAT_WwiseFileHandlerHighPriority); break;
	case EAsyncIOPriorityAndFlags::AIOP_BelowNormal: INC_DWORD_STAT(STAT_WwiseFileHandlerBelowNormalPriority); break;
	case EAsyncIOPriorityAndFlags::AIOP_Low: INC_DWORD_STAT(STAT_WwiseFileHandlerLowPriority); break;
	case EAsyncIOPriorityAndFlags::AIOP_MIN: INC_DWORD_STAT(STAT_WwiseFileHandlerBackgroundPriority); break;

	default:
	case EAsyncIOPriorityAndFlags::AIOP_Normal: INC_DWORD_STAT(STAT_WwiseFileHandlerNormalPriority); break;
	}
#endif
	SCOPED_WWISEFILEHANDLER_EVENT_3(TEXT("FWwiseFileCacheHandle::ReadData Async ReadRequest"));
	const auto* Request = CurrentFileHandle->ReadRequest(Offset, BytesToRead, Priority, &ReadCallbackFunction, OutBuffer);
	if (UNLIKELY(!Request))
	{
		UE_LOG(LogWwiseFileHandler, Verbose, TEXT("FWwiseFileCacheHandle::ReadData: ReadRequest %s failed instantiating."), *Pathname);
		ReadCallbackFunction(true, nullptr);
	}
}

void FWwiseFileCacheHandle::ReadAkData(uint8* OutBuffer, int64 Offset, int64 BytesToRead, int8 AkPriority, FWwiseFileOperationDone&& OnDone)
{
	// Wwise priority is what we expect our priority to be. Audio will skip if "our normal" is not met.
	constexpr const auto bHigherAudioPriority = true;

	EAsyncIOPriorityAndFlags Priority;
	if (LIKELY(AkPriority == AK_DEFAULT_PRIORITY))
	{
		Priority = bHigherAudioPriority ? AIOP_High : AIOP_Normal;
	}
	else if (AkPriority <= AK_MIN_PRIORITY)
	{
		Priority = bHigherAudioPriority ? AIOP_BelowNormal : AIOP_Low;
	}
	else if (AkPriority >= AK_MAX_PRIORITY)
	{
		Priority = AIOP_CriticalPath;
	}
	else if (AkPriority < AK_DEFAULT_PRIORITY)
	{
		Priority = bHigherAudioPriority ? AIOP_Normal : AIOP_Low;
	}
	else
	{
		Priority = bHigherAudioPriority ? AIOP_CriticalPath : AIOP_High;
	}
	ReadData(OutBuffer, Offset, BytesToRead, Priority, MoveTemp(OnDone));
}

void FWwiseFileCacheHandle::ReadAkData(const AkIoHeuristics& Heuristics, AkAsyncIOTransferInfo& TransferInfo,
	FWwiseAkFileOperationDone&& Callback)
{
	ReadAkData(
		static_cast<uint8*>(TransferInfo.pBuffer),
		static_cast<int64>(TransferInfo.uFilePosition),
		static_cast<int64>(TransferInfo.uRequestedSize),
		Heuristics.priority,
		[TransferInfo = &TransferInfo, FileOpDoneCallback = MoveTemp(Callback)](bool bResult)
		{
			FileOpDoneCallback(TransferInfo, bResult ? AK_Success : AK_UnknownFileError);
		});
}


void FWwiseFileCacheHandle::OnReadDataDone(bool bWasCancelled, IAsyncReadRequest* Request,
	FWwiseFileOperationDone&& OnDone)
{
	OnReadDataDone(!bWasCancelled && Request && Request->GetReadResults(), MoveTemp(OnDone));
}

void FWwiseFileCacheHandle::OnReadDataDone(bool bResult, FWwiseFileOperationDone&& OnDone)
{
	--RequestsInFlight;
	CallDone(bResult, MoveTemp(OnDone));
}
