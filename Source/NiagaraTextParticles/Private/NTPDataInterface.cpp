// Property of Lucian Tranc

#include "NTPDataInterface.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraShaderParametersBuilder.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "NiagaraDataInterfaceUtilities.h"
#include "RHI.h"
#include "VectorVM.h"

DEFINE_LOG_CATEGORY(LogNiagaraTextParticles);

#define LOCTEXT_NAMESPACE "NTPDataInterface"

static const TCHAR* FontUVTemplateShaderFile = TEXT("/Plugin/NiagaraTextParticles/Private/NTPDataInterface.ush");

static bool IsWhitespaceChar(int32 Code)
{
	return Code == ' '
		|| Code == '\t'
		|| Code == '\n'
		|| Code == '\r';
}

const FName UNTPDataInterface::GetCharacterUVName(TEXT("GetCharacterUV"));
const FName UNTPDataInterface::GetCharacterPositionName(TEXT("GetCharacterPosition"));
const FName UNTPDataInterface::GetTextCharacterCountName(TEXT("GetTextCharacterCount"));
const FName UNTPDataInterface::GetTextLineCountName(TEXT("GetTextLineCount"));
const FName UNTPDataInterface::GetLineCharacterCountName(TEXT("GetLineCharacterCount"));
const FName UNTPDataInterface::GetTextWordCountName(TEXT("GetTextWordCount"));
const FName UNTPDataInterface::GetWordCharacterCountName(TEXT("GetWordCharacterCount"));

// The struct used to store our data interface data
struct FNDIFontUVInfoInstanceData
{
	TArray<FVector4> UVRects;
	TArray<int32> Unicode;
	TArray<FVector2f> CharacterPositions;
	TArray<int32> LineStartIndices;
	TArray<int32> LineCharacterCounts;
	TArray<int32> WordStartIndices;
	TArray<int32> WordCharacterCounts;
};

// This proxy is used to safely copy data between game thread and render thread
struct FNDIFontUVInfoProxy : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIFontUVInfoInstanceData); }

	struct FRTInstanceData
	{
		FRWBufferStructured UVRectsBuffer;
		uint32 NumRects = 0;
		FRWBufferStructured UnicodeBuffer;
		FRWBufferStructured CharacterPositionsBuffer;
		FRWBufferStructured LineStartIndicesBuffer;
		FRWBufferStructured LineCharacterCountBuffer;
		FRWBufferStructured WordStartIndicesBuffer;
		FRWBufferStructured WordCharacterCountBuffer;
		uint32 NumChars = 0;
		uint32 NumLines = 0;
		uint32 NumWords = 0;

		void Release()
		{
			UVRectsBuffer.Release();
			UnicodeBuffer.Release();
			CharacterPositionsBuffer.Release();
			LineStartIndicesBuffer.Release();
			LineCharacterCountBuffer.Release();
			WordStartIndicesBuffer.Release();
			WordCharacterCountBuffer.Release();
			NumRects = 0;
			NumChars = 0;
			NumLines = 0;
			NumWords = 0;
		}
	};

	FRWBufferStructured DefaultUVRectsBuffer;
	FRWBufferStructured DefaultUIntBuffer;
	FRWBufferStructured DefaultFloatBuffer;
	bool bDefaultInitialized = false;

	void EnsureDefaultBuffer(FRHICommandListBase& RHICmdList)
	{
		if (!bDefaultInitialized)
		{
			DefaultUVRectsBuffer.Initialize(RHICmdList, TEXT("NTP_UVRects_Default"), sizeof(FVector4f), 1, BUF_ShaderResource | BUF_Static);
			const FVector4f Zero(0, 0, 0, 0);
			void* Dest = RHICmdList.LockBuffer(DefaultUVRectsBuffer.Buffer, 0, sizeof(FVector4f), RLM_WriteOnly);
			FMemory::Memcpy(Dest, &Zero, sizeof(FVector4f));
			RHICmdList.UnlockBuffer(DefaultUVRectsBuffer.Buffer);

			DefaultUIntBuffer.Initialize(RHICmdList, TEXT("NTP_UInt_Default"), sizeof(uint32), 1, BUF_ShaderResource | BUF_Static);
			uint32 ZeroU = 0;
			void* DestU = RHICmdList.LockBuffer(DefaultUIntBuffer.Buffer, 0, sizeof(uint32), RLM_WriteOnly);
			FMemory::Memcpy(DestU, &ZeroU, sizeof(uint32));
			RHICmdList.UnlockBuffer(DefaultUIntBuffer.Buffer);

			DefaultFloatBuffer.Initialize(RHICmdList, TEXT("NTP_Float2_Default"), sizeof(FVector2f), 1, BUF_ShaderResource | BUF_Static);
			const FVector2f ZeroF2(0.0f, 0.0f);
			void* DestF = RHICmdList.LockBuffer(DefaultFloatBuffer.Buffer, 0, sizeof(FVector2f), RLM_WriteOnly);
			FMemory::Memcpy(DestF, &ZeroF2, sizeof(FVector2f));
			RHICmdList.UnlockBuffer(DefaultFloatBuffer.Buffer);
			bDefaultInitialized = true;
		}
	}

	static void ProvidePerInstanceDataForRenderThread(void* InDataForRenderThread, void* InDataFromGameThread, const FNiagaraSystemInstanceID& SystemInstance)
	{
		// Initialize the render thread instance data into the pre-allocated memory
		FNDIFontUVInfoInstanceData* DataForRenderThread = new (InDataForRenderThread) FNDIFontUVInfoInstanceData();

		// Copy the game thread data
		const FNDIFontUVInfoInstanceData* DataFromGameThread = static_cast<const FNDIFontUVInfoInstanceData*>(InDataFromGameThread);
		*DataForRenderThread = *DataFromGameThread;

		UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): ProvidePerInstanceDataForRenderThread - InstanceID=%llu, UVRects.Num=%d"),
			(uint64)SystemInstance, DataForRenderThread->UVRects.Num());
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		FNDIFontUVInfoInstanceData* InstanceDataFromGT = static_cast<FNDIFontUVInfoInstanceData*>(PerInstanceData);
		FRTInstanceData& RTInstance = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// Upload / rebuild structured buffer on RT
		const int32 NumRects = InstanceDataFromGT->UVRects.Num();
		RTInstance.Release();
		const uint32 Stride = sizeof(FVector4f);
		const uint32 Count  = FMath::Max(NumRects, 1);
		RTInstance.NumRects = (uint32)NumRects;
		RTInstance.UVRectsBuffer.Initialize(RHICmdList, TEXT("NTP_UVRects"), Stride, Count, BUF_ShaderResource | BUF_Static);

		const uint32 NumBytes = Stride * Count;
		// Convert to float4 to match HLSL StructuredBuffer<float4>
		TArray<FVector4f> TempFloatRects;
		TempFloatRects.SetNumUninitialized(Count);
		if (NumRects > 0)
		{
			for (int32 i = 0; i < NumRects; ++i)
			{
				const FVector4& Src = InstanceDataFromGT->UVRects[i];
				TempFloatRects[i] = FVector4f((float)Src.X, (float)Src.Y, (float)Src.Z, (float)Src.W);
			}
		}
		else
		{
			TempFloatRects[0] = FVector4f(0, 0, 0, 0);
		}

		void* Dest = RHICmdList.LockBuffer(RTInstance.UVRectsBuffer.Buffer, 0, NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(Dest, TempFloatRects.GetData(), NumBytes);
		RHICmdList.UnlockBuffer(RTInstance.UVRectsBuffer.Buffer);

		// Upload Unicode buffer
		{
			const int32 NumChars = InstanceDataFromGT->Unicode.Num();
			RTInstance.NumChars = (uint32)NumChars;
			const uint32 UIntStride = sizeof(uint32);
			const uint32 UIntCount  = FMath::Max(NumChars, 1);
			RTInstance.UnicodeBuffer.Initialize(RHICmdList, TEXT("NTP_Unicode"), UIntStride, UIntCount, BUF_ShaderResource | BUF_Static);

			TArray<uint32> TempUInts;
			TempUInts.SetNumUninitialized(UIntCount);
			if (NumChars > 0)
			{
				for (int32 i = 0; i < NumChars; ++i)
				{
					TempUInts[i] = (uint32)InstanceDataFromGT->Unicode[i];
				}
			}
			else
			{
				TempUInts[0] = 0;
			}

			void* DestU = RHICmdList.LockBuffer(RTInstance.UnicodeBuffer.Buffer, 0, UIntStride * UIntCount, RLM_WriteOnly);
			FMemory::Memcpy(DestU, TempUInts.GetData(), UIntStride * UIntCount);
			RHICmdList.UnlockBuffer(RTInstance.UnicodeBuffer.Buffer);
		}

		// Upload character positions buffer
		{
			const int32 NumPositions = InstanceDataFromGT->CharacterPositions.Num();
			const uint32 FStride = sizeof(FVector2f);
			const uint32 FCount  = FMath::Max(NumPositions, 1);
			RTInstance.CharacterPositionsBuffer.Initialize(RHICmdList, TEXT("NTP_CharacterPositions"), FStride, FCount, BUF_ShaderResource | BUF_Static);

			TArray<FVector2f> TempVectors;
			TempVectors.SetNumUninitialized(FCount);
			if (NumPositions > 0)
			{
				FMemory::Memcpy(TempVectors.GetData(), InstanceDataFromGT->CharacterPositions.GetData(), sizeof(FVector2f) * NumPositions);
			}
			else
			{
				TempVectors[0] = FVector2f(0.0f, 0.0f);
			}

			void* DestF = RHICmdList.LockBuffer(RTInstance.CharacterPositionsBuffer.Buffer, 0, FStride * FCount, RLM_WriteOnly);
			FMemory::Memcpy(DestF, TempVectors.GetData(), FStride * FCount);
			RHICmdList.UnlockBuffer(RTInstance.CharacterPositionsBuffer.Buffer);
		}

		// Upload line start indices buffer
		{
			const int32 NumLineStartIndices = InstanceDataFromGT->LineStartIndices.Num();
			const uint32 LStride = sizeof(uint32);
			const uint32 LCount  = FMath::Max(NumLineStartIndices, 1);
			RTInstance.LineStartIndicesBuffer.Initialize(RHICmdList, TEXT("NTP_LineStartIndices"), LStride, LCount, BUF_ShaderResource | BUF_Static);

			TArray<uint32> TempLineStartIndices;
			TempLineStartIndices.SetNumUninitialized(LCount);
			if (NumLineStartIndices > 0)
			{
				for (int32 i = 0; i < NumLineStartIndices; ++i)
				{
					TempLineStartIndices[i] = (uint32)InstanceDataFromGT->LineStartIndices[i];
				}
			}
			else
			{
				TempLineStartIndices[0] = 0;
			}

			void* DestL = RHICmdList.LockBuffer(RTInstance.LineStartIndicesBuffer.Buffer, 0, LStride * LCount, RLM_WriteOnly);
			FMemory::Memcpy(DestL, TempLineStartIndices.GetData(), LStride * LCount);
			RHICmdList.UnlockBuffer(RTInstance.LineStartIndicesBuffer.Buffer);
		}

		// Upload per-line character counts buffer
		{
			const int32 NumLineCounts = InstanceDataFromGT->LineCharacterCounts.Num();
			const uint32 CStride = sizeof(uint32);
			const uint32 CCount  = FMath::Max(NumLineCounts, 1);
			RTInstance.LineCharacterCountBuffer.Initialize(RHICmdList, TEXT("NTP_LineCharacterCounts"), CStride, CCount, BUF_ShaderResource | BUF_Static);

			TArray<uint32> TempLineCounts;
			TempLineCounts.SetNumUninitialized(CCount);
			if (NumLineCounts > 0)
			{
				for (int32 i = 0; i < NumLineCounts; ++i)
				{
					TempLineCounts[i] = (uint32)InstanceDataFromGT->LineCharacterCounts[i];
				}
			}
			else
			{
				TempLineCounts[0] = 0;
			}

			void* DestC = RHICmdList.LockBuffer(RTInstance.LineCharacterCountBuffer.Buffer, 0, CStride * CCount, RLM_WriteOnly);
			FMemory::Memcpy(DestC, TempLineCounts.GetData(), CStride * CCount);
			RHICmdList.UnlockBuffer(RTInstance.LineCharacterCountBuffer.Buffer);
		}

		// Upload word start indices buffer
		{
			const int32 NumWordStartIndices = InstanceDataFromGT->WordStartIndices.Num();
			const uint32 WStride = sizeof(uint32);
			const uint32 WCount  = FMath::Max(NumWordStartIndices, 1);
			RTInstance.WordStartIndicesBuffer.Initialize(RHICmdList, TEXT("NTP_WordStartIndices"), WStride, WCount, BUF_ShaderResource | BUF_Static);

			TArray<uint32> TempWordStartIndices;
			TempWordStartIndices.SetNumUninitialized(WCount);
			if (NumWordStartIndices > 0)
			{
				for (int32 i = 0; i < NumWordStartIndices; ++i)
				{
					TempWordStartIndices[i] = (uint32)InstanceDataFromGT->WordStartIndices[i];
				}
			}
			else
			{
				TempWordStartIndices[0] = 0;
			}

			void* DestW = RHICmdList.LockBuffer(RTInstance.WordStartIndicesBuffer.Buffer, 0, WStride * WCount, RLM_WriteOnly);
			FMemory::Memcpy(DestW, TempWordStartIndices.GetData(), WStride * WCount);
			RHICmdList.UnlockBuffer(RTInstance.WordStartIndicesBuffer.Buffer);
		}

		// Upload per-word character counts buffer
		{
			const int32 NumWordCounts = InstanceDataFromGT->WordCharacterCounts.Num();
			const uint32 WCStride = sizeof(uint32);
			const uint32 WCCount  = FMath::Max(NumWordCounts, 1);
			RTInstance.WordCharacterCountBuffer.Initialize(RHICmdList, TEXT("NTP_WordCharacterCounts"), WCStride, WCCount, BUF_ShaderResource | BUF_Static);

			TArray<uint32> TempWordCounts;
			TempWordCounts.SetNumUninitialized(WCCount);
			if (NumWordCounts > 0)
			{
				for (int32 i = 0; i < NumWordCounts; ++i)
				{
					TempWordCounts[i] = (uint32)InstanceDataFromGT->WordCharacterCounts[i];
				}
			}
			else
			{
				TempWordCounts[0] = 0;
			}

			void* DestWC = RHICmdList.LockBuffer(RTInstance.WordCharacterCountBuffer.Buffer, 0, WCStride * WCCount, RLM_WriteOnly);
			FMemory::Memcpy(DestWC, TempWordCounts.GetData(), WCStride * WCCount);
			RHICmdList.UnlockBuffer(RTInstance.WordCharacterCountBuffer.Buffer);
		}

		// Copy line and word count
		RTInstance.NumChars = (uint32)InstanceDataFromGT->Unicode.Num();
		RTInstance.NumLines = (uint32)InstanceDataFromGT->LineStartIndices.Num();
		RTInstance.NumWords = (uint32)InstanceDataFromGT->WordStartIndices.Num();

		// Call the destructor to clean up the GT data
		InstanceDataFromGT->~FNDIFontUVInfoInstanceData();

		UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): ConsumePerInstanceDataFromGameThread - InstanceID=%llu, UVRects.Num=%u"),
			(uint64)InstanceID, RTInstance.NumRects);
	}

	TMap<FNiagaraSystemInstanceID, FRTInstanceData> SystemInstancesToInstanceData_RT;
};

// Creates a new data object to store our data
bool UNTPDataInterface::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIFontUVInfoInstanceData* InstanceData = new (PerInstanceData) FNDIFontUVInfoInstanceData;

	InstanceData->UVRects = GetUVRectsFromFont(FontAsset);

	TArray<FVector2f> CharacterPositionsUnfiltered = GetCharacterPositions(InstanceData->UVRects, InputText, HorizontalAlignment, VerticalAlignment);

	TArray<int32> OutUnicode;
	TArray<FVector2f> OutCharacterPositions;
	TArray<int32> OutLineStartIndices;
	TArray<int32> OutLineCharacterCounts;
	TArray<int32> OutWordStartIndices;
	TArray<int32> OutWordCharacterCounts;

	if (bSpawnWhitespaceCharacters)
	{
		ProcessTextWithWhitespace(InputText, CharacterPositionsUnfiltered, OutUnicode, OutCharacterPositions, OutLineStartIndices, OutLineCharacterCounts, OutWordStartIndices, OutWordCharacterCounts);
	}
	else
	{
		ProcessTextWithoutWhitespace(InputText, CharacterPositionsUnfiltered, OutUnicode, OutCharacterPositions, OutLineStartIndices, OutLineCharacterCounts, OutWordStartIndices, OutWordCharacterCounts);
	}

	InstanceData->Unicode = MoveTemp(OutUnicode);
	InstanceData->CharacterPositions = MoveTemp(OutCharacterPositions);
	InstanceData->LineStartIndices = MoveTemp(OutLineStartIndices);
	InstanceData->LineCharacterCounts = MoveTemp(OutLineCharacterCounts);
	InstanceData->WordStartIndices = MoveTemp(OutWordStartIndices);
	InstanceData->WordCharacterCounts = MoveTemp(OutWordCharacterCounts);

	return true;
}

TArray<FVector4> UNTPDataInterface::GetUVRectsFromFont(const UFont* FontAsset)
{
	TArray<FVector4> UVs;

	// Only offline cached fonts have the Characters array populated
	if (FontAsset && FontAsset->FontCacheType == EFontCacheType::Offline)
	{
		// Copy data from FFontCharacter array to Vector4 array
		UVs.Reserve(FontAsset->Characters.Num());

		for (const FFontCharacter& FontChar : FontAsset->Characters)
		{
			UVs.Add(FVector4(FontChar.USize, FontChar.VSize, (float)FontChar.StartU, (float)FontChar.StartV));
		}
	}
	else
	{
		UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: Font '%s' is invalid or not an offline cached font - Characters array will be empty"), *GetNameSafe(FontAsset));
	}

	return UVs;
}

TArray<FVector2f> UNTPDataInterface::GetCharacterPositions(const TArray<FVector4>& UVRects, FString InputString, ENTPTextHorizontalAlignment XAlignment, ENTPTextVerticalAlignment YAlignment)
{
	// Build Unicode from InputText
	TArray<int32> UnicodeUnfiltered;
	UnicodeUnfiltered.Reset();
	if (!InputString.IsEmpty())
	{
		UnicodeUnfiltered.Reserve(InputString.Len());
		for (int32 i = 0; i < InputString.Len(); ++i)
		{
			const int32 Code = (int32)InputString[i];
			UnicodeUnfiltered.Add(Code);
			// log the unicode
			UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: InitPerInstanceData - UnicodeFull[%d] = %d"), i, Code);
		}
	}

	const int32 NumCharsUnfiltered = UnicodeUnfiltered.Num();

	// Build 2D array of unicode values split by lines
	TArray<TArray<int32>> LinesUnfiltered;
	if (NumCharsUnfiltered > 0)
	{
		TArray<int32> CurrentLine;
		
		for (int32 i = 0; i < NumCharsUnfiltered; ++i)
		{
			const int32 Code = UnicodeUnfiltered[i];
			CurrentLine.Add(Code);
			
			// Check for newline characters
			if (Code == '\r') // CR (old Mac or part of CRLF)
			{
				// Check if next character is '\n' (CRLF)
				if (i + 1 < NumCharsUnfiltered && UnicodeUnfiltered[i + 1] == '\n')
				{
					// This is CRLF, add the '\n' to current line as well
					CurrentLine.Add('\n');
					i++; // Skip the '\n' in next iteration
					LinesUnfiltered.Add(CurrentLine);
					CurrentLine.Reset();
				}
				else
				{
					// This is standalone CR (old Mac format)
					LinesUnfiltered.Add(CurrentLine);
					CurrentLine.Reset();
				}
			}
			else if (Code == '\n') // LF (Unix/Mac/Windows, standalone)
			{
				LinesUnfiltered.Add(CurrentLine);
				CurrentLine.Reset();
			}
		}
		
		// Add the last line if it's not empty (or if text doesn't end with newline)
		if (!CurrentLine.IsEmpty())
		{
			LinesUnfiltered.Add(CurrentLine);
		}
	}

	const int32 NumLinesUnfiltered = LinesUnfiltered.Num();

	// Horizontal positions based on full text layout
	TArray<float> HorizontalPositions;
	HorizontalPositions.Reserve(NumCharsUnfiltered);
	if (NumLinesUnfiltered > 0)
	{
		switch (XAlignment)
		{
			case ENTPTextHorizontalAlignment::NTP_THA_Left:
			{
				HorizontalPositions = GetHorizontalPositionsLeftAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
			case ENTPTextHorizontalAlignment::NTP_THA_Center:
			{
				HorizontalPositions = GetHorizontalPositionsCenterAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
			case ENTPTextHorizontalAlignment::NTP_THA_Right:
			{
				HorizontalPositions = GetHorizontalPositionsRightAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
		}
	}


	// Vertical positions based on full text layout
	TArray<float> VerticalPositions;
	VerticalPositions.Reserve(NumCharsUnfiltered);
	if (NumLinesUnfiltered > 0)
	{
		switch (YAlignment)
		{
			case ENTPTextVerticalAlignment::NTP_TVA_Top:
			{
				VerticalPositions = GetVerticalPositionsTopAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
			case ENTPTextVerticalAlignment::NTP_TVA_Center:
			{
				VerticalPositions = GetVerticalPositionsCenterAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
			case ENTPTextVerticalAlignment::NTP_TVA_Bottom:
			{
				VerticalPositions = GetVerticalPositionsBottomAligned(UVRects, UnicodeUnfiltered, LinesUnfiltered);
				break;
			}
		}
	}

	TArray<FVector2f> CharacterPositions;
	CharacterPositions.Reserve(NumCharsUnfiltered);
	for (int32 i = 0; i < NumCharsUnfiltered; ++i)
	{
		CharacterPositions.Add(FVector2f(HorizontalPositions[i], VerticalPositions[i]));
	}

	return CharacterPositions;
}

void UNTPDataInterface::ProcessTextWithWhitespace(
	const FString& InputText,
	const TArray<FVector2f>& CharacterPositionsUnfiltered,
	TArray<int32>& OutUnicode,
	TArray<FVector2f>& OutCharacterPositions,
	TArray<int32>& OutLineStartIndices,
	TArray<int32>& OutLineCharacterCounts,
	TArray<int32>& OutWordStartIndices,
	TArray<int32>& OutWordCharacterCounts)
{

	OutLineStartIndices.Add(0);
	OutUnicode.Reserve(InputText.Len());
	OutCharacterPositions.Reserve(InputText.Len());

	bool bInsideWord = false;
	int32 CurrentWordStartIndex = -1;
	int32 CurrentWordCharCount = 0;

	for (int32 i = 0; i < InputText.Len(); ++i)
	{
		const int32 Code = (int32)InputText[i];
		const bool bIsWhitespace = IsWhitespaceChar(Code);

		if (!bIsWhitespace)
		{
			if (!bInsideWord)
			{
				bInsideWord = true;
				CurrentWordStartIndex = i;
				CurrentWordCharCount = 0;
			}
			CurrentWordCharCount++;
		}
		else
		{
			if (bInsideWord)
			{
				bInsideWord = false;
				OutWordStartIndices.Add(CurrentWordStartIndex);
				OutWordCharacterCounts.Add(CurrentWordCharCount);
			}
		}

		// Always add the character since we are spawning whitespace
		OutUnicode.Add(Code);
		OutCharacterPositions.Add(CharacterPositionsUnfiltered[i]);

		// Handle Newlines
		if (Code == '\n')
		{
			// Start new line on next index
			OutLineStartIndices.Add(i);
		}
		else if (Code == '\r')
		{
			// Check for CRLF
			if (i + 1 < InputText.Len() && InputText[i + 1] == '\n')
			{
				// Add the \n as well
				OutUnicode.Add('\n');
				OutCharacterPositions.Add(CharacterPositionsUnfiltered[i + 1]);
				++i; // Consume the \n
			}
			// Start new line on next index
			OutLineStartIndices.Add(i);
		}
	}

	if (bInsideWord)
	{
		OutWordStartIndices.Add(CurrentWordStartIndex);
		OutWordCharacterCounts.Add(CurrentWordCharCount);
	}

	OutLineCharacterCounts.Reset();
	OutLineCharacterCounts.Reserve(OutLineStartIndices.Num());
	for (int32 LineIdx = 0; LineIdx < OutLineStartIndices.Num(); ++LineIdx)
	{
		if (LineIdx < OutLineStartIndices.Num() - 1)
		{
			OutLineCharacterCounts.Add(OutLineStartIndices[LineIdx + 1] - OutLineStartIndices[LineIdx]);
		}
		else
		{
			OutLineCharacterCounts.Add(OutUnicode.Num() - OutLineStartIndices[LineIdx]);
		}
	}
}

void UNTPDataInterface::ProcessTextWithoutWhitespace(
	const FString& InputText,
	const TArray<FVector2f>& CharacterPositionsUnfiltered,
	TArray<int32>& OutUnicode,
	TArray<FVector2f>& OutCharacterPositions,
	TArray<int32>& OutLineStartIndices,
	TArray<int32>& OutLineCharacterCounts,
	TArray<int32>& OutWordStartIndices,
	TArray<int32>& OutWordCharacterCounts)
{
	OutLineStartIndices.Add(0);
	OutUnicode.Reserve(InputText.Len());
	OutCharacterPositions.Reserve(InputText.Len());

	bool bInsideWord = false;
	int32 CurrentWordStartIndex = -1;
	int32 CurrentWordCharCount = 0;
	
	int32 FilteredIndex = 0;
	for (int32 i = 0; i < InputText.Len(); ++i)
	{
		const int32 Code = (int32)InputText[i];
		const bool bIsWhitespace = IsWhitespaceChar(Code);

		if (!bIsWhitespace)
		{
			if (!bInsideWord)
			{
				bInsideWord = true;
				CurrentWordStartIndex = FilteredIndex;
				CurrentWordCharCount = 0;
			}
			CurrentWordCharCount++;
		}
		else
		{
			if (bInsideWord)
			{
				bInsideWord = false;
				OutWordStartIndices.Add(CurrentWordStartIndex);
				OutWordCharacterCounts.Add(CurrentWordCharCount);
			}
		}

		if (Code == '\n')
		{
			// Newline is whitespace, so we don't add it to Unicode
			// But we mark the start of a new line at the current FilteredIndex
			OutLineStartIndices.Add(FilteredIndex);
		}
		else if (Code == '\r')
		{
			// Consume CRLF \n if present
			if (i + 1 < InputText.Len() && InputText[i + 1] == '\n')
			{
				++i;
			}
			// Mark new line
			OutLineStartIndices.Add(FilteredIndex);
		}
		else if (bIsWhitespace)
		{
			// Skip other whitespace (space, tab)
			continue;
		}
		else
		{
			// Regular character
			OutUnicode.Add(Code);
			OutCharacterPositions.Add(CharacterPositionsUnfiltered[i]);
			++FilteredIndex;
		}
	}

	if (bInsideWord)
	{
		OutWordStartIndices.Add(CurrentWordStartIndex);
		OutWordCharacterCounts.Add(CurrentWordCharCount);
	}

	OutLineCharacterCounts.Reset();
	OutLineCharacterCounts.Reserve(OutLineStartIndices.Num());
	for (int32 LineIdx = 0; LineIdx < OutLineStartIndices.Num(); ++LineIdx)
	{
		if (LineIdx < OutLineStartIndices.Num() - 1)
		{
			OutLineCharacterCounts.Add(OutLineStartIndices[LineIdx + 1] - OutLineStartIndices[LineIdx]);
		}
		else
		{
			OutLineCharacterCounts.Add(OutUnicode.Num() - OutLineStartIndices[LineIdx]);
		}
	}
}

void UNTPDataInterface::BuildHorizontalLineMetrics(
	const TArray<FVector4>& UVRects,
	const TArray<TArray<int32>>& Lines,
	TArray<TArray<float>>& OutCumulativeWidthsPerCharacter)
{
	OutCumulativeWidthsPerCharacter.Reset();
	OutCumulativeWidthsPerCharacter.SetNum(Lines.Num());

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];
		OutCumulativeWidthsPerCharacter[i].Reserve(Line.Num());

		float CumulativeWidth = 0.0f;
		for (int32 j = 0; j < Line.Num(); ++j)
		{
			const int32 Code = Line[j];
			float Width = 0.0f;
			if (UVRects.IsValidIndex(Code))
			{
				Width = (float)UVRects[Code].X;
			}
			CumulativeWidth += Width;
			OutCumulativeWidthsPerCharacter[i].Add(CumulativeWidth);
		}
	}
}

void UNTPDataInterface::BuildVerticalLineMetrics(
	const TArray<FVector4>& UVRects,
	const TArray<TArray<int32>>& Lines,
	TArray<float>& OutCumulativeHeightsPerLine,
	float& OutLineHeight)
{
	OutCumulativeHeightsPerLine.Reset();
	OutCumulativeHeightsPerLine.Reserve(Lines.Num());

	// Compute a single global line height from the font's UV rects
	float GlobalMaxHeight = 0.0f;
	for (int32 i = 0; i < UVRects.Num(); ++i)
	{
		const float Height = (float)UVRects[i].Y;
		if (Height > GlobalMaxHeight)
		{
			GlobalMaxHeight = Height;
		}
	}

	OutLineHeight = GlobalMaxHeight;

	float CumulativeHeight = 0.0f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		CumulativeHeight += OutLineHeight;
		OutCumulativeHeightsPerLine.Add(CumulativeHeight);
	}
}

TArray<float> UNTPDataInterface::GetHorizontalPositionsLeftAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<TArray<float>> CumulativeWidthsPerCharacter;
	BuildHorizontalLineMetrics(UVRects, Lines, CumulativeWidthsPerCharacter);

	TArray<float> HorizontalPositions;
	HorizontalPositions.Reserve(Unicode.Num());

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];
		
		for (int32 j = 0; j < Line.Num(); ++j)
		{
			const int32 Code = Line[j];
			float CharWidth = 0.0f;
			if (UVRects.IsValidIndex(Code))
			{
				CharWidth = (float)UVRects[Code].X;
			}
			const float OffsetX = CumulativeWidthsPerCharacter[i][j] - (CharWidth * 0.5f);
			HorizontalPositions.Add(OffsetX);
		}
	}

	return HorizontalPositions;
}

TArray<float> UNTPDataInterface::GetHorizontalPositionsCenterAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<TArray<float>> CumulativeWidthsPerCharacter;
	BuildHorizontalLineMetrics(UVRects, Lines, CumulativeWidthsPerCharacter);

	TArray<float> HorizontalPositions;
	HorizontalPositions.Reserve(Unicode.Num());

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];

		if (Line.Num() == 0)
		{
			continue;
		}

		const float HalfLineWidth = CumulativeWidthsPerCharacter[i][Line.Num() - 1] * 0.5f;

		for (int32 j = 0; j < Line.Num(); ++j)
		{
			const int32 Code = Line[j];
			float CharWidth = 0.0f;
			if (UVRects.IsValidIndex(Code))
			{
				CharWidth = (float)UVRects[Code].X;
			}
			const float CenteredOffsetX = CumulativeWidthsPerCharacter[i][j] - HalfLineWidth - (CharWidth * 0.5f);
			HorizontalPositions.Add(CenteredOffsetX);
		}
	}

	return HorizontalPositions;
}

TArray<float> UNTPDataInterface::GetHorizontalPositionsRightAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<TArray<float>> CumulativeWidthsPerCharacter;
	BuildHorizontalLineMetrics(UVRects, Lines, CumulativeWidthsPerCharacter);

	TArray<float> HorizontalPositions;
	HorizontalPositions.Reserve(Unicode.Num());

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];

		if (Line.Num() == 0)
		{
			continue;
		}

		const float LineTotalWidth = CumulativeWidthsPerCharacter[i][Line.Num() - 1];

		for (int32 j = 0; j < Line.Num(); ++j)
		{
			const int32 Code = Line[j];
			float CharWidth = 0.0f;
			if (UVRects.IsValidIndex(Code))
			{
				CharWidth = (float)UVRects[Code].X;
			}
			const float OffsetX = CumulativeWidthsPerCharacter[i][j] - (CharWidth * 0.5f) - LineTotalWidth;
			HorizontalPositions.Add(OffsetX);
		}
	}

	return HorizontalPositions;
}

TArray<float> UNTPDataInterface::GetVerticalPositionsTopAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<float> CumulativeHeightsPerLine;
	float LineHeight = 0.0f;
	BuildVerticalLineMetrics(UVRects, Lines, CumulativeHeightsPerLine, LineHeight);

	TArray<float> VerticalPositions;
	VerticalPositions.Reserve(Unicode.Num());

	// First line's center is at 0, others go below it
	const float FirstLineHalfHeight = LineHeight * 0.5f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];
		const float CenteredOffsetY = CumulativeHeightsPerLine[i] - LineHeight * 0.5f - FirstLineHalfHeight;

		for (int32 j = 0; j < Line.Num(); ++j)
		{
			VerticalPositions.Add(CenteredOffsetY);
		}
	}
	
	return VerticalPositions;
}

TArray<float> UNTPDataInterface::GetVerticalPositionsCenterAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<float> CumulativeHeightsPerLine;
	float LineHeight = 0.0f;
	BuildVerticalLineMetrics(UVRects, Lines, CumulativeHeightsPerLine, LineHeight);

	TArray<float> VerticalPositions;
	VerticalPositions.Reserve(Unicode.Num());

	const float HalfTotalHeight = CumulativeHeightsPerLine[Lines.Num() - 1] * 0.5f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];
		const float CenteredOffsetY = CumulativeHeightsPerLine[i] - HalfTotalHeight - (LineHeight * 0.5f);

		for (int32 j = 0; j < Line.Num(); ++j)
		{
			VerticalPositions.Add(CenteredOffsetY);
		}
	}

	return VerticalPositions;
}

TArray<float> UNTPDataInterface::GetVerticalPositionsBottomAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines)
{
	TArray<float> CumulativeHeightsPerLine;
	float LineHeight = 0.0f;
	BuildVerticalLineMetrics(UVRects, Lines, CumulativeHeightsPerLine, LineHeight);

	TArray<float> VerticalPositions;
	VerticalPositions.Reserve(Unicode.Num());

	// Last line's center is at 0, others go above it
	const int32 LastLineIndex = Lines.Num() > 0 ? Lines.Num() - 1 : 0;
	const float Anchor = (Lines.Num() > 0) ? (CumulativeHeightsPerLine[LastLineIndex] - LineHeight * 0.5f) : 0.0f;

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const TArray<int32>& Line = Lines[i];
		const float CenteredOffsetY = CumulativeHeightsPerLine[i] - (LineHeight * 0.5f) - Anchor;

		for (int32 j = 0; j < Line.Num(); ++j)
		{
			VerticalPositions.Add(CenteredOffsetY);
		}
	}

	return VerticalPositions;
}

// Clean up RT instances
void UNTPDataInterface::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIFontUVInfoInstanceData* InstanceData = static_cast<FNDIFontUVInfoInstanceData*>(PerInstanceData);
	InstanceData->~FNDIFontUVInfoInstanceData();

	ENQUEUE_RENDER_COMMAND(RemoveProxy)
	(
		[RT_Proxy = GetProxyAs<FNDIFontUVInfoProxy>(), InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
		{
			if (FNDIFontUVInfoProxy::FRTInstanceData* Found = RT_Proxy->SystemInstancesToInstanceData_RT.Find(InstanceID))
			{
				Found->Release();
			}
			RT_Proxy->SystemInstancesToInstanceData_RT.Remove(InstanceID);
			UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): Removed InstanceID=%llu from RT map"), (uint64)InstanceID);
		}
	);
}

int32 UNTPDataInterface::PerInstanceDataSize() const
{
	return sizeof(FNDIFontUVInfoInstanceData);
}

void UNTPDataInterface::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIFontUVInfoProxy::ProvidePerInstanceDataForRenderThread(DataForRenderThread, PerInstanceData, SystemInstance);
}

UNTPDataInterface::UNTPDataInterface(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIFontUVInfoProxy());
}

// This registers our custom DI with Niagara
void UNTPDataInterface::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: Registered type with Niagara Type Registry"));
	}
}

// This lists all the functions our DI provides
void UNTPDataInterface::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{

	FNiagaraFunctionSignature SigUVRectAtIndex;
	SigUVRectAtIndex.Name = GetCharacterUVName;
#if WITH_EDITORONLY_DATA
	SigUVRectAtIndex.Description = LOCTEXT("GetCharacterUVFunctionDescription", "Returns the UV rect for a given character index. The UV rect contains USize, VSize, UStart, and VStart.");
#endif
	SigUVRectAtIndex.bMemberFunction = true;
	SigUVRectAtIndex.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigUVRectAtIndex.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterIndex")));
	SigUVRectAtIndex.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("USize")), LOCTEXT("USizeDescription", "The U size of the character UV rect"));
	SigUVRectAtIndex.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("VSize")), LOCTEXT("VSizeDescription", "The V size of the character UV rect"));
	SigUVRectAtIndex.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UStart")), LOCTEXT("UStartDescription", "The starting U coordinate of the character UV rect"));
	SigUVRectAtIndex.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("VStart")), LOCTEXT("VStartDescription", "The starting V coordinate of the character UV rect"));
	OutFunctions.Add(SigUVRectAtIndex);

	UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetFunctions - Registered function '%s' with 1 input (index) and 4 outputs."),
		*GetCharacterUVName.ToString());

	// Register GetCharacterPosition
	FNiagaraFunctionSignature SigPosition;
	SigPosition.Name = GetCharacterPositionName;
#if WITH_EDITORONLY_DATA
	SigPosition.Description = LOCTEXT("GetCharacterPositionDesc", "Returns the character position (Vector2) at CharacterIndex relative to the center of the text.");
#endif
	SigPosition.bMemberFunction = true;
	SigPosition.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigPosition.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterIndex")));
	SigPosition.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CharacterPosition")));
	OutFunctions.Add(SigPosition);

	// Register GetTextCharacterCount
	FNiagaraFunctionSignature SigLen;
	SigLen.Name = GetTextCharacterCountName;
#if WITH_EDITORONLY_DATA
	SigLen.Description = LOCTEXT("GetTextCharacterCountDesc", "Returns the number of characters in the DI's InputText.");
#endif
	SigLen.bMemberFunction = true;
	SigLen.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigLen.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterCount")));
	OutFunctions.Add(SigLen);

	// Register GetTextLineCount
	FNiagaraFunctionSignature SigTotalLines;
	SigTotalLines.Name = GetTextLineCountName;
#if WITH_EDITORONLY_DATA
	SigTotalLines.Description = LOCTEXT("GetTextLineCountDesc", "Returns the number of lines in the DI's InputText after splitting into lines.");
#endif
	SigTotalLines.bMemberFunction = true;
	SigTotalLines.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigTotalLines.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("LineCount")));
	OutFunctions.Add(SigTotalLines);

	// Register GetLineCharacterCount
	FNiagaraFunctionSignature SigLineCharCount;
	SigLineCharCount.Name = GetLineCharacterCountName;
#if WITH_EDITORONLY_DATA
	SigLineCharCount.Description = LOCTEXT("GetLineCharacterCountDesc", "Returns the number of characters in the specified line index of the DI's InputText.");
#endif
	SigLineCharCount.bMemberFunction = true;
	SigLineCharCount.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigLineCharCount.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("LineIndex")));
	SigLineCharCount.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("LineCharacterCount")));
	OutFunctions.Add(SigLineCharCount);

	// Register GetTextWordCount
	FNiagaraFunctionSignature SigWordCount;
	SigWordCount.Name = GetTextWordCountName;
#if WITH_EDITORONLY_DATA
	SigWordCount.Description = LOCTEXT("GetTextWordCountDesc", "Returns the number of words in the DI's InputText.");
#endif
	SigWordCount.bMemberFunction = true;
	SigWordCount.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigWordCount.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("WordCount")));
	OutFunctions.Add(SigWordCount);

	// Register GetWordCharacterCount
	FNiagaraFunctionSignature SigWordCharCount;
	SigWordCharCount.Name = GetWordCharacterCountName;
#if WITH_EDITORONLY_DATA
	SigWordCharCount.Description = LOCTEXT("GetWordCharacterCountDesc", "Returns the number of characters in the specified word index.");
#endif
	SigWordCharCount.bMemberFunction = true;
	SigWordCharCount.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigWordCharCount.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("WordIndex")));
	SigWordCharCount.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("WordCharacterCount")));
	OutFunctions.Add(SigWordCharCount);
}

void UNTPDataInterface::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNTPDataInterface::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FNDIFontUVInfoProxy& DataInterfaceProxy = Context.GetProxy<FNDIFontUVInfoProxy>();
	FNDIFontUVInfoProxy::FRTInstanceData* RTData = DataInterfaceProxy.SystemInstancesToInstanceData_RT.Find(Context.GetSystemInstanceID());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	DataInterfaceProxy.EnsureDefaultBuffer(RHICmdList);

	FShaderParameters* ShaderParameters = Context.GetParameterNestedStruct<FShaderParameters>();
	if (RTData && RTData->UVRectsBuffer.SRV.IsValid())
	{
		ShaderParameters->UVRects = RTData->UVRectsBuffer.SRV;
		ShaderParameters->NumRects = RTData->NumRects;
		ShaderParameters->TextUnicode = RTData->UnicodeBuffer.SRV.IsValid() ? RTData->UnicodeBuffer.SRV : DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->CharacterPositions = RTData->CharacterPositionsBuffer.SRV.IsValid() ? RTData->CharacterPositionsBuffer.SRV : DataInterfaceProxy.DefaultFloatBuffer.SRV;
		ShaderParameters->LineStartIndices = RTData->LineStartIndicesBuffer.SRV.IsValid() ? RTData->LineStartIndicesBuffer.SRV : DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->LineCharacterCounts = RTData->LineCharacterCountBuffer.SRV.IsValid() ? RTData->LineCharacterCountBuffer.SRV : DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->WordStartIndices = RTData->WordStartIndicesBuffer.SRV.IsValid() ? RTData->WordStartIndicesBuffer.SRV : DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->WordCharacterCounts = RTData->WordCharacterCountBuffer.SRV.IsValid() ? RTData->WordCharacterCountBuffer.SRV : DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->NumChars = RTData->NumChars;
		ShaderParameters->NumLines = RTData->NumLines;
		ShaderParameters->NumWords = RTData->NumWords;
	}
	else
	{
		ShaderParameters->UVRects = DataInterfaceProxy.DefaultUVRectsBuffer.SRV;
		ShaderParameters->NumRects = 0;
		ShaderParameters->TextUnicode = DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->CharacterPositions = DataInterfaceProxy.DefaultFloatBuffer.SRV;
		ShaderParameters->LineStartIndices = DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->LineCharacterCounts = DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->WordStartIndices = DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->WordCharacterCounts = DataInterfaceProxy.DefaultUIntBuffer.SRV;
		ShaderParameters->NumChars = 0;
		ShaderParameters->NumLines = 0;
		ShaderParameters->NumWords = 0;
	}
}

bool UNTPDataInterface::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	UNTPDataInterface* DestTyped = Cast<UNTPDataInterface>(Destination);
	if (DestTyped)
	{
		DestTyped->FontAsset = FontAsset;
		DestTyped->InputText = InputText;
		DestTyped->HorizontalAlignment = HorizontalAlignment;
		DestTyped->VerticalAlignment = VerticalAlignment;
		DestTyped->bSpawnWhitespaceCharacters = bSpawnWhitespaceCharacters;
		return true;
	}
	else
	{
		UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: CopyToInternal - Destination cast failed"));
		return false;
	}
}

bool UNTPDataInterface::Equals(const UNiagaraDataInterface* Other) const
{
	const UNTPDataInterface* OtherTyped = Cast<UNTPDataInterface>(Other);
	const bool bEqual = OtherTyped
		&& OtherTyped->FontAsset == FontAsset
		&& OtherTyped->InputText == InputText
		&& OtherTyped->HorizontalAlignment == HorizontalAlignment
		&& OtherTyped->VerticalAlignment == VerticalAlignment
		&& OtherTyped->bSpawnWhitespaceCharacters == bSpawnWhitespaceCharacters;
	UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI: Equals - ThisAsset=%s OtherAsset=%s Result=%s"),
		*GetNameSafe(FontAsset),
		OtherTyped ? *GetNameSafe(OtherTyped->FontAsset) : TEXT("nullptr"),
		bEqual ? TEXT("true") : TEXT("false"));
	return bEqual;
}

// This provides the cpu vm with the correct function to call
void UNTPDataInterface::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == GetCharacterUVName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterUVVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetCharacterPositionName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterPositionVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextCharacterCountVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextLineCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextLineCountVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetLineCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetLineCharacterCountVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextWordCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextWordCountVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetWordCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetWordCharacterCountVM(Context); });
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else
	{
		UE_LOG(LogNiagaraTextParticles, Display, TEXT("Could not find data interface external function in %s. Received Name: %s"), *GetPathNameSafe(this), *BindingInfo.Name.ToString());
	}
}


// Implementation called by the vectorVM
void UNTPDataInterface::GetCharacterUVVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InCharacterIndex(Context);
	FNDIOutputParam<float> OutUSize(Context);
	FNDIOutputParam<float> OutVSize(Context);
	FNDIOutputParam<float> OutUStart(Context);
	FNDIOutputParam<float> OutVStart(Context);

	const TArray<int32>& Unicode = InstData.Get()->Unicode;
	const TArray<FVector4>& UVRects = InstData.Get()->UVRects;
	const int32 NumRects = UVRects.Num();

	UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI: GetUVRectVM - NumInstances=%d, UVRects.Num=%d"),
		Context.GetNumInstances(), NumRects);

	// Iterate over the particles
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		const int32 CharacterIndex = InCharacterIndex.GetAndAdvance();

		const int32 UnicodeIndex = (Unicode.IsValidIndex(CharacterIndex)) ? Unicode[CharacterIndex] : -1;

		// Bounds check
		if (NumRects > 0 && UnicodeIndex >= 0 && UnicodeIndex < NumRects)
		{
			const FVector4& UVRect = UVRects[UnicodeIndex];
			OutUSize.SetAndAdvance(UVRect.X);
			OutVSize.SetAndAdvance(UVRect.Y);
			OutUStart.SetAndAdvance(UVRect.Z);
			OutVStart.SetAndAdvance(UVRect.W);

			if (i < 4)
			{
				UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI: VM idx=%d UnicodeIndex=%d -> UV=[%s]"),
					i, UnicodeIndex, *UVRect.ToString());
			}
		}
		else
		{
			// Return zero for invalid indices
			OutUSize.SetAndAdvance(0.0f);
			OutVSize.SetAndAdvance(0.0f);
			OutUStart.SetAndAdvance(0.0f);
			OutVStart.SetAndAdvance(0.0f);

			if (i < 4)
			{
				UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: VM idx=%d UnicodeIndex=%d out of bounds (NumRects=%d) - returning zeros"),
					i, UnicodeIndex, NumRects);
			}
		}
	}
}

void UNTPDataInterface::GetCharacterPositionVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InCharacterIndex(Context);
	FNDIOutputParam<FVector3f> OutPosition(Context);

	const TArray<FVector2f>& Positions = InstData.Get()->CharacterPositions;
	const int32 NumChars = InstData.Get()->Unicode.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 Index = InCharacterIndex.GetAndAdvance();

		if (NumChars <= 0)
		{
			OutPosition.SetAndAdvance(FVector3f(0.0f, 0.0f, 0.0f));
			continue;
		}

		Index = FMath::Clamp(Index, 0, NumChars - 1);
		const FVector2f Position2 = Positions.IsValidIndex(Index) ? Positions[Index] : FVector2f(0.0f, 0.0f);

		// UE Coordinates: X (forward) = 0, Y (left/right) = horizontal, Z (up/down) = vertical
		// The position is calculated by adding the cumulative character widths and line heights (positive values)
		// This causes the vertical component to go in the positive direction, but the final Z value should be negative
		// for subsequent lines. Similarly the horizontal component goes in the positive direction, but positive Y
		// in UE's cooridnate system is left, and we need the text to go right.
		// So, we flip both values
		OutPosition.SetAndAdvance(FVector3f(0.0f, -Position2.X, -Position2.Y));
	}
}

void UNTPDataInterface::GetTextCharacterCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIOutputParam<int32> OutLen(Context);

	const int32 NumChars = InstData.Get()->Unicode.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutLen.SetAndAdvance(NumChars);
	}
}

void UNTPDataInterface::GetTextLineCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIOutputParam<int32> OutTotalLines(Context);

	const int32 NumLines = InstData.Get()->LineStartIndices.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutTotalLines.SetAndAdvance(NumLines);
	}
}

void UNTPDataInterface::GetLineCharacterCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InLineIndex(Context);
	FNDIOutputParam<int32> OutLineCharacterCount(Context);

	const TArray<int32>& LineCharacterCounts = InstData.Get()->LineCharacterCounts;
	const int32 NumLines = InstData.Get()->LineStartIndices.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 LineIndex = InLineIndex.GetAndAdvance();

		if (NumLines > 0 && LineIndex >= 0 && LineIndex < NumLines && LineCharacterCounts.IsValidIndex(LineIndex))
		{
			OutLineCharacterCount.SetAndAdvance(LineCharacterCounts[LineIndex]);
		}
		else
		{
			OutLineCharacterCount.SetAndAdvance(0);
		}
	}
}

void UNTPDataInterface::GetTextWordCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIOutputParam<int32> OutWordCount(Context);

	const int32 NumWords = InstData.Get()->WordStartIndices.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutWordCount.SetAndAdvance(NumWords);
	}
}

void UNTPDataInterface::GetWordCharacterCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InWordIndex(Context);
	FNDIOutputParam<int32> OutWordCharacterCount(Context);

	const TArray<int32>& WordCharacterCounts = InstData.Get()->WordCharacterCounts;
	const int32 NumWords = InstData.Get()->WordStartIndices.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 WordIndex = InWordIndex.GetAndAdvance();

		if (NumWords > 0 && WordIndex >= 0 && WordIndex < NumWords && WordCharacterCounts.IsValidIndex(WordIndex))
		{
			OutWordCharacterCount.SetAndAdvance(WordCharacterCounts[WordIndex]);
		}
		else
		{
			OutWordCharacterCount.SetAndAdvance(0);
		}
	}
}

#if WITH_EDITORONLY_DATA

bool UNTPDataInterface::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
	{
		return false;
	}
	InVisitor->UpdateShaderFile(FontUVTemplateShaderFile);
	InVisitor->UpdateShaderParameters<FShaderParameters>();
	return true;
}

bool UNTPDataInterface::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	return FunctionInfo.DefinitionName == GetCharacterUVName
		|| FunctionInfo.DefinitionName == GetCharacterPositionName
		|| FunctionInfo.DefinitionName == GetTextCharacterCountName
		|| FunctionInfo.DefinitionName == GetTextLineCountName
		|| FunctionInfo.DefinitionName == GetLineCharacterCountName
		|| FunctionInfo.DefinitionName == GetTextWordCountName
		|| FunctionInfo.DefinitionName == GetWordCharacterCountName;
}

void UNTPDataInterface::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	const TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("ParameterName"), ParamInfo.DataInterfaceHLSLSymbol },
	};
	AppendTemplateHLSL(OutHLSL, FontUVTemplateShaderFile, TemplateArgs);
}

#endif

#undef LOCTEXT_NAMESPACE

