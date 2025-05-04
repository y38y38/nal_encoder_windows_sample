#pragma once

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
};

// テストフレームをNV12形式で生成する関数
void GenerateTestFrame(std::vector<BYTE>& buffer, UINT32 width, UINT32 height, UINT32 frameIndex);

// エンコーダーを初期化する関数
HRESULT InitializeEncoder(NalEncoder* pEncoder, const char* outputFilename);

// エンコーダーを初期化する関数
HRESULT InitializeEncoder(NalEncoder* pEncoder);

// フレームをエンコードする関数
HRESULT EncodeFrame(NalEncoder* pEncoder, const std::vector<BYTE>& frameData, std::vector<std::vector<BYTE>>& outputNalUnits);


// IMFSampleからNALユニットを抽出する関数
HRESULT ExtractNalUnitsFromSample(IMFSample* pSample, std::vector<std::vector<BYTE>>& outputNalUnits);

HRESULT FlushEncoder(NalEncoder* pEncoder, std::vector<std::vector<BYTE>>& allNalUnits);

// エンコーダーリソースを解放する関数
HRESULT ShutdownEncoder(NalEncoder* pEncoder);
