#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <vector>
#include <fstream>
#include <string>

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

// デコーダーを初期化する関数
HRESULT InitializeDecoder(NalDecoder* pDecoder, UINT32 width, UINT32 height, const char* outputFilename);

// NALユニットをデコードして、YUVフレームデータとして返す
HRESULT DecodeNalUnit(NalDecoder* pDecoder, const std::vector<BYTE>& nalData, std::vector<BYTE>* outputFrameData);

// デコーダーリソースを解放する関数
HRESULT ShutdownDecoder(NalDecoder* pDecoder);