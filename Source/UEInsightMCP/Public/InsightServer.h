// UEInsightMCP — Adapted from UEEditorMCP/MCPServer.h

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

class UInsightBridge;

/**
 * FInsightClientHandler
 *
 * Handles a single persistent client connection on its own thread.
 * Created by FInsightServer when a new client connects.
 */
class FInsightClientHandler : public FRunnable
{
public:
	FInsightClientHandler(FSocket* InClientSocket, UInsightBridge* InBridge, TAtomic<bool>& InServerStopping);
	virtual ~FInsightClientHandler();

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Exit() override;

	void RequestStop() { bShouldStop = true; }
	bool IsFinished() const { return bIsFinished; }

private:
	bool ReceiveMessage(FString& OutMessage);
	bool SendResponse(const FString& Response);
	FString HandlePing();
	void HandleClose();
	FString HandleGetContext();
	FString ExecuteOnGameThread(const FString& CommandType, TSharedPtr<FJsonObject> Params);

	/** Execute action directly on calling thread (for offline analysis actions) */
	FString ExecuteDirectly(const FString& CommandType, TSharedPtr<FJsonObject> Params);

	FSocket* ClientSocket;
	UInsightBridge* Bridge;
	FRunnableThread* Thread;
	TAtomic<bool>& bServerStopping;
	TAtomic<bool> bShouldStop;
	TAtomic<bool> bIsFinished;

	static constexpr float ConnectionTimeout = 300.0f;
	static constexpr int32 RecvBufferSize = 1024 * 1024;  // 1MB
};


/**
 * FInsightServer
 *
 * TCP server for UEInsightMCP. Accepts connections from the Python MCP
 * bridge and routes commands to InsightBridge.
 *
 * Port: 55559 (UEEditorMCP uses 55558)
 */
class UEINSIGHTMCP_API FInsightServer : public FRunnable
{
public:
	FInsightServer(UInsightBridge* InBridge, int32 InPort = 55559);
	virtual ~FInsightServer();

	bool Start();
	void Stop();
	bool IsRunning() const { return bIsRunning; }

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;

private:
	void CleanupFinishedHandlers();

	UInsightBridge* Bridge;
	FSocket* ListenerSocket;
	int32 Port;
	FRunnableThread* Thread;
	TArray<FInsightClientHandler*> ClientHandlers;
	FCriticalSection HandlersLock;
	TAtomic<bool> bShouldStop;
	TAtomic<bool> bIsRunning;
	TAtomic<bool> bIsStopping;

	static constexpr int32 MaxClients = 8;
};
