// UEInsightMCP — Adapted from UEEditorMCP/MCPLogCapture.h

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

// Forward declaration of log category
DECLARE_LOG_CATEGORY_EXTERN(LogInsightMCP, Log, All);

/**
 * FInsightLogCapture
 *
 * Custom FOutputDevice that hooks into GLog to capture editor log output.
 * Maintains a ring buffer of recent log entries for the Insight MCP server.
 * Thread-safe.
 */
class UEINSIGHTMCP_API FInsightLogCapture : public FOutputDevice
{
public:
	static FInsightLogCapture& Get();

	void Start();
	void Stop();
	bool IsCapturing() const { return bCapturing; }

	struct FLogEntry
	{
		uint64 Seq = 0;
		FDateTime TimestampUtc;
		double Timestamp = 0.0;
		FName Category;
		ELogVerbosity::Type Verbosity;
		FString Message;
		int32 MessageBytes = 0;
	};

	TArray<FLogEntry> GetRecent(
		int32 Count = 100,
		const FString& CategoryFilter = TEXT(""),
		ELogVerbosity::Type MinVerbosity = ELogVerbosity::All) const;

	TArray<FLogEntry> GetSince(
		uint64 AfterSeq,
		int32 MaxLines,
		int32 MaxBytes,
		const TArray<FString>& CategoryFilters,
		ELogVerbosity::Type MinVerbosity,
		const FString& ContainsFilter,
		bool& OutTruncated,
		uint64& OutLastSeq) const;

	uint64 GetLatestSeq() const;
	FDateTime GetLastReceivedUtc() const;
	bool HasRecentData(double RecentWindowSeconds) const;
	void Clear();
	int64 GetTotalCaptured() const { return TotalCaptured; }

protected:
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
	FInsightLogCapture();
	~FInsightLogCapture();

	mutable FCriticalSection Lock;
	TArray<FLogEntry> RingBuffer;
	int32 HeadIndex = 0;
	int32 EntryCount = 0;
	int64 TotalBytes = 0;
	bool bCapturing = false;
	int64 TotalCaptured = 0;
	uint64 NextSeq = 1;
	FDateTime LastReceivedUtc;

	static constexpr int32 BufferCapacity = 10000;
	static constexpr int64 MaxBufferBytes = 5 * 1024 * 1024;
};
