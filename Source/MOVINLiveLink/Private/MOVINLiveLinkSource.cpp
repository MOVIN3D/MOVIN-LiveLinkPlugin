// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkSource.h"
#include "MOVINLiveLinkModule.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkAnimationRole.h"

#define LOCTEXT_NAMESPACE "MOVINLiveLinkSource"

FMOVINLiveLinkSource::FMOVINLiveLinkSource(int32 InPort)
	: Port(InPort)
	, bStopping(false)
	, WaitTime(FTimespan::FromMilliseconds(100))
	, SourceStatus(LOCTEXT("SourceStatus_NotConnected", "Not Connected"))
{
}

FMOVINLiveLinkSource::~FMOVINLiveLinkSource()
{
	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (Socket != nullptr)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FMOVINLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;

	// Create socket and start listening
	FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);
	int32 BufferSize = 2 * 1024 * 1024; // 2 MB receive buffer

	Socket = FUdpSocketBuilder(TEXT("MOVINLiveLinkSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferSize);

	if (Socket == nullptr)
	{
		SourceStatus = FText::Format(LOCTEXT("SourceStatus_SocketError", "Socket Error (Port {0})"), FText::AsNumber(Port));
		return;
	}

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	SourceStatus = FText::Format(LOCTEXT("SourceStatus_Listening", "Listening on port {0}"), FText::AsNumber(Port));

	StartThread();
}

bool FMOVINLiveLinkSource::IsSourceStillValid() const
{
	return !bStopping && Thread != nullptr && Socket != nullptr;
}

bool FMOVINLiveLinkSource::RequestSourceShutdown()
{
	Stop();
	return true;
}

FText FMOVINLiveLinkSource::GetSourceType() const
{
	return LOCTEXT("SourceType", "MOVIN LiveLink");
}

FText FMOVINLiveLinkSource::GetSourceStatus() const
{
	return SourceStatus;
}

FText FMOVINLiveLinkSource::GetSourceMachineName() const
{
	return LOCTEXT("SourceMachineName", "MOVIN Studio");
}

void FMOVINLiveLinkSource::Update()
{
	// Nothing needed for periodic update; data is pushed from the receiver thread
}

// FRunnable interface

bool FMOVINLiveLinkSource::Init()
{
	return true;
}

void FMOVINLiveLinkSource::StartThread()
{
	FString ThreadName = TEXT("MOVINLiveLink UDP Receiver ");
	ThreadName.AppendInt(FAsyncThreadIndex::GetNext());
	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}

uint32 FMOVINLiveLinkSource::Run()
{
	TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();

	while (!bStopping)
	{
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime))
		{
			continue;
		}

		uint32 PendingDataSize;
		while (Socket->HasPendingData(PendingDataSize))
		{
			TArray<uint8> RecvBuffer;
			RecvBuffer.SetNumUninitialized(FMath::Min(PendingDataSize, 65507u));

			int32 BytesRead = 0;
			if (Socket->RecvFrom(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead, *Sender))
			{
				if (BytesRead > 0)
				{
					RecvBuffer.SetNum(BytesRead);
					ProcessReceivedData(RecvBuffer);
				}
			}
		}
	}

	return 0;
}

void FMOVINLiveLinkSource::Stop()
{
	bStopping = true;

	if (Socket != nullptr)
	{
		Socket->Close();
	}
}

// Data processing

void FMOVINLiveLinkSource::ProcessReceivedData(const TArray<uint8>& RawData)
{
	FMOVINDatagram Datagram;
	if (!FMOVINDatagramParser::Parse(RawData, Datagram))
	{
		UE_LOG(LogMOVINLiveLink, Warning, TEXT("Failed to parse incoming UDP datagram (%d bytes)"), RawData.Num());
		return;
	}

	if (!Datagram.bIsValid || Datagram.BoneCount == 0)
	{
		UE_LOG(LogMOVINLiveLink, Warning, TEXT("Received invalid datagram for subject '%s' (Valid=%d, BoneCount=%d)"),
			*Datagram.SubjectName, Datagram.bIsValid, Datagram.BoneCount);
		return;
	}

	// Use SubjectName from the packet directly as the LiveLink subject name
	const FName SubjectName(*Datagram.SubjectName);
	const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

	bool bForceStaticRebuild = false;
	if (Client != nullptr)
	{
		const TSubclassOf<ULiveLinkRole> ExistingRole = Client->GetSubjectRole_AnyThread(SubjectKey);
		if (!ExistingRole)
		{
			bForceStaticRebuild = true;
		}
	}

	// Update static data if skeleton changed
	const bool bSkeletonChanged = UpdateStaticData(Datagram, SubjectName, bForceStaticRebuild);
	int32 SkeletonRevision = 0;
	bool bSkeletonReady = false;
	{
		FScopeLock Lock(&CriticalSection);
		SkeletonRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
		bSkeletonReady = SubjectSkeletonReady.FindRef(SubjectName);
	}

	// Skip frame data on the same packet where the skeleton changed.
	// LiveLink needs one tick to commit the new static data before accepting
	// frames with the new bone layout. The next packet will push normally.
	if (bSkeletonChanged)
	{
		UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': Skeleton changed - skipping frame data this packet to let LiveLink commit new static data"),
			*SubjectName.ToString());
		return;
	}

	if (!bSkeletonReady)
	{
		UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': Skeleton refresh still pending - skipping frame data"), *SubjectName.ToString());
		return;
	}

	{
		FScopeLock Lock(&CriticalSection);
		const int32 CurrentRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
		const int32 CurrentBoneCount = SubjectBoneCounts.FindRef(SubjectName);
		const bool bCurrentReady = SubjectSkeletonReady.FindRef(SubjectName);
		if (CurrentRevision != SkeletonRevision || CurrentBoneCount != Datagram.Bones.Num() || !bCurrentReady)
		{
			UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': Dropping stale frame data (%d bones, revision %d; current %d bones, revision %d, ready=%d)"),
				*SubjectName.ToString(), Datagram.Bones.Num(), SkeletonRevision, CurrentBoneCount, CurrentRevision, bCurrentReady ? 1 : 0);
			return;
		}
	}

	FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& AnimFrameData = *FrameData.Cast<FLiveLinkAnimationFrameData>();
	TArray<FTransform>& Transforms = AnimFrameData.Transforms;
	Transforms.Reserve(Datagram.Bones.Num());

	for (const FMOVINJointData& Bone : Datagram.Bones)
	{
		FTransform Transform;
		Transform.SetLocation(Bone.LocalPosition);
		Transform.SetRotation(Bone.LocalRotation);
		Transform.SetScale3D(Bone.LocalScale);
		Transforms.Add(Transform);
	}

	Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(FrameData));
}

bool FMOVINLiveLinkSource::UpdateStaticData(const FMOVINDatagram& Datagram, const FName& SubjectName, bool bForceRebuild)
{
	// Build current bone name list
	TArray<FName> CurrentBoneNames;
	CurrentBoneNames.Reserve(Datagram.BoneCount);
	for (const FMOVINJointData& Bone : Datagram.Bones)
	{
		CurrentBoneNames.Add(Bone.BoneName);
	}

	// Check if we need to push new static data
	bool bNeedsUpdate = false;
	{
		FScopeLock Lock(&CriticalSection);
		const int32* ExistingCount = SubjectBoneCounts.Find(SubjectName);
		const TArray<FName>* ExistingNames = SubjectBoneNames.Find(SubjectName);

		if (bForceRebuild)
		{
			UE_LOG(LogMOVINLiveLink, Warning, TEXT("Subject '%s': LiveLink subject was missing or invalid - re-registering static data with %d bones"),
				*SubjectName.ToString(), Datagram.BoneCount);
			bNeedsUpdate = true;
		}
		else if (ExistingCount == nullptr)
		{
			UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': First time seen - registering skeleton with %d bones"),
				*SubjectName.ToString(), Datagram.BoneCount);
			bNeedsUpdate = true;
		}
		else if (*ExistingCount != Datagram.BoneCount)
		{
			UE_LOG(LogMOVINLiveLink, Warning, TEXT("Subject '%s': Bone count changed from %d to %d - re-pushing static data"),
				*SubjectName.ToString(), *ExistingCount, Datagram.BoneCount);
			bNeedsUpdate = true;
		}
		else if (ExistingNames == nullptr || *ExistingNames != CurrentBoneNames)
		{
			UE_LOG(LogMOVINLiveLink, Warning, TEXT("Subject '%s': Bone names changed (count still %d) - re-pushing static data"),
				*SubjectName.ToString(), Datagram.BoneCount);
			bNeedsUpdate = true;
		}

		if (bNeedsUpdate)
		{
			SubjectBoneCounts.Add(SubjectName, Datagram.BoneCount);
			SubjectBoneNames.Add(SubjectName, CurrentBoneNames);
			SubjectSkeletonRevisions.FindOrAdd(SubjectName)++;
			SubjectSkeletonReady.Add(SubjectName, false);
		}
	}

	if (!bNeedsUpdate)
	{
		return false;
	}

	int32 SkeletonRevision = 0;
	{
		FScopeLock Lock(&CriticalSection);
		SkeletonRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
	}

	FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData& SkeletonData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

	SkeletonData.BoneNames.Reserve(Datagram.Bones.Num());
	SkeletonData.BoneParents.Reserve(Datagram.Bones.Num());

	for (int32 i = 0; i < Datagram.Bones.Num(); ++i)
	{
		SkeletonData.BoneNames.Add(Datagram.Bones[i].BoneName);
		// First bone is root (parent = -1), all others are flat (parent = 0)
		SkeletonData.BoneParents.Add(i == 0 ? -1 : 0);
	}

	UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': Pushing static data with %d bones to LiveLink"),
		*SubjectName.ToString(), Datagram.Bones.Num());

	Client->PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));

	{
		FScopeLock Lock(&CriticalSection);
		if (SubjectSkeletonRevisions.FindRef(SubjectName) == SkeletonRevision)
		{
			SubjectSkeletonReady.Add(SubjectName, true);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
