// UEInsightMCP — InsightContext: Trace session state

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

// Forward declarations for TraceServices
namespace TraceServices
{
	class IAnalysisSession;
	class IAnalysisService;
}

/**
 * FInsightContext
 *
 * Maintains the current Trace session state. Analogous to UEEditorMCP's
 * FMCPEditorContext, but tracks Trace state instead of Blueprint editing state.
 */
struct UEINSIGHTMCP_API FInsightContext
{
	// ── Trace Control State ──
	bool bIsTracing = false;                        // Native Trace connection is active
	bool bTraceStartedByMCP = false;                // Current connection was started by this MCP bridge
	bool bTraceStartTimeKnown = false;              // TraceStartTime is valid only for MCP-owned traces
	FTraceAuxiliary::EConnectionType ActiveConnectionType = FTraceAuxiliary::EConnectionType::None;
	FString ActiveTraceDestination;                 // Native file path or Trace Server host
	FString ActiveTraceFile;                        // Compatibility field; set only for file traces
	TSet<FString> EnabledChannels;                  // Native active Trace channels
	double TraceStartTime = 0.0;                    // FPlatformTime::Seconds() for MCP-owned traces

	// Connection displaced by an MCP capture. Network connections can be restored on stop.
	bool bHasPreviousTrace = false;
	FTraceAuxiliary::EConnectionType PreviousConnectionType = FTraceAuxiliary::EConnectionType::None;
	FString PreviousTraceDestination;
	FString PreviousTraceChannels;

	// ── Analysis Session State (Phase 2) ──
	FString CurrentSessionFile;                      // Currently opened .utrace file for analysis
	bool bSessionLoaded = false;                     // Whether an analysis session is loaded
	TSharedPtr<const TraceServices::IAnalysisSession> AnalysisSession;  // Active analysis session
	TSharedPtr<TraceServices::IAnalysisService> AnalysisService;        // Cached analysis service

	// ── Cached Analysis Results ──
	TSharedPtr<FJsonObject> CachedFrameTimeSummary;  // Frame time summary cache
	TSharedPtr<FJsonObject> CachedLoadTimeSummary;   // Load time summary cache
	double CacheTimestamp = 0.0;                     // When cache was populated

	// ── Methods ──
	void Reset();
	void ResetSession();                             // Close session only (keep trace state)
	void InvalidateCache();
	void RefreshTraceStateFromNative();
	void MarkMCPTraceStarted(FTraceAuxiliary::EConnectionType Type, const FString& Destination);
	void ClearActiveTraceState();
	void ClearPreviousTraceState();
	TSharedPtr<FJsonObject> ToJson() const;

	/** Get or create the analysis service singleton */
	TSharedPtr<TraceServices::IAnalysisService> GetOrCreateAnalysisService();

	/** Stable string representation used by MCP responses. */
	static FString ConnectionTypeToString(FTraceAuxiliary::EConnectionType Type);

	/** Default Trace output directory: absolute {ProjectDir}/Saved/TraceSessions/. */
	static FString GetDefaultTraceDir();
};
