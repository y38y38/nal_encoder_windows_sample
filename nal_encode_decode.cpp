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
#include <dshow.h>
#include "nal_encode_win.h"  // エンコーダー機能のヘッダ
#include "nal_decode_win.h"  // デコーダー機能のヘッダを追加

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
    std::vector<BYTE> frameData; // デコードされたフレームデータを格納するためのベクター
    
    for (const auto& nalUnit : allNalUnits) {
        if (nalUnit.size() > 0) {
            // NALユニットタイプの判定 (最初のバイトの下位5ビット)
            BYTE nalType = nalUnit[0] & 0x1F;
            
            // デコードされたフレームデータを格納するためのベクター
            std::vector<BYTE> decodedFrameData;
            
            // SPSの場合は保存
            if (nalType == 7) {
                spsData = nalUnit;
                hr = DecodeNalUnit(&decoder, nalUnit, &decodedFrameData);
                if (FAILED(hr)) {
                    printf("Failed to decode SPS: 0x%08X\n", hr);
                }
            }
            // PPSの場合は保存
            else if (nalType == 8) {
                ppsData = nalUnit;
                hr = DecodeNalUnit(&decoder, nalUnit, &decodedFrameData);
                if (FAILED(hr)) {
                    printf("Failed to decode PPS: 0x%08X\n", hr);
                }
            }
            // IDRフレームの場合は、SPS/PPSを先に送ってからIDRを送る
            else if (nalType == 5) {
                // SPSがあれば送信
                if (!spsData.empty()) {
                    hr = DecodeNalUnit(&decoder, spsData, &decodedFrameData);
                    if (FAILED(hr)) {
                        printf("Failed to decode SPS before IDR: 0x%08X\n", hr);
                    }
                    
                    // 有効なYUVデータが得られた場合はファイルに書き込む
                    if (!decodedFrameData.empty()) {
                        decoder.yuvFile.write(reinterpret_cast<const char*>(decodedFrameData.data()), decodedFrameData.size());
                    }
                }
                
                // PPSがあれば送信
                if (!ppsData.empty()) {
                    decodedFrameData.clear();
                    hr = DecodeNalUnit(&decoder, ppsData, &decodedFrameData);
                    if (FAILED(hr)) {
                        printf("Failed to decode PPS before IDR: 0x%08X\n", hr);
                    }
                    
                    // 有効なYUVデータが得られた場合はファイルに書き込む
                    if (!decodedFrameData.empty()) {
                        decoder.yuvFile.write(reinterpret_cast<const char*>(decodedFrameData.data()), decodedFrameData.size());
                    }
                }
                
                // IDRフレームをデコード
                decodedFrameData.clear();
                hr = DecodeNalUnit(&decoder, nalUnit, &decodedFrameData);
                if (FAILED(hr)) {
                    printf("Failed to decode IDR frame: 0x%08X\n", hr);
                }
                
                // 有効なYUVデータが得られた場合はファイルに書き込む
                if (!decodedFrameData.empty()) {
                    decoder.yuvFile.write(reinterpret_cast<const char*>(decodedFrameData.data()), decodedFrameData.size());
                }
            }
            // その他のNALユニット
            else {
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
