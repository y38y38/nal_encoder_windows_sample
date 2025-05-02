#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <dshow.h> // ICodecAPIの定義のために追加
#include <stdio.h>
#include <vector>

// Media Foundationライブラリをリンク
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "strmiids.lib") // DirectShowライブラリ（ICodecAPI用）

// エンコード品質定数の定義（codecapi.hで定義されていない場合のフォールバック）
#ifndef eAVEncCommonQualityVeryHigh 
#define eAVEncCommonQualityVeryHigh 4
#endif

// エラーチェック用マクロ
#define CHECK_HR(hr, msg) if (FAILED(hr)) { printf("%s error: 0x%08X\n", msg, hr); goto done; }

// 簡易的な疑似YUVデータの生成（グレースケールのグラデーション）
void GenerateTestFrame(std::vector<BYTE>& buffer, UINT32 width, UINT32 height, UINT32 frameIndex)
{
    // YUV420形式のサイズを計算
    UINT32 ySize = width * height;
    UINT32 uvSize = (width / 2) * (height / 2);
    buffer.resize(ySize + uvSize * 2);
    
    // Yプレーン（輝度）- 動くグラデーションパターン
    for (UINT32 y = 0; y < height; y++) {
        for (UINT32 x = 0; x < width; x++) {
            // フレーム番号に基づいて変化するパターン
            BYTE value = static_cast<BYTE>((x + y + frameIndex * 5) % 256);
            buffer[y * width + x] = value;
        }
    }

    // Uプレーン（色差）- 固定値
    BYTE* uPlane = buffer.data() + ySize;
    for (UINT32 i = 0; i < uvSize; i++) {
        uPlane[i] = 128; // 無彩色
    }

    // Vプレーン（色差）- 固定値
    BYTE* vPlane = buffer.data() + ySize + uvSize;
    for (UINT32 i = 0; i < uvSize; i++) {
        vPlane[i] = 128; // 無彩色
    }
}

int main()
{
    HRESULT hr = S_OK;
    IMFSinkWriter* pWriter = NULL;
    IMFMediaType* pMediaTypeIn = NULL;
    IMFMediaType* pMediaTypeOut = NULL;
    DWORD streamIndex = 0;
    
    // エンコード設定
    const UINT32 width = 640;         // 映像幅
    const UINT32 height = 480;        // 映像高さ
    const UINT32 frameRateNumerator = 30;  // フレームレート分子
    const UINT32 frameRateDenominator = 1; // フレームレート分母
    const UINT32 bitrate = 1500000;   // ビットレート (1.5 Mbps)
    const UINT32 frameCount = 100;    // 生成するフレーム数
    
    const LONGLONG frameTime = 10000000LL * frameRateDenominator / frameRateNumerator; // 100ナノ秒単位
    std::vector<BYTE> frameBuffer;     // フレームデータバッファ
    
    // 出力ファイル名
    const WCHAR* outputFileName = L"output.mp4";
    
    // Media Foundationの初期化
    hr = MFStartup(MF_VERSION);
    CHECK_HR(hr, "MFStartup");
    
    // SinkWriterの作成
    hr = MFCreateSinkWriterFromURL(outputFileName, NULL, NULL, &pWriter);
    CHECK_HR(hr, "MFCreateSinkWriterFromURL");
    
    // 出力メディアタイプの設定（H.264/AVC）
    hr = MFCreateMediaType(&pMediaTypeOut);
    CHECK_HR(hr, "MFCreateMediaType for output");
    
    hr = pMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set output major type");
    
    hr = pMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    CHECK_HR(hr, "Set output subtype");
    
    hr = pMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    CHECK_HR(hr, "Set output bitrate");
    
    hr = pMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    CHECK_HR(hr, "Set output interlace mode");
    
    hr = MFSetAttributeSize(pMediaTypeOut, MF_MT_FRAME_SIZE, width, height);
    CHECK_HR(hr, "Set output frame size");
    
    hr = MFSetAttributeRatio(pMediaTypeOut, MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator);
    CHECK_HR(hr, "Set output frame rate");
    
    hr = MFSetAttributeRatio(pMediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    CHECK_HR(hr, "Set output pixel aspect ratio");
    
    // 出力ストリームの追加
    hr = pWriter->AddStream(pMediaTypeOut, &streamIndex);
    CHECK_HR(hr, "AddStream");
    
    // 入力メディアタイプの設定（YUV420P）
    hr = MFCreateMediaType(&pMediaTypeIn);
    CHECK_HR(hr, "MFCreateMediaType for input");
    
    hr = pMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    CHECK_HR(hr, "Set input major type");
    
    hr = pMediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420);
    CHECK_HR(hr, "Set input subtype");
    
    hr = pMediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    CHECK_HR(hr, "Set input interlace mode");
    
    hr = MFSetAttributeSize(pMediaTypeIn, MF_MT_FRAME_SIZE, width, height);
    CHECK_HR(hr, "Set input frame size");
    
    hr = MFSetAttributeRatio(pMediaTypeIn, MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator);
    CHECK_HR(hr, "Set input frame rate");
    
    hr = MFSetAttributeRatio(pMediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    CHECK_HR(hr, "Set input pixel aspect ratio");
    
    // 入力ストリームの設定
    hr = pWriter->SetInputMediaType(streamIndex, pMediaTypeIn, NULL);
    CHECK_HR(hr, "SetInputMediaType");
    
    // エンコーダー品質の設定（オプション）
    ICodecAPI* pCodecAPI = NULL;
    hr = pWriter->GetServiceForStream(streamIndex, GUID_NULL, IID_PPV_ARGS(&pCodecAPI));
    if (SUCCEEDED(hr) && pCodecAPI) {
        // エンコード品質を指定（バランス値）
        VARIANT var;
        var.vt = VT_UI4;
        var.ulVal = eAVEncCommonQualityVeryHigh;
        pCodecAPI->SetValue(&CODECAPI_AVEncCommonQuality, &var);
        pCodecAPI->Release();
    }
    
    // 書き込み開始
    hr = pWriter->BeginWriting();
    CHECK_HR(hr, "BeginWriting");
    
    // フレームの生成と書き込み
    printf("Encoding started: %d frames (%dx%d @ %d fps)\n", 
           frameCount, width, height, frameRateNumerator / frameRateDenominator);
           
    for (UINT32 frameIndex = 0; frameIndex < frameCount; frameIndex++) {
        // テストフレームの生成
        GenerateTestFrame(frameBuffer, width, height, frameIndex);
        
        // フレームバッファの準備
        IMFSample* pSample = NULL;
        IMFMediaBuffer* pBuffer = NULL;
        
        // サンプルの作成
        hr = MFCreateSample(&pSample);
        CHECK_HR(hr, "MFCreateSample");
        
        // バッファの作成
        hr = MFCreateMemoryBuffer(static_cast<DWORD>(frameBuffer.size()), &pBuffer);
        CHECK_HR(hr, "MFCreateMemoryBuffer");
        
        // バッファのロックとデータのコピー
        BYTE* pData = NULL;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        
        hr = pBuffer->Lock(&pData, &maxLength, &currentLength);
        CHECK_HR(hr, "Lock");
        
        memcpy(pData, frameBuffer.data(), frameBuffer.size());
        
        hr = pBuffer->SetCurrentLength(static_cast<DWORD>(frameBuffer.size()));
        CHECK_HR(hr, "SetCurrentLength");
        
        hr = pBuffer->Unlock();
        CHECK_HR(hr, "Unlock");
        
        // バッファをサンプルに追加
        hr = pSample->AddBuffer(pBuffer);
        CHECK_HR(hr, "AddBuffer");
        
        // タイムスタンプの設定
        hr = pSample->SetSampleTime(frameIndex * frameTime);
        CHECK_HR(hr, "SetSampleTime");
        
        hr = pSample->SetSampleDuration(frameTime);
        CHECK_HR(hr, "SetSampleDuration");
        
        // フレームの書き込み
        hr = pWriter->WriteSample(streamIndex, pSample);
        CHECK_HR(hr, "WriteSample");
        
        // 使用したリソースを解放
        if (pBuffer) {
            pBuffer->Release();
            pBuffer = NULL;
        }
        
        if (pSample) {
            pSample->Release();
            pSample = NULL;
        }
        
        // 進捗表示
        if (frameIndex % 10 == 0) {
            printf("Processing: Frame %d/%d\n", frameIndex, frameCount);
        }
    }
    
    // 書き込み完了
    hr = pWriter->Finalize();
    CHECK_HR(hr, "Finalize");
    
    printf("Encoding completed: Saved to %s\n", "output.mp4");
    
done:
    // リソースの解放
    if (pWriter) {
        pWriter->Release();
    }
    
    if (pMediaTypeIn) {
        pMediaTypeIn->Release();
    }
    
    if (pMediaTypeOut) {
        pMediaTypeOut->Release();
    }
    
    // Media Foundationのシャットダウン
    MFShutdown();
    
    if (SUCCEEDED(hr)) {
        return 0;
    } else {
        return 1;
    }
}