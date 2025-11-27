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
		|| Code == '\t';
}

// Iterator that understands newlines and reports original source indices per character,
struct FNTPTextIterator
{
	const FString& Source;
	const int32 Length;
	int32 CurrentIndex;

	explicit FNTPTextIterator(const FString& InSource)
		: Source(InSource)
		, Length(InSource.Len())
		, CurrentIndex(0)
	{
	}

	// Returns true as long as we are not past the end of the string.
	bool HasNextCharacter()
	{
		return CurrentIndex < Length;
	}

	// Returns the next character in the current logical line.
	// Newline characters ('\n' and '\r' / "\r\n") are consumed but never returned.
	bool NextCharacterInLine(int32& OutSourceIndex, TCHAR& OutChar)
	{
		if (CurrentIndex >= Length)
		{
			return false;
		}

		const TCHAR Ch = Source[CurrentIndex];

		// Handle newlines as line separators, not drawable characters.
		if (Ch == '\n')
		{
			++CurrentIndex;
			return false;
		}

		if (Ch == '\r')
		{
			// Treat CRLF as a single newline.
			if (CurrentIndex + 1 < Length && Source[CurrentIndex + 1] == '\n')
			{
				CurrentIndex += 2;
			}
			else
			{
				++CurrentIndex;
			}
			return false;
		}

		OutSourceIndex = CurrentIndex;
		OutChar = Ch;
		++CurrentIndex;
		return true;
	}

	// Peek at the next character in the current logical line without advancing.
	// Returns false at end-of-line or end-of-string.
	bool PeekNextCharacterInLine(TCHAR& OutChar) const
	{
		if (CurrentIndex >= Length)
		{
			return false;
		}

		const TCHAR Ch = Source[CurrentIndex];
		if (Ch == '\n' || Ch == '\r')
		{
			return false;
		}

		OutChar = Ch;
		return true;
	}
};

const FName UNTPDataInterface::GetCharacterUVName(TEXT("GetCharacterUV"));
const FName UNTPDataInterface::GetCharacterPositionName(TEXT("GetCharacterPosition"));
const FName UNTPDataInterface::GetTextCharacterCountName(TEXT("GetTextCharacterCount"));
const FName UNTPDataInterface::GetTextLineCountName(TEXT("GetTextLineCount"));
const FName UNTPDataInterface::GetLineCharacterCountName(TEXT("GetLineCharacterCount"));
const FName UNTPDataInterface::GetTextWordCountName(TEXT("GetTextWordCount"));
const FName UNTPDataInterface::GetWordCharacterCountName(TEXT("GetWordCharacterCount"));
const FName UNTPDataInterface::GetWordTrailingWhitespaceCountName(TEXT("GetWordTrailingWhitespaceCount"));
const FName UNTPDataInterface::GetFilterWhitespaceCharactersName(TEXT("GetFilterWhitespaceCharacters"));
const FName UNTPDataInterface::GetCharacterCountInWordRangeName(TEXT("GetCharacterCountInWordRange"));
const FName UNTPDataInterface::GetCharacterCountInLineRangeName(TEXT("GetCharacterCountInLineRange"));
const FName UNTPDataInterface::GetCharacterSpriteSizeName(TEXT("GetCharacterSpriteSize"));

// The struct used to store our data interface data
struct FNDIFontUVInfoInstanceData
{
	// Normalized per-glyph UVs in texture space: (USize, VSize, UStart, VStart), all in 0-1
	TArray<FVector4> CharacterTextureUvs;
	// Per-glyph sprite size in pixels: (Width, Height)
	TArray<FVector2f> CharacterSpriteSizes;
	TArray<int32> Unicode;
	TArray<FVector2f> CharacterPositions;
	TArray<int32> LineStartIndices;
	TArray<int32> LineCharacterCounts;
	TArray<int32> WordStartIndices;
	TArray<int32> WordCharacterCounts;
	bool bFilterWhitespaceCharactersValue = true;
};

// This proxy is used to safely copy data between game thread and render thread
struct FNDIFontUVInfoProxy : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIFontUVInfoInstanceData); }

	struct FRTInstanceData
	{
		FRWBufferStructured CharacterTextureUvsBuffer;
		FRWBufferStructured CharacterSpriteSizesBuffer;
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
		uint32 bFilterWhitespaceCharactersValue = 1;

		void Release()
		{
			CharacterTextureUvsBuffer.Release();
			CharacterSpriteSizesBuffer.Release();
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
			bFilterWhitespaceCharactersValue = 1;
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
			DefaultUVRectsBuffer.Initialize(RHICmdList, TEXT("NTP_CharacterTextureUvs_Default"), sizeof(FVector4f), 1, BUF_ShaderResource | BUF_Static);
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

		UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): ProvidePerInstanceDataForRenderThread - InstanceID=%llu, CharacterTextureUvs.Num=%d"),
			(uint64)SystemInstance, DataForRenderThread->CharacterTextureUvs.Num());
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		FNDIFontUVInfoInstanceData* InstanceDataFromGT = static_cast<FNDIFontUVInfoInstanceData*>(PerInstanceData);
		FRTInstanceData& RTInstance = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// Upload / rebuild structured buffers on RT
		const int32 NumRects = InstanceDataFromGT->CharacterTextureUvs.Num();
		RTInstance.Release();
		const uint32 RectStride = sizeof(FVector4f);
		const uint32 RectCount  = FMath::Max(NumRects, 1);
		RTInstance.NumRects = (uint32)NumRects;
		RTInstance.CharacterTextureUvsBuffer.Initialize(RHICmdList, TEXT("NTP_CharacterTextureUvs"), RectStride, RectCount, BUF_ShaderResource | BUF_Static);

		const uint32 RectNumBytes = RectStride * RectCount;
		// Convert to float4 to match HLSL StructuredBuffer<float4> for CharacterTextureUvs
		TArray<FVector4f> TempFloatRects;
		TempFloatRects.SetNumUninitialized(RectCount);
		if (NumRects > 0)
		{
			for (int32 i = 0; i < NumRects; ++i)
			{
				const FVector4& Src = InstanceDataFromGT->CharacterTextureUvs[i];
				TempFloatRects[i] = FVector4f((float)Src.X, (float)Src.Y, (float)Src.Z, (float)Src.W);
			}
		}
		else
		{
			TempFloatRects[0] = FVector4f(0, 0, 0, 0);
		}

		void* DestRects = RHICmdList.LockBuffer(RTInstance.CharacterTextureUvsBuffer.Buffer, 0, RectNumBytes, RLM_WriteOnly);
		FMemory::Memcpy(DestRects, TempFloatRects.GetData(), RectNumBytes);
		RHICmdList.UnlockBuffer(RTInstance.CharacterTextureUvsBuffer.Buffer);

		// Upload sprite sizes buffer (float2 per glyph, in pixels)
		{
			const int32 NumSpriteSizes = InstanceDataFromGT->CharacterSpriteSizes.Num();
			const uint32 SizeStride = sizeof(FVector2f);
			const uint32 SizeCount  = FMath::Max(NumSpriteSizes, 1);
			RTInstance.CharacterSpriteSizesBuffer.Initialize(RHICmdList, TEXT("NTP_CharacterSpriteSizes"), SizeStride, SizeCount, BUF_ShaderResource | BUF_Static);

			TArray<FVector2f> TempSizes;
			TempSizes.SetNumUninitialized(SizeCount);
			if (NumSpriteSizes > 0)
			{
				FMemory::Memcpy(TempSizes.GetData(), InstanceDataFromGT->CharacterSpriteSizes.GetData(), sizeof(FVector2f) * NumSpriteSizes);
			}
			else
			{
				TempSizes[0] = FVector2f(0.0f, 0.0f);
			}

			void* DestSizes = RHICmdList.LockBuffer(RTInstance.CharacterSpriteSizesBuffer.Buffer, 0, SizeStride * SizeCount, RLM_WriteOnly);
			FMemory::Memcpy(DestSizes, TempSizes.GetData(), SizeStride * SizeCount);
			RHICmdList.UnlockBuffer(RTInstance.CharacterSpriteSizesBuffer.Buffer);
		}

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

		// Copy line and word count and flags
		RTInstance.NumChars = (uint32)InstanceDataFromGT->Unicode.Num();
		RTInstance.NumLines = (uint32)InstanceDataFromGT->LineStartIndices.Num();
		RTInstance.NumWords = (uint32)InstanceDataFromGT->WordStartIndices.Num();
		RTInstance.bFilterWhitespaceCharactersValue = InstanceDataFromGT->bFilterWhitespaceCharactersValue ? 1u : 0u;

		// Call the destructor to clean up the GT data
		InstanceDataFromGT->~FNDIFontUVInfoInstanceData();

		UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): ConsumePerInstanceDataFromGameThread - InstanceID=%llu, CharacterTextureUvs.Num=%u"),
			(uint64)InstanceID, RTInstance.NumRects);
	}

	TMap<FNiagaraSystemInstanceID, FRTInstanceData> SystemInstancesToInstanceData_RT;
};

// Creates a new data object to store our data
bool UNTPDataInterface::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIFontUVInfoInstanceData* InstanceData = new (PerInstanceData) FNDIFontUVInfoInstanceData;

	TArray<FVector4> CharacterTextureUvs;
	TArray<FVector2f> CharacterSpriteSizes;
	TArray<int32> VerticalOffsets;
	int32 Kerning = 0;
	if (!GetFontInfo(FontAsset, CharacterTextureUvs, CharacterSpriteSizes, VerticalOffsets, Kerning))
	{
		UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: Failed to get font info from FontAsset '%s'"), *GetNameSafe(FontAsset));
	}
	
	TArray<FVector2f> CharacterPositionsUnfiltered = GetCharacterPositions(CharacterSpriteSizes, VerticalOffsets, Kerning, InputText, HorizontalAlignment, VerticalAlignment);
	
	TArray<int32> OutUnicode;
	TArray<FVector2f> OutCharacterPositions;
	TArray<int32> OutLineStartIndices;
	TArray<int32> OutLineCharacterCounts;
	TArray<int32> OutWordStartIndices;
	TArray<int32> OutWordCharacterCounts;

	ProcessText(InputText, CharacterPositionsUnfiltered, bFilterWhitespaceCharacters, OutUnicode, OutCharacterPositions, OutLineStartIndices, OutLineCharacterCounts, OutWordStartIndices, OutWordCharacterCounts);

	InstanceData->CharacterTextureUvs = MoveTemp(CharacterTextureUvs);
	InstanceData->CharacterSpriteSizes = MoveTemp(CharacterSpriteSizes);
	InstanceData->bFilterWhitespaceCharactersValue = bFilterWhitespaceCharacters;
	InstanceData->Unicode = MoveTemp(OutUnicode);
	InstanceData->CharacterPositions = MoveTemp(OutCharacterPositions);
	InstanceData->LineStartIndices = MoveTemp(OutLineStartIndices);
	InstanceData->LineCharacterCounts = MoveTemp(OutLineCharacterCounts);
	InstanceData->WordStartIndices = MoveTemp(OutWordStartIndices);
	InstanceData->WordCharacterCounts = MoveTemp(OutWordCharacterCounts);

	return true;
}

bool UNTPDataInterface::GetFontInfo(const UFont* FontAsset, TArray<FVector4>& OutCharacterTextureUvs, TArray<FVector2f>& OutCharacterSpriteSizes, TArray<int32>& OutVerticalOffsets, int32& OutKerning)
{
	OutCharacterTextureUvs.Reset();
	OutCharacterSpriteSizes.Reset();
	OutVerticalOffsets.Reset();
	OutKerning = 0;

	// Only offline cached fonts have the Characters array populated
	if (FontAsset && FontAsset->FontCacheType == EFontCacheType::Offline)
	{
		// Try to get the first font texture so we can normalize glyph UVs into 0-1 space.
		const UTexture2D* FontTexture = nullptr;
		if (FontAsset->Textures.Num() > 0)
		{
			FontTexture = Cast<UTexture2D>(FontAsset->Textures[0]);
		}

		FVector2f InvTextureSize(1.0f, 1.0f);
		if (FontTexture)
		{
			const float TexW = static_cast<float>(FontTexture->GetSizeX());
			const float TexH = static_cast<float>(FontTexture->GetSizeY());
			if (TexW > 0.0f && TexH > 0.0f)
			{
				InvTextureSize = FVector2f(1.0f / TexW, 1.0f / TexH);
			}
			else
			{
				UE_LOG(LogNiagaraTextParticles, Warning,
					TEXT("NTP DI: Font '%s' texture has invalid size (%f x %f) - UVs will not be normalized"),
					*GetNameSafe(FontAsset), TexW, TexH);
			}
		}
		else
		{
			UE_LOG(LogNiagaraTextParticles, Warning,
				TEXT("NTP DI: Font '%s' has no textures - UVs will not be normalized"),
				*GetNameSafe(FontAsset));
		}

		// Copy data from FFontCharacter array to our arrays
		const int32 NumCharacters = FontAsset->Characters.Num();
		OutCharacterTextureUvs.Reserve(NumCharacters);
		OutCharacterSpriteSizes.Reserve(NumCharacters);
		OutVerticalOffsets.Reserve(NumCharacters);

		for (const FFontCharacter& FontChar : FontAsset->Characters)
		{
			const float USizePx = static_cast<float>(FontChar.USize);
			const float VSizePx = static_cast<float>(FontChar.VSize);
			const float UStartPx = static_cast<float>(FontChar.StartU);
			const float VStartPx = static_cast<float>(FontChar.StartV);

			// Store sprite size in pixels for layout / particle sizing.
			OutCharacterSpriteSizes.Add(FVector2f(USizePx, VSizePx));

			// Precompute normalized UVs so shaders/materials don't have to divide by texture resolution.
			const float USizeNorm  = USizePx  * InvTextureSize.X;
			const float VSizeNorm  = VSizePx  * InvTextureSize.Y;
			const float UStartNorm = UStartPx * InvTextureSize.X;
			const float VStartNorm = VStartPx * InvTextureSize.Y;

			// Layout: (USize, VSize, UStart, VStart) in 0-1 texture space.
			OutCharacterTextureUvs.Add(FVector4(USizeNorm, VSizeNorm, UStartNorm, VStartNorm));
			OutVerticalOffsets.Add(FontChar.VerticalOffset);
		}

		OutKerning = FontAsset->Kerning;
		return true;
	}
	else
	{
		UE_LOG(LogNiagaraTextParticles, Warning, TEXT("NTP DI: Font '%s' is invalid or not an offline cached font - Characters array will be empty"), *GetNameSafe(FontAsset));
		return false;
	}
}

TArray<FVector2f> UNTPDataInterface::GetCharacterPositions(const TArray<FVector2f>& CharacterSpriteSizes, const TArray<int32>& VerticalOffsets, int32 Kerning, FString InputString, ENTPTextHorizontalAlignment XAlignment, ENTPTextVerticalAlignment YAlignment)
{

	TArray<FVector2f> CharacterPositionsUnfiltered;

	const int32 TextLength = InputString.Len();
	if (TextLength <= 0 || CharacterSpriteSizes.Num() == 0)
	{
		return CharacterPositionsUnfiltered;
	}

	// Initialize to (0,0) so that indices for newline characters
	// are still valid when later indexed by the text processing passes.
	CharacterPositionsUnfiltered.Init(FVector2f(0.0f, 0.0f), TextLength);

	// Global fallback line height in case a line has no drawable characters.
	float GlobalMaxGlyphHeight = 0.0f;
	for (const FVector2f& Size : CharacterSpriteSizes)
	{
		GlobalMaxGlyphHeight = FMath::Max(GlobalMaxGlyphHeight, Size.Y);
	}

	const float CharIncrement = static_cast<float>(Kerning); // No extra horizontal spacing in this data interface.

	// Per-line widths, heights, and tops
	// tops are aligned at 0, so the top of the first line is at 0, and the top of the second line is the height of the first line, etc.
	TArray<float> LineWidths;
	TArray<float> LineHeights;
	TArray<float> LineTops;
	float TotalHeight = 0.0f;

	FNTPTextIterator It(InputString);

	while (It.HasNextCharacter())
	{
		float LineX = 0.0f;
		float MaxBottom = 0.0f;

		int32 SourceIndex = INDEX_NONE;
		TCHAR Ch = 0;

		while (It.NextCharacterInLine(SourceIndex, Ch))
		{
			const int32 Code = static_cast<int32>(Ch);

			// Skip characters that do not have glyph data. (positions will be set to 0,0)
			if (!CharacterSpriteSizes.IsValidIndex(Code) || !VerticalOffsets.IsValidIndex(Code))
			{
				continue;
			}

			const FVector2f& GlyphSize = CharacterSpriteSizes[Code];

			const float SizeX = GlyphSize.X;
			const float SizeY = GlyphSize.Y;
			const float TopY  = static_cast<float>(VerticalOffsets[Code]); // how far from the line's origin its top is

			const float BottomY = TopY + SizeY; // how far from the line's origin its bottom is
			MaxBottom = FMath::Max(MaxBottom, BottomY);

			LineX += SizeX;

			// If we have another non-whitespace character on this line, add kerning.
			TCHAR NextCh = 0;
			if (It.PeekNextCharacterInLine(NextCh) && !FChar::IsWhitespace(NextCh))
			{
				LineX += CharIncrement;
			}
		}

		LineWidths.Add(LineX);

		const float LineHeight = (MaxBottom > 0.0f) ? MaxBottom : GlobalMaxGlyphHeight;
		LineHeights.Add(LineHeight);
		LineTops.Add(TotalHeight);
		TotalHeight += LineHeight;
	}

	// if there are no lines, return an array of all zeros (in the case where all characters are newlines)
	const int32 NumLines = LineWidths.Num();
	if (NumLines == 0)
	{
		return CharacterPositionsUnfiltered;
	}

	// Vertical alignment: decide where the block of text is placed relative to Y=0.
	float VerticalOffset = 0.0f;
	switch (YAlignment)
	{
		case ENTPTextVerticalAlignment::NTP_TVA_Top:
		{
			// Top of first line at Y=0.
			VerticalOffset = 0.0f;
			break;
		}
		case ENTPTextVerticalAlignment::NTP_TVA_Center:
		{
			// Center of the whole block at Y=0.
			VerticalOffset = -(TotalHeight * 0.5f);
			break;
		}
		case ENTPTextVerticalAlignment::NTP_TVA_Bottom:
		{
			// Bottom of the last line at Y=0.
			VerticalOffset = -TotalHeight;
			break;
		}
		default:
		{
			break;
		}
	}

	// Horizontal alignment: compute per-line starting X.
	TArray<float> LineStartX;
	LineStartX.SetNum(NumLines);

	for (int32 LineIdx = 0; LineIdx < NumLines; ++LineIdx)
	{
		const float Width = LineWidths[LineIdx];
		float StartX = 0.0f;

		switch (XAlignment)
		{
			case ENTPTextHorizontalAlignment::NTP_THA_Left:
			{
				StartX = 0.0f;
				break;
			}
			case ENTPTextHorizontalAlignment::NTP_THA_Center:
			{
				StartX = -Width * 0.5f;
				break;
			}
			case ENTPTextHorizontalAlignment::NTP_THA_Right:
			{
				StartX = -Width;
				break;
			}
			default:
			{
				break;
			}
		}

		LineStartX[LineIdx] = StartX;
	}

	// Second pass: assign a position to each character index in the original string,
	// walking the text again line-by-line using the iterator.
	FNTPTextIterator It2(InputString);

	for (int32 LineIdx = 0; LineIdx < NumLines && It2.HasNextCharacter(); ++LineIdx)
	{
		float LineX = 0.0f;
		const float LineTop = LineTops[LineIdx] + VerticalOffset;

		int32 SourceIndex = INDEX_NONE;
		TCHAR Ch = 0;

		while (It2.NextCharacterInLine(SourceIndex, Ch))
		{
			const int32 Code = static_cast<int32>(Ch);

			// Skip characters that do not have glyph data. (positions will be set to 0,0)
			if (!CharacterSpriteSizes.IsValidIndex(Code) || !VerticalOffsets.IsValidIndex(Code))
			{
				continue;
			}

			const FVector2f& GlyphSize = CharacterSpriteSizes[Code];

			const float SizeX = GlyphSize.X;
			const float SizeY = GlyphSize.Y;
			const float TopY  = static_cast<float>(VerticalOffsets[Code]);

			const float GlyphLeft = LineStartX[LineIdx] + LineX;
			const float GlyphTop  = LineTop + TopY;

			const float PosX = GlyphLeft + SizeX * 0.5f;
			const float PosY = GlyphTop + SizeY * 0.5f;

			CharacterPositionsUnfiltered[SourceIndex] = FVector2f(PosX, PosY);

			LineX += SizeX;

			// Apply kerning based on the next character in this logical line, if any.
			TCHAR NextCh = 0;
			if (It2.PeekNextCharacterInLine(NextCh) && !FChar::IsWhitespace(NextCh))
			{
				LineX += CharIncrement;
			}
		}
	}

	return CharacterPositionsUnfiltered;
}

void UNTPDataInterface::ProcessText(
	const FString& InputText,
	const TArray<FVector2f>& CharacterPositionsUnfiltered,
	const bool bFilterWhitespace,
	TArray<int32>& OutUnicode,
	TArray<FVector2f>& OutCharacterPositions,
	TArray<int32>& OutLineStartIndices,
	TArray<int32>& OutLineCharacterCounts,
	TArray<int32>& OutWordStartIndices,
	TArray<int32>& OutWordCharacterCounts)
{
	OutUnicode.Reset();
	OutCharacterPositions.Reset();
	OutLineStartIndices.Reset();
	OutWordStartIndices.Reset();
	OutWordCharacterCounts.Reset();

	// First line always starts at index 0.
	OutLineStartIndices.Add(0);

	OutUnicode.Reserve(InputText.Len());
	OutCharacterPositions.Reserve(InputText.Len());

	FNTPTextIterator It(InputText);

	bool bInsideWord = false;
	int32 CurrentWordStartIndex = -1;
	int32 CurrentWordCharCount = 0;

	while (It.HasNextCharacter())
	{
		int32 SourceIndex = INDEX_NONE;
		TCHAR Ch = 0;

		while (It.NextCharacterInLine(SourceIndex, Ch))
		{
			const int32 Code = static_cast<int32>(Ch);
			const bool bIsWhitespace = IsWhitespaceChar(Code);

			// Handle word state transitions
			if (bIsWhitespace)
			{
				if (bInsideWord)
				{
					bInsideWord = false;
					OutWordStartIndices.Add(CurrentWordStartIndex);
					OutWordCharacterCounts.Add(CurrentWordCharCount);
				}
			}
			else
			{
				if (!bInsideWord)
				{
					bInsideWord = true;
					CurrentWordStartIndex = OutUnicode.Num();
					CurrentWordCharCount = 0;
				}
				CurrentWordCharCount++;
			}

			// Filter logic: if filtering is on and it's whitespace, skip output.
			if (bFilterWhitespace && bIsWhitespace)
			{
				continue;
			}

			// Add to output
			OutUnicode.Add(Code);
			OutCharacterPositions.Add(CharacterPositionsUnfiltered[SourceIndex]);
		}

		// End of logical line. Check if there is another line following (meaning we consumed a newline).
		if (It.HasNextCharacter())
		{
			// Newline breaks word in both modes.
			if (bInsideWord)
			{
				bInsideWord = false;
				OutWordStartIndices.Add(CurrentWordStartIndex);
				OutWordCharacterCounts.Add(CurrentWordCharCount);
			}

			// Mark the start of the next line
			OutLineStartIndices.Add(OutUnicode.Num());
		}
	}

	if (bInsideWord)
	{
		OutWordStartIndices.Add(CurrentWordStartIndex);
		OutWordCharacterCounts.Add(CurrentWordCharCount);
	}

	// Derive per-line character counts from the line start indices.
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

	// Register GetWordTrailingWhitespaceCount
	FNiagaraFunctionSignature SigWordTrailingSpace;
	SigWordTrailingSpace.Name = GetWordTrailingWhitespaceCountName;
#if WITH_EDITORONLY_DATA
	SigWordTrailingSpace.Description = LOCTEXT("GetWordTrailingWhitespaceCountDesc", "Returns the number of whitespace characters after the specified word index.");
#endif
	SigWordTrailingSpace.bMemberFunction = true;
	SigWordTrailingSpace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigWordTrailingSpace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("WordIndex")));
	SigWordTrailingSpace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TrailingWhitespaceCount")));
	OutFunctions.Add(SigWordTrailingSpace);

	// Register GetFilterWhitespaceCharacters
	FNiagaraFunctionSignature SigFilterWhitespace;
	SigFilterWhitespace.Name = GetFilterWhitespaceCharactersName;
#if WITH_EDITORONLY_DATA
	SigFilterWhitespace.Description = LOCTEXT("GetFilterWhitespaceCharactersDesc", "Returns 1 if this data interface is filtering whitespace characters, 0 otherwise.");
#endif
	SigFilterWhitespace.bMemberFunction = true;
	SigFilterWhitespace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigFilterWhitespace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("FilterWhitespaceCharacters")));
	OutFunctions.Add(SigFilterWhitespace);

	// Register GetCharacterCountInWordRange
	FNiagaraFunctionSignature SigCharCountInWordRange;
	SigCharCountInWordRange.Name = GetCharacterCountInWordRangeName;
#if WITH_EDITORONLY_DATA
	SigCharCountInWordRange.Description = LOCTEXT("GetCharacterCountInWordRangeDesc", "Returns the total number of characters between StartWordIndex and EndWordIndex (inclusive). When whitespace filtering is disabled, trailing whitespace for each word in the range is also included.");
#endif
	SigCharCountInWordRange.bMemberFunction = true;
	SigCharCountInWordRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigCharCountInWordRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("StartWordIndex")));
	SigCharCountInWordRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EndWordIndex")));
	SigCharCountInWordRange.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterCountInRange")));
	OutFunctions.Add(SigCharCountInWordRange);

	// Register GetCharacterCountInLineRange
	FNiagaraFunctionSignature SigCharCountInLineRange;
	SigCharCountInLineRange.Name = GetCharacterCountInLineRangeName;
#if WITH_EDITORONLY_DATA
	SigCharCountInLineRange.Description = LOCTEXT("GetCharacterCountInLineRangeDesc", "Returns the total number of characters between StartLineIndex and EndLineIndex (inclusive).");
#endif
	SigCharCountInLineRange.bMemberFunction = true;
	SigCharCountInLineRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigCharCountInLineRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("StartLineIndex")));
	SigCharCountInLineRange.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EndLineIndex")));
	SigCharCountInLineRange.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterCountInLineRange")));
	OutFunctions.Add(SigCharCountInLineRange);

	// Register GetCharacterSpriteSize
	FNiagaraFunctionSignature SigSpriteSize;
	SigSpriteSize.Name = GetCharacterSpriteSizeName;
#if WITH_EDITORONLY_DATA
	SigSpriteSize.Description = LOCTEXT("GetCharacterSpriteSizeDesc", "Returns the sprite size in pixels (Width, Height) for the given character index.");
#endif
	SigSpriteSize.bMemberFunction = true;
	SigSpriteSize.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Font UV Information interface")));
	SigSpriteSize.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CharacterIndex")));
	SigSpriteSize.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SpriteWidth")));
	SigSpriteSize.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SpriteHeight")));
	OutFunctions.Add(SigSpriteSize);
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
	if (RTData && RTData->CharacterTextureUvsBuffer.SRV.IsValid())
	{
		ShaderParameters->CharacterTextureUvs = RTData->CharacterTextureUvsBuffer.SRV;
		ShaderParameters->CharacterSpriteSizes = RTData->CharacterSpriteSizesBuffer.SRV.IsValid() ? RTData->CharacterSpriteSizesBuffer.SRV : DataInterfaceProxy.DefaultFloatBuffer.SRV;
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
		ShaderParameters->bFilterWhitespaceCharactersValue = RTData->bFilterWhitespaceCharactersValue;
	}
	else
	{
		ShaderParameters->CharacterTextureUvs = DataInterfaceProxy.DefaultUVRectsBuffer.SRV;
		ShaderParameters->CharacterSpriteSizes = DataInterfaceProxy.DefaultFloatBuffer.SRV;
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
		ShaderParameters->bFilterWhitespaceCharactersValue = bFilterWhitespaceCharacters ? 1u : 0u;
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
		DestTyped->bFilterWhitespaceCharacters = bFilterWhitespaceCharacters;
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
		&& OtherTyped->bFilterWhitespaceCharacters == bFilterWhitespaceCharacters;
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
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetCharacterPositionName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterPositionVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextCharacterCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextLineCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextLineCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetLineCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetLineCharacterCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetTextWordCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetTextWordCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetWordCharacterCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetWordCharacterCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetWordTrailingWhitespaceCountName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetWordTrailingWhitespaceCountVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetFilterWhitespaceCharactersName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetFilterWhitespaceCharactersVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetCharacterCountInWordRangeName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterCountInWordRangeVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetCharacterCountInLineRangeName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterCountInLineRangeVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
	}
	else if (BindingInfo.Name == GetCharacterSpriteSizeName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->GetCharacterSpriteSizeVM(Context); });
		//UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI: GetVMExternalFunction - Bound function '%s'"), *BindingInfo.Name.ToString());
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
	const TArray<FVector4>& TextureUvs = InstData.Get()->CharacterTextureUvs;
	const int32 NumRects = TextureUvs.Num();

	UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI: GetCharacterUVVM - NumInstances=%d, CharacterTextureUvs.Num=%d"),
		Context.GetNumInstances(), NumRects);

	// Iterate over the particles
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		const int32 CharacterIndex = InCharacterIndex.GetAndAdvance();

		const int32 UnicodeIndex = (Unicode.IsValidIndex(CharacterIndex)) ? Unicode[CharacterIndex] : -1;

		// Bounds check
		if (NumRects > 0 && UnicodeIndex >= 0 && UnicodeIndex < NumRects)
		{
			const FVector4& UVRect = TextureUvs[UnicodeIndex];
			OutUSize.SetAndAdvance(UVRect.X);
			OutVSize.SetAndAdvance(UVRect.Y);
			OutUStart.SetAndAdvance(UVRect.Z);
			OutVStart.SetAndAdvance(UVRect.W);

			if (i < 4)
			{
				UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI: VM idx=%d UnicodeIndex=%d -> CharacterTextureUV=[%s]"),
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

static int32 GetLineCharacterCountInternal(const FNDIFontUVInfoInstanceData* Data, int32 LineIndex)
{
	const TArray<int32>& LineCharacterCounts = Data->LineCharacterCounts;
	const int32 NumLines = Data->LineStartIndices.Num();

	if (NumLines > 0 && LineIndex >= 0 && LineIndex < NumLines && LineCharacterCounts.IsValidIndex(LineIndex))
	{
		return LineCharacterCounts[LineIndex];
	}

	return 0;
}

void UNTPDataInterface::GetLineCharacterCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InLineIndex(Context);
	FNDIOutputParam<int32> OutLineCharacterCount(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 LineIndex = InLineIndex.GetAndAdvance();

		const FNDIFontUVInfoInstanceData* Data = InstData.Get();
		const int32 CharCount = GetLineCharacterCountInternal(Data, LineIndex);
		OutLineCharacterCount.SetAndAdvance(CharCount);
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

static int32 GetWordCharacterCountInternal(const FNDIFontUVInfoInstanceData* Data, int32 WordIndex)
{
	const TArray<int32>& WordCharacterCounts = Data->WordCharacterCounts;
	const int32 NumWords = Data->WordStartIndices.Num();

	if (NumWords > 0 && WordIndex >= 0 && WordIndex < NumWords && WordCharacterCounts.IsValidIndex(WordIndex))
	{
		return WordCharacterCounts[WordIndex];
	}

	return 0;
}

static int32 GetWordTrailingWhitespaceCountInternal(const FNDIFontUVInfoInstanceData* Data, int32 WordIndex)
{
	const TArray<int32>& WordStartIndices = Data->WordStartIndices;
	const TArray<int32>& WordCharacterCounts = Data->WordCharacterCounts;
	const int32 NumWords = WordStartIndices.Num();
	const int32 TotalChars = Data->Unicode.Num();

	if (NumWords > 0 && WordIndex >= 0 && WordIndex < NumWords &&
		WordCharacterCounts.IsValidIndex(WordIndex) && WordStartIndices.IsValidIndex(WordIndex))
	{
		const int32 EndOfWordIndex = WordStartIndices[WordIndex] + WordCharacterCounts[WordIndex];
		int32 NextWordStartIndex = TotalChars;

		if (WordIndex < NumWords - 1 && WordStartIndices.IsValidIndex(WordIndex + 1))
		{
			NextWordStartIndex = WordStartIndices[WordIndex + 1];
		}

		return FMath::Max(0, NextWordStartIndex - EndOfWordIndex);
	}

	return 0;
}

void UNTPDataInterface::GetWordCharacterCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InWordIndex(Context);
	FNDIOutputParam<int32> OutWordCharacterCount(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 WordIndex = InWordIndex.GetAndAdvance();

		const FNDIFontUVInfoInstanceData* Data = InstData.Get();
		const int32 CharCount = GetWordCharacterCountInternal(Data, WordIndex);
		OutWordCharacterCount.SetAndAdvance(CharCount);
	}
}

void UNTPDataInterface::GetWordTrailingWhitespaceCountVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InWordIndex(Context);
	FNDIOutputParam<int32> OutTrailingWhitespaceCount(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 WordIndex = InWordIndex.GetAndAdvance();
		const FNDIFontUVInfoInstanceData* Data = InstData.Get();
		const int32 TrailingSpace = GetWordTrailingWhitespaceCountInternal(Data, WordIndex);
		OutTrailingWhitespaceCount.SetAndAdvance(TrailingSpace);
	}
}

void UNTPDataInterface::GetFilterWhitespaceCharactersVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIOutputParam<bool> OutFilter(Context);

	const bool bValue = InstData.Get()->bFilterWhitespaceCharactersValue;

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutFilter.SetAndAdvance(bValue);
	}
}

void UNTPDataInterface::GetCharacterCountInWordRangeVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InStartWordIndex(Context);
	FNDIInputParam<int32> InEndWordIndex(Context);
	FNDIOutputParam<int32> OutCharacterCountInRange(Context);

	const TArray<int32>& WordStartIndices = InstData.Get()->WordStartIndices;
	const TArray<int32>& WordCharacterCounts = InstData.Get()->WordCharacterCounts;
	const int32 NumWords = WordStartIndices.Num();
	const int32 TotalChars = InstData.Get()->Unicode.Num();
	const bool bFilterWhitespace = InstData.Get()->bFilterWhitespaceCharactersValue;

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 StartWordIndex = InStartWordIndex.GetAndAdvance();
		int32 EndWordIndex = InEndWordIndex.GetAndAdvance();

		int32 TotalInRange = 0;

		if (NumWords > 0 && StartWordIndex >= 0 && StartWordIndex < NumWords)
		{
			const int32 StartIndex = StartWordIndex;
			const int32 EndIndex   = FMath::Clamp(EndWordIndex, 0, NumWords - 1);

			if (StartIndex <= EndIndex)
			{
				for (int32 WordIndex = StartIndex; WordIndex <= EndIndex; ++WordIndex)
				{
					const FNDIFontUVInfoInstanceData* Data = InstData.Get();
					const int32 CharCount = GetWordCharacterCountInternal(Data, WordIndex);
					TotalInRange += CharCount;

					if (!bFilterWhitespace)
					{
						const int32 TrailingSpace = GetWordTrailingWhitespaceCountInternal(Data, WordIndex);
						TotalInRange += TrailingSpace;
					}
				}
			}
		}

		OutCharacterCountInRange.SetAndAdvance(TotalInRange);
	}
}

void UNTPDataInterface::GetCharacterCountInLineRangeVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InStartLineIndex(Context);
	FNDIInputParam<int32> InEndLineIndex(Context);
	FNDIOutputParam<int32> OutCharacterCountInLineRange(Context);

	const int32 NumLines = InstData.Get()->LineStartIndices.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		int32 StartLineIndex = InStartLineIndex.GetAndAdvance();
		int32 EndLineIndex = InEndLineIndex.GetAndAdvance();

		int32 TotalInRange = 0;

		if (NumLines > 0 && StartLineIndex >= 0 && StartLineIndex < NumLines)
		{
			const int32 StartIndex = StartLineIndex;
			const int32 EndIndex   = FMath::Clamp(EndLineIndex, 0, NumLines - 1);

			if (StartIndex <= EndIndex)
			{
				const FNDIFontUVInfoInstanceData* Data = InstData.Get();

				for (int32 LineIndex = StartIndex; LineIndex <= EndIndex; ++LineIndex)
				{
					const int32 CharCount = GetLineCharacterCountInternal(Data, LineIndex);
					TotalInRange += CharCount;
				}
			}
		}

		OutCharacterCountInLineRange.SetAndAdvance(TotalInRange);
	}
}

void UNTPDataInterface::GetCharacterSpriteSizeVM(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFontUVInfoInstanceData> InstData(Context);
	FNDIInputParam<int32> InCharacterIndex(Context);
	FNDIOutputParam<float> OutWidth(Context);
	FNDIOutputParam<float> OutHeight(Context);

	const TArray<int32>& Unicode = InstData.Get()->Unicode;
	const TArray<FVector2f>& SpriteSizes = InstData.Get()->CharacterSpriteSizes;
	const int32 NumSizes = SpriteSizes.Num();

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		const int32 CharacterIndex = InCharacterIndex.GetAndAdvance();

		const int32 UnicodeIndex = (Unicode.IsValidIndex(CharacterIndex)) ? Unicode[CharacterIndex] : -1;

		if (NumSizes > 0 && UnicodeIndex >= 0 && UnicodeIndex < NumSizes)
		{
			const FVector2f& Size = SpriteSizes[UnicodeIndex];
			OutWidth.SetAndAdvance(Size.X);
			OutHeight.SetAndAdvance(Size.Y);
		}
		else
		{
			OutWidth.SetAndAdvance(0.0f);
			OutHeight.SetAndAdvance(0.0f);
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
		|| FunctionInfo.DefinitionName == GetCharacterSpriteSizeName
		|| FunctionInfo.DefinitionName == GetTextCharacterCountName
		|| FunctionInfo.DefinitionName == GetTextLineCountName
		|| FunctionInfo.DefinitionName == GetLineCharacterCountName
		|| FunctionInfo.DefinitionName == GetTextWordCountName
		|| FunctionInfo.DefinitionName == GetWordCharacterCountName
		|| FunctionInfo.DefinitionName == GetWordTrailingWhitespaceCountName
		|| FunctionInfo.DefinitionName == GetFilterWhitespaceCharactersName
		|| FunctionInfo.DefinitionName == GetCharacterCountInWordRangeName
		|| FunctionInfo.DefinitionName == GetCharacterCountInLineRangeName;
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

