// clang-format off
#include <windows.h>
#include "yuv_encoder_win.h"
#include <codecapi.h>
#include <strmif.h>
// clang-format on

// Media Foundationライブラリをリンク
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

// H.264エンコーダーのCLSIDを定義
static const GUID CLSID_CMSH264EncoderMFT = 
    { 0x6ca50344, 0x051a, 0x4ded, { 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35 } };

// 簡素化されたエラーチェック用マクロ
#define CHECK_HR(hr, msg) if (FAILED(hr)) { \
    printf("%s error: 0x%08X\n", msg, hr); \
    return hr; \
}

// GenerateTestFrameメソッドをNV12形式に修正
void GenerateTestFrame(std::vector<BYTE>& buffer, UINT32 width, UINT32 height, UINT32 frameIndex)
{
    // NV12形式のサイズを計算 (YプレーンとUVプレーン)
    UINT32 ySize = width * height;
    buffer.resize(ySize + (ySize / 2)); // Y + UV
    
    // Yプレーン（輝度）- 動くグラデーションパターン
    for (UINT32 y = 0; y < height; y++) {
        for (UINT32 x = 0; x < width; x++) {
            // フレーム番号に基づいて変化するパターン
            BYTE value = static_cast<BYTE>((x + y + frameIndex * 5) % 256);
            buffer[y * width + x] = value;
        }
    }

    // UVプレーン (交互にUとV) - 固定値で灰色設定
    BYTE* uvPlane = buffer.data() + ySize;
    for (UINT32 i = 0; i < ySize / 2; i += 2) {
        uvPlane[i] = 128;     // U値 (128 = 無彩色)
        uvPlane[i + 1] = 128; // V値 (128 = 無彩色)
    }
}


// IMFSampleからNALユニットを抽出する関数
HRESULT ExtractNalUnitsFromSample(IMFSample* pSample, std::vector<std::vector<BYTE>>& outputNalUnits)
{
    HRESULT hr = S_OK;
    DWORD bufferCount = 0;
    
    if (!pSample) {
        return E_INVALIDARG;
    }
    
    hr = pSample->GetBufferCount(&bufferCount);
    CHECK_HR(hr, "GetBufferCount");
    
    printf("Extracting NAL units (buffer count: %d)\n", bufferCount);
    
    for (DWORD i = 0; i < bufferCount; i++) {
        IMFMediaBuffer* pBuffer = NULL;
        hr = pSample->GetBufferByIndex(i, &pBuffer);
        CHECK_HR(hr, "GetBufferByIndex");
        
        BYTE* pData = NULL;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        
        hr = pBuffer->Lock(&pData, &maxLength, &currentLength);
        CHECK_HR(hr, "Lock");
        
        // NALユニットを抽出
        if (currentLength > 0 && pData != NULL) {
            // 新しいNALユニット用のvector作成
            std::vector<BYTE> nalUnit(pData, pData + currentLength);
            // pDataの先頭の16バイトを表示
            printf("First 16 bytes of pData: ");
            for (int j = 0; j < 16 && j < currentLength; j++) {
                printf("%02X ", pData[j]);
            }
            // pic_typeを表示
            UINT32 picType = 0;
            IMFAttributes* pAttributes = nullptr;
            hr = pSample->QueryInterface(IID_PPV_ARGS(&pAttributes));
            if (SUCCEEDED(hr)) {
                hr = pAttributes->GetUINT32(MFSampleExtension_CleanPoint, &picType);
                if (SUCCEEDED(hr)) {
                    const char* picTypeStr = (picType == 1) ? "I-frame" : (picType == 0) ? "P-frame" : "B-frame";
                    printf("pic_type: %s\n", picTypeStr);
                }
                pAttributes->Release();
            }
            printf("\n");
            if (nalUnit.size() > 5) {
                nalUnit.erase(nalUnit.begin(), nalUnit.begin() + 5);
            }
            outputNalUnits.push_back(nalUnit);
            
            printf("  - NAL unit extracted: %d bytes\n", currentLength);
        }
        
        hr = pBuffer->Unlock();
        CHECK_HR(hr, "Unlock");
        
        if (pBuffer) {
            pBuffer->Release();
            pBuffer = NULL;
        }
    }
    
    return hr;
}

// エンコーダーを初期化する関数
HRESULT InitializeEncoder(NalEncoder* pEncoder)
{
    HRESULT hr = S_OK;
    
    // 構造体の初期化
    pEncoder->pEncoder = NULL;
    pEncoder->pInputType = NULL;
    pEncoder->pOutputType = NULL;
    pEncoder->pInputSample = NULL;
    pEncoder->pInputBuffer = NULL;
    pEncoder->frameCount = 0;
    
    // デフォルトパラメータ設定
#if 1
    pEncoder->width = 1920;
    pEncoder->height = 1088;//8の倍数にする必要がある
#else
pEncoder->width = 640;
pEncoder->height = 480;
#endif
    pEncoder->frameRateNum = 30;
    pEncoder->frameRateDenom = 1;
    pEncoder->bitrate = 1500000; // 1.5 Mbps
    
    // NAL出力ファイルを開く
    
    // Media Foundationの初期化
    hr = MFStartup(MF_VERSION);
    CHECK_HR(hr, "MFStartup");
    
    // 使用可能なH.264エンコーダーの列挙（デバッグ情報）
    IMFActivate** ppActivate = NULL;
    UINT32 count = 0;
    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, 0, NULL, &info, &ppActivate, &count);
    
    if (SUCCEEDED(hr)) {
        printf("Found %d video encoders\n", count);
        for (UINT32 i = 0; i < count; i++) {
            LPWSTR friendlyName = NULL;
            if (SUCCEEDED(ppActivate[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, NULL))) {
                printf("Encoder %d: ", i);
                wprintf(L"%s\n", friendlyName);
                CoTaskMemFree(friendlyName);
            }
            ppActivate[i]->Release();
        }
        CoTaskMemFree(ppActivate);
    } else {
        printf("Enumerating encoders failed: 0x%08X\n", hr);
    }
    
    // H.264エンコーダートランスフォームの作成
    hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER,
                           IID_IMFTransform, (void**)&pEncoder->pEncoder);
    CHECK_HR(hr, "CoCreateInstance H.264 Encoder");
    
    // 出力メディアタイプの設定
    hr = MFCreateMediaType(&pEncoder->pOutputType);
    CHECK_HR(hr, "MFCreateMediaType for output");
    
    hr = pEncoder->pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set output major type");
    
    hr = pEncoder->pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    CHECK_HR(hr, "Set output subtype");
    
    hr = pEncoder->pOutputType->SetUINT32(MF_MT_AVG_BITRATE, pEncoder->bitrate);
    CHECK_HR(hr, "Set output bitrate");
    
    hr = pEncoder->pOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    CHECK_HR(hr, "Set output interlace mode");
    
    hr = MFSetAttributeSize(pEncoder->pOutputType, MF_MT_FRAME_SIZE, pEncoder->width, pEncoder->height);
    CHECK_HR(hr, "Set output frame size");
    
    hr = MFSetAttributeRatio(pEncoder->pOutputType, MF_MT_FRAME_RATE, 
                             pEncoder->frameRateNum, pEncoder->frameRateDenom);
    CHECK_HR(hr, "Set output frame rate");
    
    hr = MFSetAttributeRatio(pEncoder->pOutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    CHECK_HR(hr, "Set output pixel aspect ratio");
    
    // 出力タイプをエンコーダに設定
    hr = pEncoder->pEncoder->SetOutputType(0, pEncoder->pOutputType, 0);
    CHECK_HR(hr, "SetOutputType");
    
    // 入力メディアタイプの設定 - より詳細な設定を追加
    hr = MFCreateMediaType(&pEncoder->pInputType);
    CHECK_HR(hr, "MFCreateMediaType for input");
    
    hr = pEncoder->pInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set input major type");
    
    // NV12フォーマットを使用（より広くサポートされている）
    hr = pEncoder->pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    CHECK_HR(hr, "Set input subtype");
    
    hr = pEncoder->pInputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    CHECK_HR(hr, "Set input interlace mode");
    
    hr = MFSetAttributeSize(pEncoder->pInputType, MF_MT_FRAME_SIZE, pEncoder->width, pEncoder->height);
    CHECK_HR(hr, "Set input frame size");
    
    hr = MFSetAttributeRatio(pEncoder->pInputType, MF_MT_FRAME_RATE, 
                             pEncoder->frameRateNum, pEncoder->frameRateDenom);
    CHECK_HR(hr, "Set input frame rate");
    
    hr = MFSetAttributeRatio(pEncoder->pInputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    CHECK_HR(hr, "Set input pixel aspect ratio");
    
    // 重要: デフォルトストライドの設定
    hr = pEncoder->pInputType->SetUINT32(MF_MT_DEFAULT_STRIDE, pEncoder->width);
    CHECK_HR(hr, "Set input default stride");
    
    // 入力タイプをエンコーダに設定
    hr = pEncoder->pEncoder->SetInputType(0, pEncoder->pInputType, 0);
    CHECK_HR(hr, "SetInputType");
    
    // エンコーダの処理開始
    hr = pEncoder->pEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    CHECK_HR(hr, "ProcessMessage BEGIN_STREAMING");
    
    // 入力サンプル用のバッファを作成 - NV12のサイズ計算
    UINT32 nv12Size = pEncoder->width * pEncoder->height * 3 / 2;  // NV12のサイズ計算
    
    hr = MFCreateSample(&pEncoder->pInputSample);
    CHECK_HR(hr, "MFCreateSample");
    
    hr = MFCreateMemoryBuffer(nv12Size, &pEncoder->pInputBuffer);
    CHECK_HR(hr, "MFCreateMemoryBuffer");
    
    hr = pEncoder->pInputSample->AddBuffer(pEncoder->pInputBuffer);
    CHECK_HR(hr, "AddBuffer");
    
    printf("Encoder initialized: %dx%d @ %d fps\n", 
           pEncoder->width, pEncoder->height, 
           pEncoder->frameRateNum / pEncoder->frameRateDenom);
    
    return hr;
}

// フレームをエンコードして、NALユニットを取得する関数
HRESULT EncodeFrame(NalEncoder* pEncoder, const std::vector<BYTE>& frameData, std::vector<std::vector<BYTE>>& outputNalUnits)
{
    HRESULT hr = S_OK;
    MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
    DWORD processOutputStatus = 0;
    
    // 入力バッファへのフレームデータのコピー
    BYTE* pData = NULL;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    
    hr = pEncoder->pInputBuffer->Lock(&pData, &maxLength, &currentLength);
    CHECK_HR(hr, "Lock input buffer");
    
    // フレームデータをコピー
    if (frameData.size() <= maxLength) {
        memcpy(pData, frameData.data(), frameData.size());
        // 型変換の警告を修正（size_tからDWORDへの変換）
        hr = pEncoder->pInputBuffer->SetCurrentLength(static_cast<DWORD>(frameData.size()));
    } else {
        hr = E_INVALIDARG;
        printf("Frame data too large for buffer\n");
    }
    
    hr = pEncoder->pInputBuffer->Unlock();
    CHECK_HR(hr, "Unlock input buffer");
    
    // タイムスタンプの設定（フレーム番号に基づく）
    LONGLONG timestamp = pEncoder->frameCount * 
                          (10000000LL * pEncoder->frameRateDenom / pEncoder->frameRateNum);
    
    hr = pEncoder->pInputSample->SetSampleTime(timestamp);
    CHECK_HR(hr, "SetSampleTime");
    
    LONGLONG duration = 10000000LL * pEncoder->frameRateDenom / pEncoder->frameRateNum;
    hr = pEncoder->pInputSample->SetSampleDuration(duration);
    CHECK_HR(hr, "SetSampleDuration");
    
    // フレームをエンコーダーに渡す
    hr = pEncoder->pEncoder->ProcessInput(0, pEncoder->pInputSample, 0);
    CHECK_HR(hr, "ProcessInput");
    
    // 出力サンプルの取得
    IMFSample* pOutSample = NULL;
    IMFMediaBuffer* pOutBuffer = NULL;
    
    // 修正：出力バッファを作成してサンプルに追加（E_INVALIDARGエラーの修正）
    hr = MFCreateSample(&pOutSample);
    CHECK_HR(hr, "MFCreateSample for output");
    
    // 出力バッファサイズは十分大きく設定（NV12サイズより大きく）
    hr = MFCreateMemoryBuffer(pEncoder->width * pEncoder->height * 2, &pOutBuffer);
    CHECK_HR(hr, "MFCreateMemoryBuffer for output");
    
    hr = pOutSample->AddBuffer(pOutBuffer);
    CHECK_HR(hr, "AddBuffer to output sample");
    
    if (pOutBuffer) {
        pOutBuffer->Release();
        pOutBuffer = NULL;
    }
    
    outputDataBuffer.dwStreamID = 0;
    outputDataBuffer.pSample = pOutSample;
    outputDataBuffer.dwStatus = 0;
    outputDataBuffer.pEvents = NULL;
    
    // 出力データを格納するベクターをクリア
    outputNalUnits.clear();
    
    // エンコード結果を取得（複数のNALユニットが出力される可能性あり）
    do {
        hr = pEncoder->pEncoder->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
        
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // さらに入力が必要な場合（出力がない場合）
            printf("No output available for this frame\n");
            hr = S_OK;
            break;
        } else if (SUCCEEDED(hr)) {
            // NALユニットを取得してvectorに追加
            hr = ExtractNalUnitsFromSample(outputDataBuffer.pSample, outputNalUnits);
            CHECK_HR(hr, "ExtractNalUnitsFromSample");
        } else if (hr == E_INVALIDARG) {
            // 無効な引数エラーの詳細情報を表示（デバッグ用）
            printf("E_INVALIDARG error - Check buffer configuration, StreamID: %d, Status: 0x%08X\n", 
                   outputDataBuffer.dwStreamID, outputDataBuffer.dwStatus);
            break;
        } else {
            // その他のエラー
            CHECK_HR(hr, "ProcessOutput");
            break;
        }
    } while (SUCCEEDED(hr));
    
    // リソース解放
    if (pOutSample) {
        pOutSample->Release();
        pOutSample = NULL;
    }
    
    // フレームカウントをインクリメント
    pEncoder->frameCount++;
    
    return hr;
}

/**
 * FlushEncoder: Flush後のNALユニットをallNalUnitsに追加する
 */
HRESULT FlushEncoder(NalEncoder* pEncoder, std::vector<std::vector<BYTE>>& allNalUnits)
{
    HRESULT hr = S_OK;
    if (!pEncoder || !pEncoder->pEncoder) return E_POINTER;

    // フラッシュのためにストリーミング終了を通知
    pEncoder->pEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    pEncoder->pEncoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);

    // Flush後の出力回収
    while (true) {
        MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
        DWORD processOutputStatus = 0;
        IMFSample* pOutSample = nullptr;
        IMFMediaBuffer* pOutBuffer = nullptr;

        HRESULT hrSample = MFCreateSample(&pOutSample);
        if (FAILED(hrSample)) break;
        HRESULT hrBuffer = MFCreateMemoryBuffer(pEncoder->width * pEncoder->height * 2, &pOutBuffer);
        if (FAILED(hrBuffer)) {
            pOutSample->Release();
            break;
        }
        pOutSample->AddBuffer(pOutBuffer);

        outputDataBuffer.dwStreamID = 0;
        outputDataBuffer.pSample = pOutSample;

        HRESULT hrOut = pEncoder->pEncoder->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
        if (hrOut == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // もう出力はない
            pOutBuffer->Release();
            pOutSample->Release();
            break;
        } else if (SUCCEEDED(hrOut)) {
            // NALユニットを抽出してallNalUnitsに追加
            std::vector<std::vector<BYTE>> nalUnits;
            ExtractNalUnitsFromSample(outputDataBuffer.pSample, nalUnits);
            for (auto& nalu : nalUnits) {
                allNalUnits.push_back(std::move(nalu));
            }
        }
        if (pOutBuffer) pOutBuffer->Release();
        if (pOutSample) pOutSample->Release();
    }
    return hr;
}

// エンコーダーリソースを解放する関数
HRESULT ShutdownEncoder(NalEncoder* pEncoder)
{
    HRESULT hr = S_OK;
    
    // NAL出力ファイルを閉じる
    
    // リソースの解放
    if (pEncoder->pInputBuffer) {
        pEncoder->pInputBuffer->Release();
        pEncoder->pInputBuffer = NULL;
    }
    
    if (pEncoder->pInputSample) {
        pEncoder->pInputSample->Release();
        pEncoder->pInputSample = NULL;
    }
    
    if (pEncoder->pInputType) {
        pEncoder->pInputType->Release();
        pEncoder->pInputType = NULL;
    }
    
    if (pEncoder->pOutputType) {
        pEncoder->pOutputType->Release();
        pEncoder->pOutputType = NULL;
    }
    
    if (pEncoder->pEncoder) {
        pEncoder->pEncoder->Release();
        pEncoder->pEncoder = NULL;
    }
    
    // Media Foundationのシャットダウン
    hr = MFShutdown();
    
    printf("Encoder shutdown complete. Processed %llu frames.\n", pEncoder->frameCount);
    
    return hr;
}
