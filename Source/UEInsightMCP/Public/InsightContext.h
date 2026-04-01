// UEInsightMCP — InsightContext: Trace session state

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

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
	bool bIsTracing = false;                        // Whether Trace is currently active
	FString ActiveTraceFile;                         // Current .utrace output file path
	TSet<FString> EnabledChannels;                   // Currently enabled Trace channels
	double TraceStartTime = 0.0;                     // FPlatformTime::Seconds() when Trace started

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
	TSharedPtr<FJsonObject> ToJson() const;

	/** Get or create the analysis service singleton */
	TSharedPtr<TraceServices::IAnalysisService> GetOrCreateAnalysisService();

	/** Default Trace output directory: {ProjectDir}/Saved/TraceSessions/ */
	static FString GetDefaultTraceDir();
};
