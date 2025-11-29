// Property of Lucian Tranc

#pragma once

#include "NiagaraDataInterface.h"
#include "VectorVM.h"
#include "Engine/Font.h"
#include "NTPDataInterface.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraTextParticles, Log, All);

enum class ENiagaraSimTarget : uint8;
struct FNiagaraDataInterfaceGeneratedFunction;
struct FNiagaraFunctionSignature;
struct FVMExternalFunctionBindingInfo;

UENUM(BlueprintType)
enum class ENTPTextVerticalAlignment : uint8
{
	NTP_TVA_Top		UMETA(DisplayName = "Top"),
	NTP_TVA_Center	UMETA(DisplayName = "Center"),
	NTP_TVA_Bottom	UMETA(DisplayName = "Bottom"),
};

UENUM(BlueprintType)
enum class ENTPTextHorizontalAlignment : uint8
{
	NTP_THA_Left	UMETA(DisplayName = "Left"),
	NTP_THA_Center	UMETA(DisplayName = "Center"),
	NTP_THA_Right	UMETA(DisplayName = "Right"),
};

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

	virtual ~FNDIFontUVInfoProxy() override
	{
		DefaultUVRectsBuffer.Release();
		DefaultUIntBuffer.Release();
		DefaultFloatBuffer.Release();
	}

	FRWBufferStructured DefaultUVRectsBuffer;
	FRWBufferStructured DefaultUIntBuffer;
	FRWBufferStructured DefaultFloatBuffer;
	bool bDefaultInitialized = false;

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

	void UpdateData_RT(FNDIFontUVInfoInstanceData* InstanceDataFromGT, const FNiagaraSystemInstanceID& InstanceID, FRHICommandListBase& RHICmdList)
	{
		FRTInstanceData& RTInstance = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);

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
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		UE_LOG(LogNiagaraTextParticles, Log, TEXT("NTP DI Proxy: ConsumePerInstanceDataFromGameThread - Proxy=%p, InstanceID=%llu"), 
			this, (uint64)InstanceID);

		FNDIFontUVInfoInstanceData* InstanceDataFromGT = static_cast<FNDIFontUVInfoInstanceData*>(PerInstanceData);
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		UpdateData_RT(InstanceDataFromGT, InstanceID, RHICmdList);

		// Call the destructor to clean up the GT data
		InstanceDataFromGT->~FNDIFontUVInfoInstanceData();

		UE_LOG(LogNiagaraTextParticles, Verbose, TEXT("NTP DI (RT): ConsumePerInstanceDataFromGameThread - InstanceID=%llu, CharacterTextureUvs.Num=%u"),
			(uint64)InstanceID, SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID).NumRects);
	}

	TMap<FNiagaraSystemInstanceID, FRTInstanceData> SystemInstancesToInstanceData_RT;
};

UCLASS(EditInlineNew, BlueprintType, Category = "Niagara Text Particles Plugin", meta = (DisplayName = "NTP Data Interface"))
class NIAGARATEXTPARTICLES_API UNTPDataInterface : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		// Normalized per-glyph texture UVs (USize, VSize, UStart, VStart), all in 0-1 texture space
		SHADER_PARAMETER_SRV(	StructuredBuffer<float4>,	CharacterTextureUvs)
		// Per-glyph sprite size in pixels, used for layout / particle sizing
		SHADER_PARAMETER_SRV(	StructuredBuffer<float2>,	CharacterSpriteSizes)
		SHADER_PARAMETER(		uint32,						NumRects)
		SHADER_PARAMETER_SRV(	StructuredBuffer<uint>,		TextUnicode)
		SHADER_PARAMETER_SRV(	StructuredBuffer<float2>,	CharacterPositions)
		SHADER_PARAMETER_SRV(	StructuredBuffer<uint>,		LineStartIndices)
		SHADER_PARAMETER_SRV(	StructuredBuffer<uint>,		LineCharacterCounts)
		SHADER_PARAMETER_SRV(	StructuredBuffer<uint>,		WordStartIndices)
		SHADER_PARAMETER_SRV(	StructuredBuffer<uint>,		WordCharacterCounts)
		SHADER_PARAMETER(		uint32,						NumChars)
		SHADER_PARAMETER(		uint32,						NumLines)
		SHADER_PARAMETER(		uint32,						NumWords)
		SHADER_PARAMETER(		uint32,						bFilterWhitespaceCharactersValue)
	END_SHADER_PARAMETER_STRUCT()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Font Asset"))
	UFont* FontAsset = nullptr;

	// The input text to compute character positions for; converted to Unicode and character positions per instance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Input Text", MultiLine = "true"))
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Horizontal Alignment"))
	ENTPTextHorizontalAlignment HorizontalAlignment = ENTPTextHorizontalAlignment::NTP_THA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Vertical Alignment"))
	ENTPTextVerticalAlignment VerticalAlignment = ENTPTextVerticalAlignment::NTP_TVA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Filter Whitespace Characters"))
	bool bFilterWhitespaceCharacters = true;

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	//UNiagaraDataInterface Interface

	void GetCharacterUVVM(FVectorVMExternalFunctionContext& Context);
	void GetCharacterPositionVM(FVectorVMExternalFunctionContext& Context);
	void GetTextCharacterCountVM(FVectorVMExternalFunctionContext& Context);
	void GetTextLineCountVM(FVectorVMExternalFunctionContext& Context);
	void GetLineCharacterCountVM(FVectorVMExternalFunctionContext& Context);
	void GetTextWordCountVM(FVectorVMExternalFunctionContext& Context);
	void GetWordCharacterCountVM(FVectorVMExternalFunctionContext& Context);
	void GetWordTrailingWhitespaceCountVM(FVectorVMExternalFunctionContext& Context);
	void GetFilterWhitespaceCharactersVM(FVectorVMExternalFunctionContext& Context);
	void GetCharacterCountInWordRangeVM(FVectorVMExternalFunctionContext& Context);
	void GetCharacterCountInLineRangeVM(FVectorVMExternalFunctionContext& Context);
	void GetCharacterSpriteSizeVM(FVectorVMExternalFunctionContext& Context);

	/** Returns the render thread proxy for this data interface. */
	FNDIFontUVInfoProxy* GetFontProxy() const { return static_cast<FNDIFontUVInfoProxy*>(Proxy.Get()); }

private:
	static const FName GetCharacterUVName;
	static const FName GetCharacterPositionName;
	static const FName GetTextCharacterCountName;
	static const FName GetTextLineCountName;
	static const FName GetLineCharacterCountName;
	static const FName GetTextWordCountName;
	static const FName GetWordCharacterCountName;
	static const FName GetWordTrailingWhitespaceCountName;
	static const FName GetFilterWhitespaceCharactersName;
	static const FName GetCharacterCountInWordRangeName;
	static const FName GetCharacterCountInLineRangeName;
	static const FName GetCharacterSpriteSizeName;

	// Computes per-character positions in local text space using per-glyph sprite sizes in pixels.
	static TArray<FVector2f> GetCharacterPositions(const TArray<FVector2f>& CharacterSpriteSizes, const TArray<int32>& VerticalOffsets, int32 Kerning, FString InputString, ENTPTextHorizontalAlignment XAlignment, ENTPTextVerticalAlignment YAlignment);

	// Extracts per-glyph sprite sizes (pixels), normalized texture UVs, vertical offsets, and global kerning from the font asset.
	static bool GetFontInfo(const UFont* FontAsset, TArray<FVector4>& OutCharacterTextureUvs, TArray<FVector2f>& OutCharacterSpriteSizes, TArray<int32>& OutVerticalOffsets, int32& OutKerning);

	static void ProcessText(
		const FString& InputText,
		const TArray<FVector2f>& CharacterPositionsUnfiltered,
		const bool bFilterWhitespace,
		TArray<int32>& OutUnicode,
		TArray<FVector2f>& OutCharacterPositions,
		TArray<int32>& OutLineStartIndices,
		TArray<int32>& OutLineCharacterCounts,
		TArray<int32>& OutWordStartIndices,
		TArray<int32>& OutWordCharacterCounts
	);

};

