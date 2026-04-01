// UEInsightMCP — InsightBridge: command router

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "InsightContext.h"
#include "InsightBridge.generated.h"

class FInsightServer;
class FInsightAction;

/**
 * UInsightBridge
 *
 * Editor subsystem that manages the Insight TCP server and routes
 * commands to appropriate Trace action handlers.
 */
UCLASS()
class UEINSIGHTMCP_API UInsightBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UInsightBridge();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Execute a command received from the MCP server.
	 * Routes to appropriate action handler based on command type.
	 */
	TSharedPtr<FJsonObject> ExecuteCommand(
		const FString& CommandType,
		const TSharedPtr<FJsonObject>& Params);

	/**
	 * Execute with crash protection (SEH + C++ exception guard).
	 */
	TSharedPtr<FJsonObject> ExecuteCommandSafe(
		const FString& CommandType,
		const TSharedPtr<FJsonObject>& Params);

	/** Get the current Trace context */
	FInsightContext& GetContext() { return Context; }
	const FInsightContext& GetContext() const { return Context; }

	/**
	 * Check if a command should run on the game thread.
	 * Returns true for trace control commands, false for offline analysis (session / query).
	 * Returns true for unknown commands (safe default).
	 */
	bool ShouldRunOnGameThread(const FString& CommandType) const;

	/**
	 * Execute a command directly on the calling thread (no game-thread dispatch).
	 * Only safe for actions whose RequiresGameThread() returns false.
	 * Thread-safety: callers must ensure no concurrent writes to Context.
	 */
	TSharedPtr<FJsonObject> ExecuteCommandDirect(
		const FString& CommandType,
		const TSharedPtr<FJsonObject>& Params);

	/** Create a success response */
	static TSharedPtr<FJsonObject> CreateSuccessResponse(
		const TSharedPtr<FJsonObject>& ResultData = nullptr);

	/** Create an error response */
	static TSharedPtr<FJsonObject> CreateErrorResponse(
		const FString& ErrorMessage,
		const FString& ErrorType = TEXT("error"));

private:
	void RegisterActions();
	TSharedRef<FInsightAction>* FindAction(const FString& CommandType);
	TSharedPtr<FJsonObject> ExecuteCommandInternal(
		const FString& CommandType,
		const TSharedPtr<FJsonObject>& Params);

	FInsightServer* Server;
	FInsightContext Context;
	TMap<FString, TSharedRef<FInsightAction>> ActionHandlers;

	static constexpr int32 DefaultPort = 55559;
};
