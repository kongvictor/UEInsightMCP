// UEInsightMCP — InsightBridge implementation

#include "InsightBridge.h"
#include "InsightServer.h"
#include "Actions/InsightAction.h"
#include "Actions/TraceControlActions.h"
#include "Actions/TraceSessionActions.h"
#include "Actions/TraceQueryActions.h"
#include "Editor.h"

UInsightBridge::UInsightBridge()
	: Server(nullptr)
{
}

void UInsightBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Bridge initializing"));

	Context.RefreshTraceStateFromNative();
	RegisterActions();

	Server = new FInsightServer(this, DefaultPort);
	if (Server->Start())
	{
		UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Server started on port %d"), DefaultPort);
	}
	else
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Failed to start server"));
	}
}

void UInsightBridge::Deinitialize()
{
	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Bridge deinitializing"));

	if (Server)
	{
		Server->Stop();
		delete Server;
		Server = nullptr;
	}

	ActionHandlers.Empty();

	Super::Deinitialize();
}

void UInsightBridge::RegisterActions()
{
	// =========================================================================
	// Phase 1: Trace Control Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("trace.start"), MakeShared<FTraceStartAction>());
	ActionHandlers.Add(TEXT("trace.stop"), MakeShared<FTraceStopAction>());
	ActionHandlers.Add(TEXT("trace.status"), MakeShared<FTraceStatusAction>());
	ActionHandlers.Add(TEXT("trace.channels.list"), MakeShared<FTraceChannelsListAction>());
	ActionHandlers.Add(TEXT("trace.channels.toggle"), MakeShared<FTraceChannelsToggleAction>());
	ActionHandlers.Add(TEXT("trace.bookmark.add"), MakeShared<FTraceBookmarkAction>());

	// =========================================================================
	// Utility Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("batch_execute"), MakeShared<FBatchExecuteInsightAction>());

	// =========================================================================
	// Phase 2: Session Management Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("session.list"), MakeShared<FSessionListAction>());
	ActionHandlers.Add(TEXT("session.open"), MakeShared<FSessionOpenAction>());
	ActionHandlers.Add(TEXT("session.info"), MakeShared<FSessionInfoAction>());
	ActionHandlers.Add(TEXT("session.close"), MakeShared<FSessionCloseAction>());

	// =========================================================================
	// Phase 2: Data Query Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("query.frame_times"), MakeShared<FQueryFrameTimesAction>());
	ActionHandlers.Add(TEXT("query.cpu_threads"), MakeShared<FQueryCpuThreadsAction>());
	ActionHandlers.Add(TEXT("query.gpu_timing"), MakeShared<FQueryGpuTimingAction>());
	ActionHandlers.Add(TEXT("query.loadtime"), MakeShared<FQueryLoadTimeAction>());
	ActionHandlers.Add(TEXT("query.loadtime_deps"), MakeShared<FQueryLoadTimeDepsAction>());
	ActionHandlers.Add(TEXT("query.memory"), MakeShared<FQueryMemoryAction>());
	ActionHandlers.Add(TEXT("query.counters"), MakeShared<FQueryCountersAction>());
	ActionHandlers.Add(TEXT("query.bookmarks"), MakeShared<FQueryBookmarksAction>());
	ActionHandlers.Add(TEXT("query.hitches"), MakeShared<FQueryHitchesAction>());

	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Registered %d action handlers"), ActionHandlers.Num());
}

TSharedRef<FInsightAction>* UInsightBridge::FindAction(const FString& CommandType)
{
	return ActionHandlers.Find(CommandType);
}

bool UInsightBridge::ShouldRunOnGameThread(const FString& CommandType) const
{
	const TSharedRef<FInsightAction>* ActionPtr = const_cast<UInsightBridge*>(this)->ActionHandlers.Find(CommandType);
	if (ActionPtr)
	{
		return (*ActionPtr)->RequiresGameThread();
	}
	return true; // Unknown commands default to game thread (safe)
}

TSharedPtr<FJsonObject> UInsightBridge::ExecuteCommandDirect(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	return ExecuteCommandSafe(CommandType, Params);
}

TSharedPtr<FJsonObject> UInsightBridge::ExecuteCommand(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	TSharedRef<FInsightAction>* ActionPtr = FindAction(CommandType);
	if (ActionPtr)
	{
		return (*ActionPtr)->Execute(Params, Context);
	}

	return CreateErrorResponse(
		FString::Printf(TEXT("Unknown command type: %s"), *CommandType),
		TEXT("unknown_command")
	);
}

TSharedPtr<FJsonObject> UInsightBridge::ExecuteCommandSafe(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	try
	{
		return ExecuteCommandInternal(CommandType, Params);
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("C++ exception in command '%s': %hs"), *CommandType, Ex.what());
		return CreateErrorResponse(
			FString::Printf(TEXT("C++ exception: %hs"), Ex.what()),
			TEXT("cpp_exception")
		);
	}
	catch (...)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("Unknown C++ exception in command '%s'"), *CommandType);
		return CreateErrorResponse(TEXT("Unknown C++ exception"), TEXT("cpp_exception"));
	}
}

TSharedPtr<FJsonObject> UInsightBridge::ExecuteCommandInternal(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	return ExecuteCommand(CommandType, Params);
}

TSharedPtr<FJsonObject> UInsightBridge::CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("success"));
	Response->SetBoolField(TEXT("success"), true);

	if (ResultData.IsValid())
	{
		Response->SetObjectField(TEXT("result"), ResultData);
	}
	else
	{
		Response->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
	}

	return Response;
}

TSharedPtr<FJsonObject> UInsightBridge::CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("error"));
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	Response->SetStringField(TEXT("error_type"), ErrorType);

	return Response;
}
