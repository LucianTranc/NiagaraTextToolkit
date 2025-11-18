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

UCLASS(EditInlineNew, BlueprintType, Category = "Niagara Text Particles Plugin", meta = (DisplayName = "NTP Data Interface"))
class NIAGARATEXTPARTICLES_API UNTPDataInterface : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_SRV(	Buffer<float4>,				UVRects)
		SHADER_PARAMETER(		uint32,						NumRects)
		SHADER_PARAMETER_SRV(	Buffer<uint>,				TextUnicode)
		SHADER_PARAMETER_SRV(	Buffer<float2>,				CharacterPositions)
		SHADER_PARAMETER_SRV(	Buffer<uint>,				LineStartIndices)
		SHADER_PARAMETER_SRV(	Buffer<uint>,				LineCharacterCounts)
		SHADER_PARAMETER_SRV(	Buffer<uint>,				WordStartIndices)
		SHADER_PARAMETER_SRV(	Buffer<uint>,				WordCharacterCounts)
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Horizontal Alignment"))
	ENTPTextHorizontalAlignment HorizontalAlignment = ENTPTextHorizontalAlignment::NTP_THA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Vertical Alignment"))
	ENTPTextVerticalAlignment VerticalAlignment = ENTPTextVerticalAlignment::NTP_TVA_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Filter Whitespace Characters"))
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

	static TArray<FVector2f> GetCharacterPositions(const TArray<FVector4>& UVRects, FString InputString, ENTPTextHorizontalAlignment XAlignment, ENTPTextVerticalAlignment YAlignment);
	static TArray<FVector4> GetUVRectsFromFont(const UFont* FontAsset);
	static void BuildHorizontalLineMetrics(const TArray<FVector4>& UVRects, const TArray<TArray<int32>>& Lines, TArray<TArray<float>>& OutCumulativeWidthsPerCharacter);
	static void BuildVerticalLineMetrics(const TArray<FVector4>& UVRects, const TArray<TArray<int32>>& Lines, TArray<float>& OutCumulativeHeightsPerLine, float& OutLineHeight);
	static TArray<float> GetHorizontalPositionsLeftAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);
	static TArray<float> GetHorizontalPositionsCenterAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);
	static TArray<float> GetHorizontalPositionsRightAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);
	static TArray<float> GetVerticalPositionsTopAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);
	static TArray<float> GetVerticalPositionsCenterAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);
	static TArray<float> GetVerticalPositionsBottomAligned(const TArray<FVector4>& UVRects, const TArray<int32>& Unicode, const TArray<TArray<int32>>& Lines);

	static void ProcessTextWithWhitespace(
		const FString& InputText,
		const TArray<FVector2f>& CharacterPositionsUnfiltered,
		TArray<int32>& OutUnicode,
		TArray<FVector2f>& OutCharacterPositions,
		TArray<int32>& OutLineStartIndices,
		TArray<int32>& OutLineCharacterCounts,
		TArray<int32>& OutWordStartIndices,
		TArray<int32>& OutWordCharacterCounts
	);

	static void ProcessTextWithoutWhitespace(
		const FString& InputText,
		const TArray<FVector2f>& CharacterPositionsUnfiltered,
		TArray<int32>& OutUnicode,
		TArray<FVector2f>& OutCharacterPositions,
		TArray<int32>& OutLineStartIndices,
		TArray<int32>& OutLineCharacterCounts,
		TArray<int32>& OutWordStartIndices,
		TArray<int32>& OutWordCharacterCounts
	);

};

