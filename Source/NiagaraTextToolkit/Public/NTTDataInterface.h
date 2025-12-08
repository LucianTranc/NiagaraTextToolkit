// Property of Lucian Tranc

#pragma once

#include "NiagaraDataInterface.h"
#include "VectorVM.h"
#include "Engine/Font.h"
#include "NTTDataInterface.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraTextToolkit, Log, All);

enum class ENiagaraSimTarget : uint8;
struct FNiagaraDataInterfaceGeneratedFunction;
struct FNiagaraFunctionSignature;
struct FVMExternalFunctionBindingInfo;

UENUM(BlueprintType)
enum class ENTTTextVerticalAlignment : uint8
{
	NTT_TVA_Top		UMETA(DisplayName = "Top"),
	NTT_TVA_Center	UMETA(DisplayName = "Center"),
	NTT_TVA_Bottom	UMETA(DisplayName = "Bottom"),
};

UENUM(BlueprintType)
enum class ENTTTextHorizontalAlignment : uint8
{
	NTT_THA_Left	UMETA(DisplayName = "Left"),
	NTT_THA_Center	UMETA(DisplayName = "Center"),
	NTT_THA_Right	UMETA(DisplayName = "Right"),
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
	float TotalTextHeight = 0.0f;
	bool bFilterWhitespaceCharactersValue = true;
};

// This proxy is used to safely copy data between game thread and render thread
struct FNDIFontUVInfoProxy : public FNiagaraDataInterfaceProxy
{
	// We manage render-thread buffers explicitly via InitPerInstanceData/UpdateData_RT,
	// so we don't need Niagara's per-frame GT->RT instance data path.
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

	virtual ~FNDIFontUVInfoProxy() override
	{
		PackedBuffer.Release();
	}

	FRWBufferStructured PackedBuffer;
	bool bDefaultInitialized = false;

	struct FRTInstanceData
	{
		FRWBufferStructured PackedBuffer;
		uint32 NumRects = 0;
		uint32 NumChars = 0;
		uint32 NumLines = 0;
		uint32 NumWords = 0;
		uint32 bFilterWhitespaceCharactersValue = 1;
		float TotalTextHeight = 0.0f;
		
		uint32 Offset_UVs = 0;
		uint32 Offset_Sizes = 0;
		uint32 Offset_Unicode = 0;
		uint32 Offset_Positions = 0;
		uint32 Offset_LineStart = 0;
		uint32 Offset_LineCount = 0;
		uint32 Offset_WordStart = 0;
		uint32 Offset_WordCount = 0;

		void Release()
		{
			PackedBuffer.Release();
			NumRects = 0;
			NumChars = 0;
			NumLines = 0;
			NumWords = 0;
			bFilterWhitespaceCharactersValue = 1;
			TotalTextHeight = 0.0f;
		
			Offset_UVs = 0;
			Offset_Sizes = 0;
			Offset_Unicode = 0;
			Offset_Positions = 0;
			Offset_LineStart = 0;
			Offset_LineCount = 0;
			Offset_WordStart = 0;
			Offset_WordCount = 0;
		}
	};

	void EnsureDefaultBuffer(FRHICommandListBase& RHICmdList)
	{
		if (!bDefaultInitialized)
		{
			PackedBuffer.Initialize(RHICmdList, TEXT("NTT_Packed_Default"), sizeof(float), 4, BUF_ShaderResource | BUF_Static);
			float Zeros[4] = { 0, 0, 0, 0 };
			void* Dest = RHICmdList.LockBuffer(PackedBuffer.Buffer, 0, sizeof(float) * 4, RLM_WriteOnly);
			FMemory::Memcpy(Dest, &Zeros, sizeof(float) * 4);
			RHICmdList.UnlockBuffer(PackedBuffer.Buffer);
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

		UE_LOG(LogNiagaraTextToolkit, Verbose, TEXT("NTT DI (RT): ProvidePerInstanceDataForRenderThread - InstanceID=%llu, CharacterTextureUvs.Num=%d"),
			(uint64)SystemInstance, DataForRenderThread->CharacterTextureUvs.Num());
	}

	void UpdateData_RT(FNDIFontUVInfoInstanceData* InstanceDataFromGT, const FNiagaraSystemInstanceID& InstanceID, FRHICommandListBase& RHICmdList)
	{
		FRTInstanceData& RTInstance = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);

		// Release old data first. This resets all counts and offsets to 0.
		RTInstance.Release();

		// Calculate sizes
		const int32 NumRects = InstanceDataFromGT->CharacterTextureUvs.Num();
		const int32 NumChars = InstanceDataFromGT->Unicode.Num();
		const int32 NumLines = InstanceDataFromGT->LineStartIndices.Num();
		const int32 NumWords = InstanceDataFromGT->WordStartIndices.Num();

		RTInstance.NumRects = (uint32)NumRects;
		RTInstance.NumChars = (uint32)NumChars;
		RTInstance.NumLines = (uint32)NumLines;
		RTInstance.NumWords = (uint32)NumWords;
		RTInstance.bFilterWhitespaceCharactersValue = InstanceDataFromGT->bFilterWhitespaceCharactersValue ? 1u : 0u;
		RTInstance.TotalTextHeight = InstanceDataFromGT->TotalTextHeight;

		// Calculate offsets (in floats) directly into the struct
		RTInstance.Offset_UVs = 0;
		uint32 CurrentOffset = RTInstance.Offset_UVs + NumRects * 4;

		RTInstance.Offset_Sizes = CurrentOffset;
		CurrentOffset += NumRects * 2;

		RTInstance.Offset_Unicode = CurrentOffset;
		CurrentOffset += NumChars * 1;

		RTInstance.Offset_Positions = CurrentOffset;
		CurrentOffset += NumChars * 2;

		RTInstance.Offset_LineStart = CurrentOffset;
		CurrentOffset += NumLines * 1;

		RTInstance.Offset_LineCount = CurrentOffset;
		CurrentOffset += NumLines * 1;

		RTInstance.Offset_WordStart = CurrentOffset;
		CurrentOffset += NumWords * 1;

		RTInstance.Offset_WordCount = CurrentOffset;
		CurrentOffset += NumWords * 1;

		const uint32 TotalFloats = FMath::Max(CurrentOffset, 1u);

		// Initialize buffer
		RTInstance.PackedBuffer.Initialize(RHICmdList, TEXT("NTT_PackedBuffer"), sizeof(float), TotalFloats, BUF_ShaderResource | BUF_Static);

		float* DestInfo = (float*)RHICmdList.LockBuffer(RTInstance.PackedBuffer.Buffer, 0, TotalFloats * sizeof(float), RLM_WriteOnly);

		// Helper to safely write data
		if (TotalFloats == 1 && CurrentOffset == 0)
		{
			DestInfo[0] = 0.0f;
		}
		else
		{
			// UVs (float4)
			for (int32 i = 0; i < NumRects; ++i)
			{
				const FVector4& Src = InstanceDataFromGT->CharacterTextureUvs[i];
				int32 Base = RTInstance.Offset_UVs + i * 4;
				DestInfo[Base + 0] = (float)Src.X;
				DestInfo[Base + 1] = (float)Src.Y;
				DestInfo[Base + 2] = (float)Src.Z;
				DestInfo[Base + 3] = (float)Src.W;
			}

			// Sizes (float2)
			for (int32 i = 0; i < NumRects; ++i)
			{
				const FVector2f& Src = InstanceDataFromGT->CharacterSpriteSizes[i];
				int32 Base = RTInstance.Offset_Sizes + i * 2;
				DestInfo[Base + 0] = Src.X;
				DestInfo[Base + 1] = Src.Y;
			}

			// Unicode (int32 -> asfloat)
			for (int32 i = 0; i < NumChars; ++i)
			{
				int32 Src = InstanceDataFromGT->Unicode[i];
				int32 Base = RTInstance.Offset_Unicode + i;
				FMemory::Memcpy(&DestInfo[Base], &Src, sizeof(int32));
			}

			// Positions (float2)
			for (int32 i = 0; i < NumChars; ++i)
			{
				const FVector2f& Src = InstanceDataFromGT->CharacterPositions[i];
				int32 Base = RTInstance.Offset_Positions + i * 2;
				DestInfo[Base + 0] = Src.X;
				DestInfo[Base + 1] = Src.Y;
			}

			// LineStartIndices (int32)
			for (int32 i = 0; i < NumLines; ++i)
			{
				int32 Src = InstanceDataFromGT->LineStartIndices[i];
				int32 Base = RTInstance.Offset_LineStart + i;
				FMemory::Memcpy(&DestInfo[Base], &Src, sizeof(int32));
			}

			// LineCharacterCounts (int32)
			for (int32 i = 0; i < NumLines; ++i)
			{
				int32 Src = InstanceDataFromGT->LineCharacterCounts[i];
				int32 Base = RTInstance.Offset_LineCount + i;
				FMemory::Memcpy(&DestInfo[Base], &Src, sizeof(int32));
			}

			// WordStartIndices (int32)
			for (int32 i = 0; i < NumWords; ++i)
			{
				int32 Src = InstanceDataFromGT->WordStartIndices[i];
				int32 Base = RTInstance.Offset_WordStart + i;
				FMemory::Memcpy(&DestInfo[Base], &Src, sizeof(int32));
			}

			// WordCharacterCounts (int32)
			for (int32 i = 0; i < NumWords; ++i)
			{
				int32 Src = InstanceDataFromGT->WordCharacterCounts[i];
				int32 Base = RTInstance.Offset_WordCount + i;
				FMemory::Memcpy(&DestInfo[Base], &Src, sizeof(int32));
			}
		}

		RHICmdList.UnlockBuffer(RTInstance.PackedBuffer.Buffer);
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		UE_LOG(LogNiagaraTextToolkit, Log, TEXT("NTT DI Proxy: ConsumePerInstanceDataFromGameThread - Proxy=%p, InstanceID=%llu"), 
			this, (uint64)InstanceID);

		FNDIFontUVInfoInstanceData* InstanceDataFromGT = static_cast<FNDIFontUVInfoInstanceData*>(PerInstanceData);
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		UpdateData_RT(InstanceDataFromGT, InstanceID, RHICmdList);

		// Call the destructor to clean up the GT data
		InstanceDataFromGT->~FNDIFontUVInfoInstanceData();
	}

	TMap<FNiagaraSystemInstanceID, FRTInstanceData> SystemInstancesToInstanceData_RT;
};

UCLASS(EditInlineNew, BlueprintType, Category = "Niagara Text Toolkit Plugin", meta = (DisplayName = "NTT Data Interface"))
class NIAGARATEXTTOOLKIT_API UNTTDataInterface : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, PackedBuffer)

		SHADER_PARAMETER(uint32, Offset_UVs)
		SHADER_PARAMETER(uint32, Offset_Sizes)
		SHADER_PARAMETER(uint32, Offset_Unicode)
		SHADER_PARAMETER(uint32, Offset_Positions)
		SHADER_PARAMETER(uint32, Offset_LineStart)
		SHADER_PARAMETER(uint32, Offset_LineCount)
		SHADER_PARAMETER(uint32, Offset_WordStart)
		SHADER_PARAMETER(uint32, Offset_WordCount)

		SHADER_PARAMETER(uint32, NumRects)
		SHADER_PARAMETER(uint32, NumChars)
		SHADER_PARAMETER(uint32, NumLines)
		SHADER_PARAMETER(uint32, NumWords)
		SHADER_PARAMETER(uint32, bFilterWhitespaceCharactersValue)
		SHADER_PARAMETER(float, TotalTextHeight)
	END_SHADER_PARAMETER_STRUCT()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Font Asset"))
	UFont* FontAsset = nullptr;

	// The input text to compute character positions for; converted to Unicode and character positions per instance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Input Text", MultiLine = "true"))
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Horizontal Alignment"))
	ENTTTextHorizontalAlignment HorizontalAlignment = ENTTTextHorizontalAlignment::NTT_THA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Vertical Alignment"))
	ENTTTextVerticalAlignment VerticalAlignment = ENTTTextVerticalAlignment::NTT_TVA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Vertical Offset"))
	float VerticalOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Kerning Offset"))
	float KerningOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (DisplayName = "Whitespace Width Multiplier"))
	float WhitespaceWidthMultiplier = 1.0f;

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
	void GetTextHeightVM(FVectorVMExternalFunctionContext& Context);

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
	static const FName GetTextHeightName;

	// Computes per-character positions in local text space using per-glyph sprite sizes in pixels.
	static TArray<FVector2f> GetCharacterPositions(const TArray<FVector2f>& CharacterSpriteSizes, const TArray<int32>& VerticalOffsets, int32 Kerning, float ExtraVerticalOffset, float ExtraKerningOffset, float WhitespaceWidthMultiplier, FString InputString, ENTTTextHorizontalAlignment XAlignment, ENTTTextVerticalAlignment YAlignment, float& OutTotalHeight);

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
