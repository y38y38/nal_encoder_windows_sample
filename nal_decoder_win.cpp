#include "nal_decoder_win.h"
#include <stdio.h>

// H.264デコーダーのCLSIDを定義
static const GUID CLSID_CMSH264DecoderMFT = 
    { 0x62CE7E72, 0x4C71, 0x4d20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

// 簡素化されたエラーチェック用マクロ
#define CHECK_HR(hr, msg) if (FAILED(hr)) { \
    printf("%s error: 0x%08X\n", msg, hr); \
    return hr; \
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

// 空のNALユニット（Flush時）を処理する内部関数
HRESULT ProcessEmptyNalUnit(NalDecoder* pDecoder, std::vector<BYTE>* outputFrameData) {
    HRESULT hr = S_OK;
    MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
    DWORD processOutputStatus = 0;

    // 出力サンプルの取得
    IMFSample* pOutSample = NULL;
    hr = MFCreateSample(&pOutSample);
    CHECK_HR(hr, "MFCreateSample for decoder output");
    IMFMediaBuffer* pOutBuffer = NULL;
    UINT32 nv12Size = pDecoder->width * pDecoder->height * 3 / 2;
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
    
    // ProcessOutputのみ実行（入力なし）
    hr = pDecoder->pDecoder->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        if (pOutSample) {
            pOutSample->Release();
        }
        return hr;
    } else if (SUCCEEDED(hr)) {
        // 出力バッファからYUVデータを取得
        IMFMediaBuffer* pBuffer = NULL;
        hr = pOutSample->ConvertToContiguousBuffer(&pBuffer);
        CHECK_HR(hr, "ConvertToContiguousBuffer for decoder output");
        BYTE* pYuvData = NULL;
        DWORD yuvMaxLength = 0;
        DWORD yuvCurrentLength = 0;
        hr = pBuffer->Lock(&pYuvData, &yuvMaxLength, &yuvCurrentLength);
        CHECK_HR(hr, "Lock decoder output buffer");
        if (yuvCurrentLength > 0 && pYuvData != NULL) {
            size_t currentSize = outputFrameData->size();
            outputFrameData->resize(currentSize + yuvCurrentLength);
            memcpy(outputFrameData->data() + currentSize, pYuvData, yuvCurrentLength);
            printf("Decoded frame %llu: %d bytes of YUV data\n", pDecoder->frameCount, yuvCurrentLength);
            pDecoder->frameCount++;
        }
        hr = pBuffer->Unlock();
        CHECK_HR(hr, "Unlock decoder output buffer");
        if (pBuffer) {
            pBuffer->Release();
        }
    } else {
        CHECK_HR(hr, "ProcessOutput for decoder");
    }
    
    if (pOutSample) {
        pOutSample->Release();
    }
    return hr;
}

// NALデータを入力として処理する内部関数
HRESULT ProcessNalInput(NalDecoder* pDecoder, const std::vector<BYTE>& nalData) {
    HRESULT hr = S_OK;
    
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
    
    return hr;
}

// デコーダー出力を処理する内部関数
HRESULT ProcessDecoderOutput(NalDecoder* pDecoder, std::vector<BYTE>* outputFrameData) {
    HRESULT hr = S_OK;
    MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
    DWORD processOutputStatus = 0;
    
    // 出力サンプルの取得
    IMFSample* pOutSample = NULL;

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
            // デコード成功、YUVデータを取得
            IMFMediaBuffer* pBuffer = NULL;
            hr = pOutSample->ConvertToContiguousBuffer(&pBuffer);
            CHECK_HR(hr, "ConvertToContiguousBuffer for decoder output");

            BYTE* pYuvData = NULL;
            DWORD yuvMaxLength = 0;
            DWORD yuvCurrentLength = 0;

            hr = pBuffer->Lock(&pYuvData, &yuvMaxLength, &yuvCurrentLength);
            CHECK_HR(hr, "Lock decoder output buffer");

            // YUVデータを出力ベクターにコピー
            if (yuvCurrentLength > 0 && pYuvData != NULL) {
                // 出力ベクターのサイズを設定（既存データがあれば追加）
                size_t currentSize = outputFrameData->size();
                outputFrameData->resize(currentSize + yuvCurrentLength);

                // YUVデータをコピー
                memcpy(outputFrameData->data() + currentSize, pYuvData, yuvCurrentLength);

                printf("Decoded frame %llu: %d bytes of YUV data\n", pDecoder->frameCount, yuvCurrentLength);
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

// NALユニットをデコードして、YUVフレームデータとして返す（リファクタリング版）
HRESULT DecodeNalUnit(NalDecoder* pDecoder, const std::vector<BYTE>& nalData, std::vector<BYTE>* outputFrameData) {
    // 出力パラメータの検証
    if (!outputFrameData) {
        return E_INVALIDARG;
    }

    // NALデータが空の場合はFlush処理（ProcessInputを呼ばず、ProcessOutputのみ実行）
    if (nalData.empty()) {
        return ProcessEmptyNalUnit(pDecoder, outputFrameData);
    }

    // 通常のNALデータ処理
    HRESULT hr = ProcessNalInput(pDecoder, nalData);
    if (FAILED(hr)) {
        return hr;
    }

    // デコード出力の処理
    return ProcessDecoderOutput(pDecoder, outputFrameData);
}

// デコーダーをFlushし、残りの出力フレームをYUVファイルに書き込む関数
HRESULT FlushDecoder(NalDecoder* pDecoder) {
    if (!pDecoder || !pDecoder->pDecoder) return E_POINTER;
    HRESULT hr = S_OK;
    hr = pDecoder->pDecoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    if (FAILED(hr)) {
        printf("Decoder drain command failed: 0x%08X\n", hr);
        return hr;
    }
    // 残りの出力フレームを取得
    while (true) {
        std::vector<BYTE> flushedFrameData;
        HRESULT hrOut = DecodeNalUnit(pDecoder, std::vector<BYTE>(), &flushedFrameData);
        if (hrOut == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // もう出力はない
            break;
        } else if (FAILED(hrOut)) {
            printf("Flush decode failed: 0x%08X\n", hrOut);
            break;
        }
        if (!flushedFrameData.empty()) {
            pDecoder->yuvFile.write(reinterpret_cast<const char*>(flushedFrameData.data()), flushedFrameData.size());
        }
    }
    return S_OK;
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