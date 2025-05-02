#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <string>
// DShowヘッダー（必要ない場合は削除可能）
#include <dshow.h>

// Media Foundationライブラリをリンク
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
// strmiids.libが必要ない場合は削除可能
#pragma comment(lib, "strmiids.lib")

// H.264エンコーダーのCLSIDを定義
static const GUID CLSID_CMSH264EncoderMFT = 
    { 0x6ca50344, 0x051a, 0x4ded, { 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35 } };

// H.264デコーダーのCLSIDを定義
static const GUID CLSID_CMSH264DecoderMFT = 
    { 0x62CE7E72, 0x4C71, 0x4d20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

// 簡素化されたエラーチェック用マクロ
#define CHECK_HR(hr, msg) if (FAILED(hr)) { \
    printf("%s error: 0x%08X\n", msg, hr); \
    return hr; \
}

// NALエンコーダー構造体
struct NalEncoder {
    IMFTransform* pEncoder;            // H.264エンコーダートランスフォーム
    IMFMediaType* pInputType;          // 入力メディアタイプ
    IMFMediaType* pOutputType;         // 出力メディアタイプ
    IMFSample* pInputSample;           // 入力サンプル
    IMFMediaBuffer* pInputBuffer;      // 入力バッファ
    
    UINT32 width;                      // 映像幅
    UINT32 height;                     // 映像高さ
    UINT32 frameRateNum;               // フレームレート分子
    UINT32 frameRateDenom;             // フレームレート分母
    UINT32 bitrate;                    // ビットレート
    UINT64 frameCount;                 // 処理したフレーム数

    // 出力NALユニットファイル
    std::ofstream nalFile;
    std::string nalFilename;
};

// NALデコーダー構造体
struct NalDecoder {
    IMFTransform* pDecoder;            // H.264デコーダートランスフォーム
    IMFMediaType* pInputType;          // 入力メディアタイプ
    IMFMediaType* pOutputType;         // 出力メディアタイプ
    
    UINT32 width;                      // 映像幅
    UINT32 height;                     // 映像高さ
    UINT64 frameCount;                 // 処理したフレーム数

    // 出力YUVファイル
    std::ofstream yuvFile;
    std::string yuvFilename;
};

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

// NALユニットをファイルに書き込む関数
HRESULT WriteNalToFile(NalEncoder* pEncoder, IMFSample* pSample)
{
    HRESULT hr = S_OK;
    DWORD bufferCount = 0;
    
    hr = pSample->GetBufferCount(&bufferCount);
    CHECK_HR(hr, "GetBufferCount");
    
    printf("Processing: NAL units from frame %llu (buffer count: %d)\n", pEncoder->frameCount, bufferCount);
    
    for (DWORD i = 0; i < bufferCount; i++) {
        IMFMediaBuffer* pBuffer = NULL;
        hr = pSample->GetBufferByIndex(i, &pBuffer);
        CHECK_HR(hr, "GetBufferByIndex");
        
        BYTE* pData = NULL;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        
        hr = pBuffer->Lock(&pData, &maxLength, &currentLength);
        CHECK_HR(hr, "Lock");
        
        // NALユニットをファイルに書き込み
        if (currentLength > 0 && pData != NULL) {
            // NALユニット長をファイルに書き込む (ビッグエンディアン 4バイト)
            BYTE lengthBytes[4];
            lengthBytes[0] = (currentLength >> 24) & 0xFF;
            lengthBytes[1] = (currentLength >> 16) & 0xFF;
            lengthBytes[2] = (currentLength >> 8) & 0xFF;
            lengthBytes[3] = currentLength & 0xFF;
            
            pEncoder->nalFile.write(reinterpret_cast<char*>(lengthBytes), 4);
            
            // NALユニットデータを書き込む
            pEncoder->nalFile.write(reinterpret_cast<char*>(pData), currentLength);
            
            printf("  - NAL unit written: %d bytes\n", currentLength);
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
HRESULT InitializeEncoder(NalEncoder* pEncoder, const char* outputFilename)
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
    pEncoder->width = 640;
    pEncoder->height = 480;
    pEncoder->frameRateNum = 30;
    pEncoder->frameRateDenom = 1;
    pEncoder->bitrate = 1500000; // 1.5 Mbps
    
    // NAL出力ファイルを開く
    pEncoder->nalFilename = outputFilename;
    pEncoder->nalFile.open(outputFilename, std::ios::binary | std::ios::trunc);
    if (!pEncoder->nalFile.is_open()) {
        printf("Failed to create output file: %s\n", outputFilename);
        return E_FAIL;
    }
    
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
    
    printf("Encoder initialized: %dx%d @ %d fps, Output: %s\n", 
           pEncoder->width, pEncoder->height, 
           pEncoder->frameRateNum / pEncoder->frameRateDenom,
           outputFilename);
    
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

// エンコーダーリソースを解放する関数
HRESULT ShutdownEncoder(NalEncoder* pEncoder)
{
    HRESULT hr = S_OK;
    
    // フラッシュのためにストリーミング終了を通知
    if (pEncoder->pEncoder) {
        pEncoder->pEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        pEncoder->pEncoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    
    // NAL出力ファイルを閉じる
    if (pEncoder->nalFile.is_open()) {
        pEncoder->nalFile.close();
        printf("NAL output file closed: %s\n", pEncoder->nalFilename.c_str());
    }
    
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

// デコーダーを初期化する関数
HRESULT InitializeDecoder(NalDecoder* pDecoder, UINT32 width, UINT32 height, const char* outputFilename) {
    HRESULT hr = S_OK;
    
    // 構造体の初期化
    pDecoder->pDecoder = NULL;
    pDecoder->pInputType = NULL;
    pDecoder->pOutputType = NULL;
    pDecoder->frameCount = 0;
    
    // パラメータ設定
    pDecoder->width = width;
    pDecoder->height = height;
    
    // 出力YUVファイルを開く
    pDecoder->yuvFilename = outputFilename;
    pDecoder->yuvFile.open(outputFilename, std::ios::binary | std::ios::trunc);
    if (!pDecoder->yuvFile.is_open()) {
        printf("Failed to create output YUV file: %s\n", outputFilename);
        return E_FAIL;
    }
    
    // H.264デコーダートランスフォームの作成
    hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, NULL, CLSCTX_INPROC_SERVER,
                          IID_IMFTransform, (void**)&pDecoder->pDecoder);
    CHECK_HR(hr, "CoCreateInstance H.264 Decoder");
    
    // 入力メディアタイプの設定
    hr = MFCreateMediaType(&pDecoder->pInputType);
    CHECK_HR(hr, "MFCreateMediaType for decoder input");
    
    hr = pDecoder->pInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set decoder input major type");
    
    hr = pDecoder->pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    CHECK_HR(hr, "Set decoder input subtype");
    
    hr = MFSetAttributeSize(pDecoder->pInputType, MF_MT_FRAME_SIZE, pDecoder->width, pDecoder->height);
    CHECK_HR(hr, "Set decoder input frame size");
    
    // 入力タイプをデコーダに設定
    hr = pDecoder->pDecoder->SetInputType(0, pDecoder->pInputType, 0);
    CHECK_HR(hr, "SetInputType for decoder");
    
    // 出力メディアタイプの設定
    hr = MFCreateMediaType(&pDecoder->pOutputType);
    CHECK_HR(hr, "MFCreateMediaType for decoder output");
    
    hr = pDecoder->pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set decoder output major type");
    
    // NV12フォーマットで出力を設定
    hr = pDecoder->pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    CHECK_HR(hr, "Set decoder output subtype");
    
    hr = MFSetAttributeSize(pDecoder->pOutputType, MF_MT_FRAME_SIZE, pDecoder->width, pDecoder->height);
    CHECK_HR(hr, "Set decoder output frame size");
    
    hr = pDecoder->pOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    CHECK_HR(hr, "Set decoder output interlace mode");
    
    // 出力タイプをデコーダに設定
    hr = pDecoder->pDecoder->SetOutputType(0, pDecoder->pOutputType, 0);
    CHECK_HR(hr, "SetOutputType for decoder");
    
    // デコーダの処理開始
    hr = pDecoder->pDecoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    CHECK_HR(hr, "ProcessMessage BEGIN_STREAMING for decoder");
    
    printf("Decoder initialized: %dx%d, Output: %s\n", 
           pDecoder->width, pDecoder->height, outputFilename);
    
    return hr;
}

// NALユニットをデコードして、YUVフレームとして保存する
HRESULT DecodeNalUnit(NalDecoder* pDecoder, const std::vector<BYTE>& nalData) {
    HRESULT hr = S_OK;
    MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
    DWORD processOutputStatus = 0;
    
    // 入力サンプルの作成
    IMFSample* pInSample = NULL;
    IMFMediaBuffer* pInBuffer = NULL;
    
    hr = MFCreateSample(&pInSample);
    CHECK_HR(hr, "MFCreateSample for decoder input");
    
    hr = MFCreateMemoryBuffer(static_cast<DWORD>(nalData.size()), &pInBuffer);
    CHECK_HR(hr, "MFCreateMemoryBuffer for decoder input");
    
    // NALデータをバッファにコピー
    BYTE* pData = NULL;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    
    hr = pInBuffer->Lock(&pData, &maxLength, &currentLength);
    CHECK_HR(hr, "Lock decoder input buffer");
    
    memcpy(pData, nalData.data(), nalData.size());
    hr = pInBuffer->SetCurrentLength(static_cast<DWORD>(nalData.size()));
    CHECK_HR(hr, "SetCurrentLength for decoder input");
    
    hr = pInBuffer->Unlock();
    CHECK_HR(hr, "Unlock decoder input buffer");
    
    hr = pInSample->AddBuffer(pInBuffer);
    CHECK_HR(hr, "AddBuffer to decoder input sample");
    
    if (pInBuffer) {
        pInBuffer->Release();
        pInBuffer = NULL;
    }
    
    // 入力サンプルをデコーダに渡す
    hr = pDecoder->pDecoder->ProcessInput(0, pInSample, 0);
    if (hr == MF_E_NOTACCEPTING) {
        // デコーダーがまだ入力を受け付けられない場合は、出力処理を行う
        printf("Decoder not accepting input, processing pending output first\n");
    } else {
        CHECK_HR(hr, "ProcessInput for decoder");
    }
    
    if (pInSample) {
        pInSample->Release();
        pInSample = NULL;
    }
    
    // 出力サンプルの取得
    IMFSample* pOutSample = NULL;
    
    // デコード結果を取得
    do {
        // 出力サンプルを作成
        hr = MFCreateSample(&pOutSample);
        CHECK_HR(hr, "MFCreateSample for decoder output");
        
        // 出力バッファを作成
        IMFMediaBuffer* pOutBuffer = NULL;
        UINT32 nv12Size = pDecoder->width * pDecoder->height * 3 / 2; // NV12のサイズ
        
        hr = MFCreateMemoryBuffer(nv12Size, &pOutBuffer);
        CHECK_HR(hr, "MFCreateMemoryBuffer for decoder output");
        
        hr = pOutSample->AddBuffer(pOutBuffer);
        CHECK_HR(hr, "AddBuffer to decoder output sample");
        
        if (pOutBuffer) {
            pOutBuffer->Release();
            pOutBuffer = NULL;
        }
        
        // 出力データバッファの設定
        outputDataBuffer.dwStreamID = 0;
        outputDataBuffer.pSample = pOutSample;
        outputDataBuffer.dwStatus = 0;
        outputDataBuffer.pEvents = NULL;
        
        // 出力処理
        hr = pDecoder->pDecoder->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
        
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // さらに入力が必要な場合（出力がない場合）
            printf("Decoder needs more input\n");
            hr = S_OK;
            if (pOutSample) {
                pOutSample->Release();
                pOutSample = NULL;
            }
            break;
        } else if (SUCCEEDED(hr)) {
            // デコード成功、YUVデータを取得して保存
            IMFMediaBuffer* pBuffer = NULL;
            hr = pOutSample->ConvertToContiguousBuffer(&pBuffer);
            CHECK_HR(hr, "ConvertToContiguousBuffer for decoder output");
            
            BYTE* pYuvData = NULL;
            DWORD yuvMaxLength = 0;
            DWORD yuvCurrentLength = 0;
            
            hr = pBuffer->Lock(&pYuvData, &yuvMaxLength, &yuvCurrentLength);
            CHECK_HR(hr, "Lock decoder output buffer");
            
            // YUVデータをファイルに書き込み
            if (yuvCurrentLength > 0 && pYuvData != NULL) {
                pDecoder->yuvFile.write(reinterpret_cast<char*>(pYuvData), yuvCurrentLength);
                printf("Decoded frame %llu: %d bytes written to YUV file\n", 
                       pDecoder->frameCount, yuvCurrentLength);
                pDecoder->frameCount++;
            }
            
            hr = pBuffer->Unlock();
            CHECK_HR(hr, "Unlock decoder output buffer");
            
            if (pBuffer) {
                pBuffer->Release();
                pBuffer = NULL;
            }
        } else {
            // その他のエラー
            CHECK_HR(hr, "ProcessOutput for decoder");
            break;
        }
        
        if (pOutSample) {
            pOutSample->Release();
            pOutSample = NULL;
        }
    } while (SUCCEEDED(hr));
    
    return hr;
}

// デコーダーリソースを解放する関数
HRESULT ShutdownDecoder(NalDecoder* pDecoder) {
    HRESULT hr = S_OK;
    
    // フラッシュのためにストリーミング終了を通知
    if (pDecoder->pDecoder) {
        pDecoder->pDecoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        pDecoder->pDecoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    
    // YUV出力ファイルを閉じる
    if (pDecoder->yuvFile.is_open()) {
        pDecoder->yuvFile.close();
        printf("YUV output file closed: %s\n", pDecoder->yuvFilename.c_str());
    }
    
    // リソースの解放
    if (pDecoder->pInputType) {
        pDecoder->pInputType->Release();
        pDecoder->pInputType = NULL;
    }
    
    if (pDecoder->pOutputType) {
        pDecoder->pOutputType->Release();
        pDecoder->pOutputType = NULL;
    }
    
    if (pDecoder->pDecoder) {
        pDecoder->pDecoder->Release();
        pDecoder->pDecoder = NULL;
    }
    
    printf("Decoder shutdown complete. Processed %llu frames.\n", pDecoder->frameCount);
    
    return hr;
}

int main()
{
    HRESULT hr = S_OK;
    // COMの初期化
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        printf("CoInitializeEx failed: 0x%08X\n", hr);
        return 1;
    }
    
    // エンコーダーオブジェクトの作成
    NalEncoder encoder;
    
    // エンコーダーの初期化
    hr = InitializeEncoder(&encoder, "output_nal.h264");
    if (FAILED(hr)) {
        printf("Encoder initialization failed: 0x%08X\n", hr);
        CoUninitialize();
        return 1;
    }
    
    // テストフレームの生成とエンコード
    std::vector<BYTE> frameBuffer;
    const UINT32 frameCount = 100;
    
    // すべてのエンコード結果を格納するベクター
    std::vector<std::vector<BYTE>> allNalUnits;
    
    for (UINT32 i = 0; i < frameCount; i++) {
        // テストフレームの生成
        GenerateTestFrame(frameBuffer, encoder.width, encoder.height, i);
        
        // フレームのエンコード
        std::vector<std::vector<BYTE>> outputNalUnits;
        hr = EncodeFrame(&encoder, frameBuffer, outputNalUnits);
        if (FAILED(hr)) {
            printf("Frame encoding failed at frame %d: 0x%08X\n", i, hr);
            break;
        }
        
        // エンコード結果を全体のリストに追加
        allNalUnits.insert(allNalUnits.end(), outputNalUnits.begin(), outputNalUnits.end());
        
        // 進捗表示
        if (i % 10 == 0) {
            printf("Encoded frame %d/%d\n", i, frameCount);
        }
    }
    
    // 全NALユニットをファイルに書き込む
    printf("Writing %zu NAL units to file...\n", allNalUnits.size());
    for (const auto& nalUnit : allNalUnits) {
        // NALユニット長をファイルに書き込む (ビッグエンディアン 4バイト)
        BYTE lengthBytes[4];
        lengthBytes[0] = (nalUnit.size() >> 24) & 0xFF;
        lengthBytes[1] = (nalUnit.size() >> 16) & 0xFF;
        lengthBytes[2] = (nalUnit.size() >> 8) & 0xFF;
        lengthBytes[3] = nalUnit.size() & 0xFF;
        
        encoder.nalFile.write(reinterpret_cast<const char*>(lengthBytes), 4);
        encoder.nalFile.write(reinterpret_cast<const char*>(nalUnit.data()), nalUnit.size());
    }
    
    // エンコーダーのシャットダウン
    hr = ShutdownEncoder(&encoder);
    if (FAILED(hr)) {
        printf("Encoder shutdown failed: 0x%08X\n", hr);
    }
    
    // デコードプロセスの開始
    printf("\n--- Starting decoding process ---\n");
    
    // デコーダーオブジェクトの作成
    NalDecoder decoder;
    
    // デコーダーの初期化
    hr = InitializeDecoder(&decoder, encoder.width, encoder.height, "output.yuv");
    if (FAILED(hr)) {
        printf("Decoder initialization failed: 0x%08X\n", hr);
        CoUninitialize();
        return 1;
    }
    
    // NALユニットをデコード
    printf("Decoding %zu NAL units...\n", allNalUnits.size());
    
    // 一部のNALユニット(SPS, PPS)は複数回送信する必要がある場合があるので、
    // IDRフレームの前にSPS/PPSを常に送るようにする
    std::vector<BYTE> spsData;
    std::vector<BYTE> ppsData;
    
    for (const auto& nalUnit : allNalUnits) {
        if (nalUnit.size() > 0) {
            // NALユニットタイプの判定 (最初のバイトの下位5ビット)
            BYTE nalType = nalUnit[0] & 0x1F;
            
            // SPSの場合は保存
            if (nalType == 7) {
                spsData = nalUnit;
                hr = DecodeNalUnit(&decoder, nalUnit);
                if (FAILED(hr)) {
                    printf("Failed to decode SPS: 0x%08X\n", hr);
                }
            }
            // PPSの場合は保存
            else if (nalType == 8) {
                ppsData = nalUnit;
                hr = DecodeNalUnit(&decoder, nalUnit);
                if (FAILED(hr)) {
                    printf("Failed to decode PPS: 0x%08X\n", hr);
                }
            }
            // IDRフレームの場合は、SPS/PPSを先に送ってからIDRを送る
            else if (nalType == 5) {
                // SPSがあれば送信
                if (!spsData.empty()) {
                    hr = DecodeNalUnit(&decoder, spsData);
                    if (FAILED(hr)) {
                        printf("Failed to decode SPS before IDR: 0x%08X\n", hr);
                    }
                }
                
                // PPSがあれば送信
                if (!ppsData.empty()) {
                    hr = DecodeNalUnit(&decoder, ppsData);
                    if (FAILED(hr)) {
                        printf("Failed to decode PPS before IDR: 0x%08X\n", hr);
                    }
                }
                
                // IDRフレームをデコード
                hr = DecodeNalUnit(&decoder, nalUnit);
                if (FAILED(hr)) {
                    printf("Failed to decode IDR frame: 0x%08X\n", hr);
                }
            }
            // その他のNALユニット
            else {
                hr = DecodeNalUnit(&decoder, nalUnit);
                if (FAILED(hr)) {
                    printf("Failed to decode NAL unit type %d: 0x%08X\n", nalType, hr);
                }
            }
        }
    }
    
    // デコーダーのシャットダウン
    hr = ShutdownDecoder(&decoder);
    if (FAILED(hr)) {
        printf("Decoder shutdown failed: 0x%08X\n", hr);
    }
    
    // COMのクリーンアップ
    CoUninitialize();
    
    printf("NAL encoding and decoding completed.\n");
    
    return SUCCEEDED(hr) ? 0 : 1;
}
