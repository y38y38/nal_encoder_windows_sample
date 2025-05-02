// NALDecoderSample.cpp
// Media Foundation APIを使用したNALユニットのデコードサンプル

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <stdio.h>
#include <vector>
#include <iostream>

// リンクに必要なライブラリを指定
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

// NALユニットの種類
enum NalUnitType {
    NAL_UNIT_UNSPECIFIED = 0,
    NAL_UNIT_CODED_SLICE = 1,
    NAL_UNIT_CODED_SLICE_DATAPART_A = 2,
    NAL_UNIT_CODED_SLICE_DATAPART_B = 3,
    NAL_UNIT_CODED_SLICE_DATAPART_C = 4,
    NAL_UNIT_CODED_SLICE_IDR = 5,
    NAL_UNIT_SEI = 6,
    NAL_UNIT_SPS = 7,
    NAL_UNIT_PPS = 8,
    NAL_UNIT_ACCESS_UNIT_DELIMITER = 9,
    NAL_UNIT_END_OF_SEQUENCE = 10,
    NAL_UNIT_END_OF_STREAM = 11,
    NAL_UNIT_FILLER_DATA = 12
};

// NALユニット構造体
struct NALUnit {
    BYTE* data;
    DWORD size;
    NalUnitType type;
};

// H.264ストリームからNALユニットを検索する関数
std::vector<NALUnit> FindNALUnits(const BYTE* buffer, DWORD size) {
    std::vector<NALUnit> nalUnits;
    
    DWORD startCodePos = 0;
    DWORD nalUnitStart = 0;
    bool foundStartCode = false;
    
    // NALユニットの開始コードを検索
    for (DWORD i = 0; i < size - 3; i++) {
        // 4バイトのスタートコード (0x00000001) を検索
        if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 0 && buffer[i + 3] == 1) {
            if (foundStartCode) {
                // 現在のNALユニットの終了位置
                DWORD nalUnitSize = i - nalUnitStart;
                
                if (nalUnitSize > 0) {
                    NALUnit nalUnit;
                    nalUnit.data = const_cast<BYTE*>(buffer + nalUnitStart);
                    nalUnit.size = nalUnitSize;
                    
                    // NALユニットのタイプを取得
                    nalUnit.type = static_cast<NalUnitType>(buffer[nalUnitStart] & 0x1F);
                    
                    nalUnits.push_back(nalUnit);
                }
            }
            
            foundStartCode = true;
            nalUnitStart = i + 4; // 開始コードの後ろからNALユニットは始まる
            i += 3; // 開始コードをスキップ
        }
        // 3バイトのスタートコード (0x000001) を検索
        else if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1) {
            if (foundStartCode) {
                // 現在のNALユニットの終了位置
                DWORD nalUnitSize = i - nalUnitStart;
                
                if (nalUnitSize > 0) {
                    NALUnit nalUnit;
                    nalUnit.data = const_cast<BYTE*>(buffer + nalUnitStart);
                    nalUnit.size = nalUnitSize;
                    
                    // NALユニットのタイプを取得
                    nalUnit.type = static_cast<NalUnitType>(buffer[nalUnitStart] & 0x1F);
                    
                    nalUnits.push_back(nalUnit);
                }
            }
            
            foundStartCode = true;
            nalUnitStart = i + 3; // 開始コードの後ろからNALユニットは始まる
            i += 2; // 開始コードをスキップ
        }
    }
    
    // ファイルの最後のNALユニットを追加
    if (foundStartCode && nalUnitStart < size) {
        NALUnit nalUnit;
        nalUnit.data = const_cast<BYTE*>(buffer + nalUnitStart);
        nalUnit.size = size - nalUnitStart;
        
        // NALユニットのタイプを取得
        nalUnit.type = static_cast<NalUnitType>(buffer[nalUnitStart] & 0x1F);
        
        nalUnits.push_back(nalUnit);
    }
    
    return nalUnits;
}

// NALユニットのタイプを文字列で返す関数
const char* GetNalUnitTypeString(NalUnitType type) {
    switch (type) {
        case NAL_UNIT_CODED_SLICE: return "CODED_SLICE";
        case NAL_UNIT_CODED_SLICE_DATAPART_A: return "CODED_SLICE_DATAPART_A";
        case NAL_UNIT_CODED_SLICE_DATAPART_B: return "CODED_SLICE_DATAPART_B";
        case NAL_UNIT_CODED_SLICE_DATAPART_C: return "CODED_SLICE_DATAPART_C";
        case NAL_UNIT_CODED_SLICE_IDR: return "CODED_SLICE_IDR";
        case NAL_UNIT_SEI: return "SEI";
        case NAL_UNIT_SPS: return "SPS";
        case NAL_UNIT_PPS: return "PPS";
        case NAL_UNIT_ACCESS_UNIT_DELIMITER: return "ACCESS_UNIT_DELIMITER";
        case NAL_UNIT_END_OF_SEQUENCE: return "END_OF_SEQUENCE";
        case NAL_UNIT_END_OF_STREAM: return "END_OF_STREAM";
        case NAL_UNIT_FILLER_DATA: return "FILLER_DATA";
        default: return "UNKNOWN";
    }
}

// Media Foundation ソースリーダーを作成
HRESULT CreateSourceReaderFromFile(LPCWSTR filePath, IMFSourceReader** ppSourceReader) {
    HRESULT hr = S_OK;
    
    // Media Foundation の初期化
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "MFStartup failed: " << std::hex << hr << std::endl;
        return hr;
    }
    
    // ソースリーダーの作成
    hr = MFCreateSourceReaderFromURL(filePath, NULL, ppSourceReader);
    if (FAILED(hr)) {
        std::cerr << "MFCreateSourceReaderFromURL failed: " << std::hex << hr << std::endl;
        MFShutdown();
        return hr;
    }
    
    // ビデオストリームのみを選択
    hr = (*ppSourceReader)->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr)) {
        std::cerr << "SetStreamSelection(ALL_STREAMS) failed: " << std::hex << hr << std::endl;
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    hr = (*ppSourceReader)->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        std::cerr << "SetStreamSelection(FIRST_VIDEO_STREAM) failed: " << std::hex << hr << std::endl;
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    // 出力形式をH.264に設定
    IMFMediaType* pMediaType = NULL;
    hr = MFCreateMediaType(&pMediaType);
    if (FAILED(hr)) {
        std::cerr << "MFCreateMediaType failed: " << std::hex << hr << std::endl;
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) {
        std::cerr << "SetGUID(MF_MT_MAJOR_TYPE) failed: " << std::hex << hr << std::endl;
        pMediaType->Release();
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (FAILED(hr)) {
        std::cerr << "SetGUID(MF_MT_SUBTYPE) failed: " << std::hex << hr << std::endl;
        pMediaType->Release();
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    hr = (*ppSourceReader)->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pMediaType);
    if (FAILED(hr)) {
        std::cerr << "SetCurrentMediaType failed: " << std::hex << hr << std::endl;
        pMediaType->Release();
        (*ppSourceReader)->Release();
        MFShutdown();
        return hr;
    }
    
    pMediaType->Release();
    
    return hr;
}

// ビデオファイルからNALユニットを抽出
HRESULT ExtractNALUnits(LPCWSTR filePath) {
    HRESULT hr = S_OK;
    IMFSourceReader* pSourceReader = NULL;
    
    // ソースリーダーの作成
    hr = CreateSourceReaderFromFile(filePath, &pSourceReader);
    if (FAILED(hr)) {
        return hr;
    }
    
    // サンプルの読み取り
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* pSample = NULL;
    int sampleCount = 0;
    
    while (true) {
        hr = pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );
        
        if (FAILED(hr)) {
            std::cerr << "ReadSample failed: " << std::hex << hr << std::endl;
            break;
        }
        
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            std::cout << "End of stream reached." << std::endl;
            break;
        }
        
        if (pSample) {
            // サンプルからバッファを取得
            IMFMediaBuffer* pBuffer = NULL;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);
            if (SUCCEEDED(hr)) {
                BYTE* pData = NULL;
                DWORD cbData = 0;
                
                hr = pBuffer->Lock(&pData, NULL, &cbData);
                if (SUCCEEDED(hr)) {
                    // NALユニットを検索
                    std::vector<NALUnit> nalUnits = FindNALUnits(pData, cbData);
                    
                    std::cout << "Sample " << sampleCount << " contains " << nalUnits.size() << " NAL units:" << std::endl;
                    
                    for (size_t i = 0; i < nalUnits.size(); i++) {
                        std::cout << "  NAL Unit " << i << ": Type = " << GetNalUnitTypeString(nalUnits[i].type)
                                  << ", Size = " << nalUnits[i].size << " bytes" << std::endl;
                        
                        // ここでNALユニットの内容を処理することができます
                        // 例：デコードしたり、分析したり
                    }
                    
                    pBuffer->Unlock();
                }
                
                pBuffer->Release();
            }
            
            pSample->Release();
            sampleCount++;
        }
    }
    
    std::cout << "Total samples processed: " << sampleCount << std::endl;
    
    // クリーンアップ
    pSourceReader->Release();
    MFShutdown();
    
    return hr;
}

int main(int argc, char* argv[]) {
    // COMの初期化
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::cerr << "COM initialization failed: " << std::hex << hr << std::endl;
        return -1;
    }
    
    if (argc < 2) {
        std::cerr << "Usage: NALDecoderSample <video_file_path>" << std::endl;
        CoUninitialize();
        return -1;
    }
    
    // ファイルパスをワイド文字列に変換
    int widePathSize = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, NULL, 0);
    wchar_t* widePath = new wchar_t[widePathSize];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, widePath, widePathSize);
    
    // NALユニットの抽出
    hr = ExtractNALUnits(widePath);
    
    // クリーンアップ
    delete[] widePath;
    CoUninitialize();
    
    return SUCCEEDED(hr) ? 0 : -1;
}