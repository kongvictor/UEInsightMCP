// UEInsightMCP — Adapted from UEEditorMCP/MCPServer.cpp

#include "InsightServer.h"
#include "InsightBridge.h"
#include "Actions/InsightAction.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

// =============================================================================
// FInsightClientHandler - per-client thread
// =============================================================================

FInsightClientHandler::FInsightClientHandler(FSocket* InClientSocket, UInsightBridge* InBridge, TAtomic<bool>& InServerStopping)
	: ClientSocket(InClientSocket)
	, Bridge(InBridge)
	, Thread(nullptr)
	, bServerStopping(InServerStopping)
	, bShouldStop(false)
	, bIsFinished(false)
{
	Thread = FRunnableThread::Create(this, TEXT("UEInsightMCP Client Handler"));
}

FInsightClientHandler::~FInsightClientHandler()
{
	bShouldStop = true;

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (ClientSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ClientSocket);
		}
		ClientSocket = nullptr;
	}
}

uint32 FInsightClientHandler::Run()
{
	ClientSocket->SetNonBlocking(false);
	ClientSocket->SetNoDelay(true);

	float LastActivityTime = FPlatformTime::Seconds();

	while (!bShouldStop && !bServerStopping)
	{
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - LastActivityTime > ConnectionTimeout)
		{
			UE_LOG(LogInsightMCP, Warning, TEXT("UEInsightMCP: Client connection timed out"));
			break;
		}

		if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(0.5)))
		{
			continue;
		}

		FString Message;
		if (!ReceiveMessage(Message))
		{
			UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Client disconnected or receive failed"));
			break;
		}

		LastActivityTime = FPlatformTime::Seconds();

		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Invalid JSON\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		FString CommandType;
		if (!JsonObj->TryGetStringField(TEXT("type"), CommandType))
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Missing 'type' field\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		if (CommandType == TEXT("ping"))
		{
			SendResponse(HandlePing());
			continue;
		}

		if (CommandType == TEXT("close"))
		{
			HandleClose();
			break;
		}

		if (CommandType == TEXT("get_context"))
		{
			SendResponse(HandleGetContext());
			continue;
		}

		TSharedPtr<FJsonObject> Params;
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			Params = *ParamsPtr;
		}
		else
		{
			Params = MakeShared<FJsonObject>();
		}

		// Offline analysis actions (session.*/query.*) run directly on this TCP thread.
		// Only trace control actions need the game thread.
		if (Bridge && !Bridge->ShouldRunOnGameThread(CommandType))
		{
			FString Response = ExecuteDirectly(CommandType, Params);
			SendResponse(Response);
		}
		else
		{
			FString Response = ExecuteOnGameThread(CommandType, Params);
			SendResponse(Response);
		}
	}

	bIsFinished = true;
	return 0;
}

void FInsightClientHandler::Exit()
{
	bIsFinished = true;
}

bool FInsightClientHandler::ReceiveMessage(FString& OutMessage)
{
	uint8 LengthBytes[4];
	int32 BytesRead = 0;
	if (!ClientSocket->Recv(LengthBytes, 4, BytesRead) || BytesRead != 4)
	{
		return false;
	}

	int32 Length = (LengthBytes[0] << 24) | (LengthBytes[1] << 16) | (LengthBytes[2] << 8) | LengthBytes[3];

	if (Length <= 0 || Length > RecvBufferSize)
	{
		UE_LOG(LogInsightMCP, Warning, TEXT("UEInsightMCP: Invalid message length: %d"), Length);
		return false;
	}

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(Length);

	int32 TotalReceived = 0;
	while (TotalReceived < Length)
	{
		int32 Received = 0;
		if (!ClientSocket->Recv(Buffer.GetData() + TotalReceived, Length - TotalReceived, Received) || Received <= 0)
		{
			return false;
		}
		TotalReceived += Received;
	}

	OutMessage = FString(Length, UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
	return true;
}

bool FInsightClientHandler::SendResponse(const FString& Response)
{
	FTCHARToUTF8 Converter(*Response);
	int32 Length = Converter.Length();

	uint8 LengthBytes[4] = {
		static_cast<uint8>((Length >> 24) & 0xFF),
		static_cast<uint8>((Length >> 16) & 0xFF),
		static_cast<uint8>((Length >> 8) & 0xFF),
		static_cast<uint8>(Length & 0xFF)
	};

	int32 BytesSent = 0;
	if (!ClientSocket->Send(LengthBytes, 4, BytesSent) || BytesSent != 4)
	{
		return false;
	}

	int32 TotalSent = 0;
	while (TotalSent < Length)
	{
		int32 Sent = 0;
		if (!ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()) + TotalSent, Length - TotalSent, Sent) || Sent <= 0)
		{
			return false;
		}
		TotalSent += Sent;
	}

	return true;
}

FString FInsightClientHandler::HandlePing()
{
	return TEXT("{\"status\":\"success\",\"result\":{\"pong\":true}}");
}

void FInsightClientHandler::HandleClose()
{
	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Client requested disconnect"));
	SendResponse(TEXT("{\"status\":\"success\",\"result\":{\"closed\":true}}"));
}

FString FInsightClientHandler::HandleGetContext()
{
	FString Result;
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);

	AsyncTask(ENamedThreads::GameThread, [this, &Result, DoneEvent]()
	{
		struct FTriggerOnExit
		{
			FEvent* Event;
			~FTriggerOnExit() { Event->Trigger(); }
		} TriggerGuard{DoneEvent};

		if (!Bridge)
		{
			Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		try
		{
			TSharedPtr<FJsonObject> ContextJson = Bridge->GetContext().ToJson();

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			Response->SetObjectField(TEXT("result"), ContextJson);

			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			Result = ResponseStr;
		}
		catch (...)
		{
			UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Exception in HandleGetContext"));
			Result = TEXT("{\"status\":\"error\",\"error\":\"Exception during get_context\"}");
		}
	});

	DoneEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return Result;
}

FString FInsightClientHandler::ExecuteOnGameThread(const FString& CommandType, TSharedPtr<FJsonObject> Params)
{
	auto Result = MakeShared<FString>();
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
	auto bCallerWaiting = MakeShared<TAtomic<bool>>(true);

	AsyncTask(ENamedThreads::GameThread, [this, CmdType = CommandType, Params, Result, DoneEvent, bCallerWaiting]()
	{
		struct FCleanupGuard
		{
			FEvent* Event;
			TSharedPtr<TAtomic<bool>> CallerWaiting;
			~FCleanupGuard()
			{
				Event->Trigger();
				if (!CallerWaiting->Load())
				{
					FPlatformProcess::ReturnSynchEventToPool(Event);
				}
			}
		} Guard{DoneEvent, bCallerWaiting};

		if (!Bridge)
		{
			*Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		TSharedPtr<FJsonObject> Response;
		try
		{
			Response = Bridge->ExecuteCommandSafe(CmdType, Params);
		}
		catch (...)
		{
			UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Unhandled exception in ExecuteCommandSafe for '%s'"), *CmdType);
		}

		if (Response.IsValid())
		{
			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			*Result = ResponseStr;
		}
		else
		{
			*Result = FString::Printf(
				TEXT("{\"status\":\"error\",\"error\":\"Command '%s' returned null response\"}"),
				*CmdType);
		}
	});

	static constexpr uint32 GameThreadTimeoutMs = 240000;
	if (!DoneEvent->Wait(GameThreadTimeoutMs))
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Game thread execution timed out after %ds for command '%s'"),
			GameThreadTimeoutMs / 1000, *CommandType);
		bCallerWaiting->Store(false);
		return FString::Printf(
			TEXT("{\"status\":\"error\",\"error\":\"Game thread execution timed out after %ds for command: %s\"}"),
			GameThreadTimeoutMs / 1000, *CommandType);
	}

	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return MoveTemp(*Result);
}

FString FInsightClientHandler::ExecuteDirectly(const FString& CommandType, TSharedPtr<FJsonObject> Params)
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
	}

	TSharedPtr<FJsonObject> Response;
	try
	{
		Response = Bridge->ExecuteCommandDirect(CommandType, Params);
	}
	catch (...)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Unhandled exception in ExecuteDirectly for '%s'"), *CommandType);
	}

	if (Response.IsValid())
	{
		FString ResponseStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
		FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
		return ResponseStr;
	}

	return FString::Printf(
		TEXT("{\"status\":\"error\",\"error\":\"Command '%s' returned null response\"}"),
		*CommandType);
}

// =============================================================================
// FInsightServer - accept loop
// =============================================================================

FInsightServer::FInsightServer(UInsightBridge* InBridge, int32 InPort)
	: Bridge(InBridge)
	, ListenerSocket(nullptr)
	, Port(InPort)
	, Thread(nullptr)
	, bShouldStop(false)
	, bIsRunning(false)
	, bIsStopping(false)
{
}

FInsightServer::~FInsightServer()
{
	Stop();
}

bool FInsightServer::Start()
{
	if (bIsRunning)
	{
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to get socket subsystem"));
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UEInsightMCP Listener"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to create listener socket"));
		return false;
	}

	ListenerSocket->SetReuseAddr(true);

	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
	Addr->SetPort(Port);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to bind to port %d"), Port);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	if (!ListenerSocket->Listen(MaxClients))
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	bShouldStop = false;
	Thread = FRunnableThread::Create(this, TEXT("UEInsightMCP Server Thread"));
	if (!Thread)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to create server thread"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Server started on port %d (max %d clients)"), Port, MaxClients);
	return true;
}

void FInsightServer::Stop()
{
	if (bIsStopping)
	{
		return;
	}

	bIsStopping = true;
	bShouldStop = true;

	if (ListenerSocket)
	{
		ListenerSocket->Close();
	}

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	{
		FScopeLock Lock(&HandlersLock);
		for (FInsightClientHandler* Handler : ClientHandlers)
		{
			Handler->RequestStop();
			delete Handler;
		}
		ClientHandlers.Empty();
	}

	if (ListenerSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}

	bIsRunning = false;
	bIsStopping = false;
	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Server stopped"));
}

bool FInsightServer::Init()
{
	return true;
}

uint32 FInsightServer::Run()
{
	bIsRunning = true;

	while (!bShouldStop)
	{
		CleanupFinishedHandlers();

		bool bPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bPendingConnection, FTimespan::FromSeconds(0.5)))
		{
			if (bPendingConnection)
			{
				FSocket* ClientSocket = ListenerSocket->Accept(TEXT("UEInsightMCP Client"));
				if (ClientSocket)
				{
					FScopeLock Lock(&HandlersLock);

					if (ClientHandlers.Num() >= MaxClients)
					{
						UE_LOG(LogInsightMCP, Warning, TEXT("UEInsightMCP: Max clients (%d) reached, rejecting connection"), MaxClients);
						ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
						if (SocketSubsystem)
						{
							SocketSubsystem->DestroySocket(ClientSocket);
						}
						continue;
					}

					UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Client connected (total: %d)"), ClientHandlers.Num() + 1);

					FInsightClientHandler* Handler = new FInsightClientHandler(ClientSocket, Bridge, bShouldStop);
					ClientHandlers.Add(Handler);
				}
			}
		}
	}

	bIsRunning = false;
	return 0;
}

void FInsightServer::Exit()
{
	bIsRunning = false;
}

void FInsightServer::CleanupFinishedHandlers()
{
	FScopeLock Lock(&HandlersLock);

	for (int32 i = ClientHandlers.Num() - 1; i >= 0; --i)
	{
		if (ClientHandlers[i]->IsFinished())
		{
			UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Cleaning up finished client handler (remaining: %d)"), ClientHandlers.Num() - 1);
			delete ClientHandlers[i];
			ClientHandlers.RemoveAt(i);
		}
	}
}
