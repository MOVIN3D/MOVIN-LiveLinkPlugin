// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Common/UdpSocketBuilder.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include "MOVINDatagram.h"

class ILiveLinkClient;

/**
 * LiveLink source that receives motion capture data from MOVIN Studio via UDP.
 * 
 * Runs a background thread that listens on a configurable UDP port,
 * parses incoming MOVIN Studio packets, and pushes skeleton/animation
 * data into the LiveLink system.
 */
class MOVINLIVELINK_API FMOVINLiveLinkSource : public ILiveLinkSource, public FRunnable
{
public:

	FMOVINLiveLinkSource(int32 InPort);
	virtual ~FMOVINLiveLinkSource();

	// ~Begin ILiveLinkSource interface
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceStillValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceStatus() const override;
	virtual FText GetSourceMachineName() const override;
	virtual void Update() override;
	// ~End ILiveLinkSource interface

	// ~Begin FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override {}
	// ~End FRunnable interface

private:

	/** Start the receiver thread */
	void StartThread();

	/** Process a fully received UDP datagram */
	void ProcessReceivedData(const TArray<uint8>& RawData);

	/** Update or create the skeleton static data for a subject. Returns true if static data was pushed (skeleton changed). */
	bool UpdateStaticData(const FMOVINDatagram& Datagram, const FName& SubjectName, bool bForceRebuild = false);

private:

	ILiveLinkClient* Client = nullptr;
	FGuid SourceGuid;

	/** UDP port to listen on */
	int32 Port;

	/** The network socket */
	FSocket* Socket = nullptr;

	/** Socket subsystem reference */
	ISocketSubsystem* SocketSubsystem = nullptr;

	/** The receiver thread */
	FRunnableThread* Thread = nullptr;

	/** Flag indicating the thread should stop */
	FThreadSafeBool bStopping;

	/** Time to wait for incoming data before looping */
	FTimespan WaitTime;

	/** Source status text displayed in the LiveLink UI */
	FText SourceStatus;

	/** Track bone counts per subject to detect when static data needs to be re-pushed */
	TMap<FName, int32> SubjectBoneCounts;

	/** Track bone names per subject for change detection */
	TMap<FName, TArray<FName>> SubjectBoneNames;

	/** Monotonic skeleton revision per subject to discard stale queued frame updates */
	TMap<FName, int32> SubjectSkeletonRevisions;

	/** Whether a subject is ready to accept animation frames after a skeleton refresh */
	TMap<FName, bool> SubjectSkeletonReady;

	/** Critical section for thread-safe access */
	FCriticalSection CriticalSection;
};
