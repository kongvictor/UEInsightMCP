// UEInsightMCP — InsightAction implementation

#include "Actions/InsightAction.h"

#if PLATFORM_WINDOWS && defined(_MSC_VER)
#include "Windows/WindowsHWrapper.h"
#endif

// ============================================================================
// FInsightAction Implementation
// ============================================================================

TSharedPtr<FJsonObject> FInsightAction::Execute(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	FString Error;

	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Action '%s' Execute started"), *GetActionName());

	// Step 1: Pre-validation
	if (!Validate(Params, Context, Error))
	{
		UE_LOG(LogInsightMCP, Warning, TEXT("UEInsightMCP: Action '%s' validation failed: %s"), *GetActionName(), *Error);
		return CreateErrorResponse(Error, TEXT("validation_failed"));
	}

	// Step 2: Execute with crash protection
	TSharedPtr<FJsonObject> Result = ExecuteWithCrashProtection(Params, Context);
	if (!Result)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("UEInsightMCP: Action '%s' returned nullptr!"), *GetActionName());
		return CreateCrashPreventedResponse();
	}

	// Step 3: Post-validation
	if (!PostValidate(Context, Error))
	{
		UE_LOG(LogInsightMCP, Warning, TEXT("UEInsightMCP: Action '%s' post-validation failed: %s"), *GetActionName(), *Error);
		return CreateErrorResponse(Error, TEXT("post_validation_failed"));
	}

	return Result;
}

#if PLATFORM_WINDOWS && defined(_MSC_VER)
struct FInsightSEHCallContext
{
	FInsightAction* Action;
	const TSharedPtr<FJsonObject>* Params;
	FInsightContext* Context;
	TSharedPtr<FJsonObject>* OutResult;
};

static void InsightSEH_InvokeInternal(void* RawCtx)
{
	FInsightSEHCallContext* Ctx = static_cast<FInsightSEHCallContext*>(RawCtx);
	*Ctx->OutResult = Ctx->Action->ExecuteInternal(*Ctx->Params, *Ctx->Context);
}

#pragma warning(push)
#pragma warning(disable: 4611)
static DWORD InsightSEH_TryCall(void (*Func)(void*), void* UserData)
{
	__try
	{
		Func(UserData);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}
}
#pragma warning(pop)
#endif

TSharedPtr<FJsonObject> FInsightAction::ExecuteWithCrashProtection(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if PLATFORM_WINDOWS && defined(_MSC_VER)
	TSharedPtr<FJsonObject> OutResult;
	FInsightSEHCallContext CallCtx = { this, &Params, &Context, &OutResult };

	DWORD ExCode = InsightSEH_TryCall(&InsightSEH_InvokeInternal, &CallCtx);
	if (ExCode != 0)
	{
		UE_LOG(LogInsightMCP, Error, TEXT("SEH exception 0x%08X in action '%s'"), ExCode, *GetActionName());
		return CreateCrashPreventedResponse();
	}
	return OutResult;
#else
	return ExecuteInternal(Params, Context);
#endif
}

// ============================================================================
// Response Helpers
// ============================================================================

TSharedPtr<FJsonObject> FInsightAction::CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData) const
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);

	if (ResultData.IsValid())
	{
		for (const auto& Field : ResultData->Values)
		{
			Response->SetField(Field.Key, Field.Value);
		}
	}

	return Response;
}

TSharedPtr<FJsonObject> FInsightAction::CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType) const
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	Response->SetStringField(TEXT("error_type"), ErrorType);

	return Response;
}

TSharedPtr<FJsonObject> FInsightAction::CreateCrashPreventedResponse() const
{
	return CreateErrorResponse(
		FString::Printf(TEXT("CRASH PREVENTED: Access violation in '%s'. Operation aborted safely."), *GetActionName()),
		TEXT("crash_prevented")
	);
}

// ============================================================================
// Parameter Helpers
// ============================================================================

bool FInsightAction::GetRequiredString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, FString& OutValue, FString& OutError) const
{
	if (!Params.IsValid() || !Params->TryGetStringField(ParamName, OutValue) || OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Required parameter '%s' is missing or empty"), *ParamName);
		return false;
	}
	return true;
}

FString FInsightAction::GetOptionalString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, const FString& Default) const
{
	FString Value;
	if (Params.IsValid() && Params->TryGetStringField(ParamName, Value) && !Value.IsEmpty())
	{
		return Value;
	}
	return Default;
}

const TArray<TSharedPtr<FJsonValue>>* FInsightAction::GetOptionalArray(const TSharedPtr<FJsonObject>& Params, const FString& ParamName) const
{
	if (Params.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* OutArray = nullptr;
		if (Params->TryGetArrayField(ParamName, OutArray))
		{
			return OutArray;
		}
	}
	return nullptr;
}

double FInsightAction::GetOptionalNumber(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, double Default) const
{
	double Value = Default;
	if (Params.IsValid() && Params->TryGetNumberField(ParamName, Value))
	{
		return Value;
	}
	return Default;
}

bool FInsightAction::GetOptionalBool(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, bool Default) const
{
	bool Value = Default;
	if (Params.IsValid() && Params->TryGetBoolField(ParamName, Value))
	{
		return Value;
	}
	return Default;
}
