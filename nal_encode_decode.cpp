#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <stdio.h>
#define _CRT_SECURE_NO_WARNINGS
#include <vector>
#include <fstream>
#include <string>
#include <dshow.h>
#include "yuv_encoder_win.h"  // エンコーダー機能のヘッダ
#include "nal_decoder_win.h"  // デコーダー機能のヘッダを追加

// Media Foundationライブラリをリンク
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "strmiids.lib")

// 簡素化されたエラーチェック用マクロ
#define CHECK_HR(hr, msg) if (FAILED(hr)) { \
    printf("%s error: 0x%08X\n", msg, hr); \
    return hr; \
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
    hr = InitializeEncoder(&encoder);
    if (FAILED(hr)) {
        printf("Encoder initialization failed: 0x%08X\n", hr);
        CoUninitialize();
        return 1;
    }
    
    // テストフレームの生成とエンコード
    std::vector<BYTE> frameBuffer;
    const UINT32 frameCount = 61;
    
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

    // FlushEncoderでflush後のNALユニットもallNalUnitsに追加
    hr = FlushEncoder(&encoder, allNalUnits);
    if (FAILED(hr)) {
        printf("FlushEncoder failed: 0x%08X\n", hr);
    }
    
    // 注意: H.264エンコーダはPフレーム混在時、全フレームでNALユニットが出力されるとは限りません。
    // 例: 100フレーム入力してもNALユニット数が92などになる場合があります（仕様通り）。
    // 全フレーム分のNALユニットが必要な場合は全てIDR出力にしてください。

    // 全NALユニットをファイルに書き込む
    printf("Writing %zu NAL units to file...\n", allNalUnits.size());
    // ファイルポインタを使用してNALユニットを保存
    FILE* nalFile = fopen("output.h264", "wb");
    if (!nalFile) {
        printf("Failed to open output.h264 for writing.\n");
        return 1;
    }

    for (const auto& nalUnit : allNalUnits) {
        // NALユニット長をファイルに書き込む (ビッグエンディアン 4バイト)
        BYTE lengthBytes[4];
        lengthBytes[0] = (nalUnit.size() >> 24) & 0xFF;
        lengthBytes[1] = (nalUnit.size() >> 16) & 0xFF;
        lengthBytes[2] = (nalUnit.size() >> 8) & 0xFF;
        lengthBytes[3] = nalUnit.size() & 0xFF;

        fwrite(lengthBytes, 1, 4, nalFile);
        fwrite(nalUnit.data(), 1, nalUnit.size(), nalFile);
    }

    fclose(nalFile);
    
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
    std::vector<BYTE> frameData; // デコードされたフレームデータを格納するためのベクター
    
    for (const auto& nalUnit : allNalUnits) {
        if (nalUnit.size() > 0) {
            // NALユニットタイプの判定 (最初のバイトの下位5ビット)
            BYTE nalType = nalUnit[0] & 0x1F;
            
            // デコードされたフレームデータを格納するためのベクター
            std::vector<BYTE> decodedFrameData;
                hr = DecodeNalUnit(&decoder, nalUnit, &decodedFrameData);
                if (FAILED(hr)) {
                    printf("Failed to decode NAL unit type %d: 0x%08X\n", nalType, hr);
                }
                
                // 有効なYUVデータが得られた場合はファイルに書き込む
                if (!decodedFrameData.empty()) {
                    decoder.yuvFile.write(reinterpret_cast<const char*>(decodedFrameData.data()), decodedFrameData.size());
                }
        }
    }
#if 1
    // FlushDecoder APIで残りの出力フレームを取得
    hr = FlushDecoder(&decoder);
    if (FAILED(hr)) {
        printf("FlushDecoder failed: 0x%08X\n", hr);
    }
#endif
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
