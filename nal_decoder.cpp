#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <propvarutil.h>
#include <codecapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

// Media Foundationのエラーコードが定義されていない場合の定義
#ifndef MF_E_TRANSFORM_TYPE_NOT_SET
#define MF_E_TRANSFORM_TYPE_NOT_SET _HRESULT_TYPEDEF_(0xC00D6D60)
#endif

#ifndef MF_E_NO_MORE_TYPES
#define MF_E_NO_MORE_TYPES _HRESULT_TYPEDEF_(0xC00D6D73)
#endif

#ifndef MF_E_NOTACCEPTING
#define MF_E_NOTACCEPTING _HRESULT_TYPEDEF_(0xC00D36B2)
#endif

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT _HRESULT_TYPEDEF_(0xC00D6D72)
#endif

// COMインターフェースを解放するためのヘルパークラス
template <class T>
class ComPtr {
private:
    T* ptr;

public:
    ComPtr() : ptr(nullptr) {}
    ~ComPtr() { Release(); }

    T** operator&() { Release(); return &ptr; }
    T* operator->() { return ptr; }
    operator T*() { return ptr; }
    operator bool() { return ptr != nullptr; }

    void Release() {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
};

// H.264 NAL Unit Types
enum NalUnitType {
    UNSPECIFIED = 0,
    CODED_SLICE_NON_IDR = 1,
    CODED_SLICE_PARTITION_A = 2,
    CODED_SLICE_PARTITION_B = 3,
    CODED_SLICE_PARTITION_C = 4,
    CODED_SLICE_IDR = 5,
    SEI = 6,
    SPS = 7,
    PPS = 8,
    AUD = 9,
    END_OF_SEQUENCE = 10,
    END_OF_STREAM = 11,
    FILLER_DATA = 12,
    SPS_EXT = 13,
    PREFIX_NAL = 14,
    SUBSET_SPS = 15
};

// NALユニットのタイプを取得する関数
uint8_t GetNalUnitType(const std::vector<uint8_t>& nalUnit) {
    if (nalUnit.size() < 5) return UNSPECIFIED;
    
    // スタートコードの後の最初のバイトからタイプを抽出
    uint8_t startCodeSize = 0;
    if (nalUnit.size() >= 4 && nalUnit[0] == 0 && nalUnit[1] == 0 && nalUnit[2] == 0 && nalUnit[3] == 1) {
        startCodeSize = 4;
    } else if (nalUnit.size() >= 3 && nalUnit[0] == 0 && nalUnit[1] == 0 && nalUnit[2] == 1) {
        startCodeSize = 3;
    }
    
    if (startCodeSize == 0 || nalUnit.size() <= startCodeSize) return UNSPECIFIED;
    
    // NAL unit typeは下位5ビット
    return nalUnit[startCodeSize] & 0x1F;
}

// NALユニットをファイルから読み込む関数
std::vector<std::vector<uint8_t>> ReadNalUnitsFromFile(const std::string& filename) {
    std::vector<std::vector<uint8_t>> nalUnits;
    std::ifstream file(filename, std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return nalUnits;
    }

    // ファイル全体を読み込む
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        std::cerr << "Failed to read file" << std::endl;
        return nalUnits;
    }

    // SPS/PPSを含むAnnexBストリームを作成する
    bool foundSPS = false;
    bool foundPPS = false;
    
    // NALユニットを検索 (開始コード: 0x00 0x00 0x00 0x01 または 0x00 0x00 0x01)
    size_t pos = 0;
    while (pos < buffer.size()) {
        // NAL開始コードを検索
        size_t startCodePos = pos;
        while (startCodePos < buffer.size() - 3) {
            if (buffer[startCodePos] == 0 && buffer[startCodePos + 1] == 0 && 
                ((buffer[startCodePos + 2] == 0 && buffer[startCodePos + 3] == 1) ||
                 (buffer[startCodePos + 2] == 1))) {
                break;
            }
            startCodePos++;
        }

        if (startCodePos >= buffer.size() - 3) {
            // 開始コードが見つからなかった
            break;
        }

        // 開始コードのサイズを決定 (3バイトか4バイト)
        size_t startCodeSize = (buffer[startCodePos + 2] == 0) ? 4 : 3;
        
        // 次のNAL開始コードを検索
        size_t nextStartCodePos = startCodePos + startCodeSize;
        while (nextStartCodePos < buffer.size() - 2) {
            if (buffer[nextStartCodePos] == 0 && buffer[nextStartCodePos + 1] == 0 && 
                (nextStartCodePos + 2 < buffer.size() && 
                 ((buffer[nextStartCodePos + 2] == 0 && nextStartCodePos + 3 < buffer.size() && buffer[nextStartCodePos + 3] == 1) ||
                  buffer[nextStartCodePos + 2] == 1))) {
                break;
            }
            nextStartCodePos++;
        }

        // NALユニットを抽出
        size_t nalUnitStart = startCodePos;  // スタートコードを含める
        size_t nalUnitEnd = (nextStartCodePos < buffer.size() - 2) ? 
                           nextStartCodePos : 
                           buffer.size();

        std::vector<uint8_t> nalUnit(buffer.begin() + nalUnitStart, buffer.begin() + nalUnitEnd);
        
        // NALユニットタイプをチェック
        if (nalUnit.size() > startCodeSize) {
            uint8_t nalType = nalUnit[startCodeSize] & 0x1F;
            if (nalType == 7) { // SPS
                foundSPS = true;
                std::cout << "Found SPS NAL unit, size: " << nalUnit.size() << " bytes" << std::endl;
            } 
            else if (nalType == 8) { // PPS
                foundPPS = true;
                std::cout << "Found PPS NAL unit, size: " << nalUnit.size() << " bytes" << std::endl;
            }
        }
        
        nalUnits.push_back(nalUnit);
        
        // 次のNALユニットへ
        pos = (nextStartCodePos < buffer.size() - 2) ? nextStartCodePos : buffer.size();
    }

    std::cout << "Found " << nalUnits.size() << " NAL units in file" << std::endl;
    
    // SPSとPPSが見つからなかった場合は警告
    if (!foundSPS || !foundPPS) {
        std::cerr << "Warning: " << (!foundSPS ? "SPS" : "") << (!foundSPS && !foundPPS ? " and " : "") 
                  << (!foundPPS ? "PPS" : "") << " NAL units not found in the stream. "
                  << "Decoding may fail." << std::endl;
    }
    
    return nalUnits;
}

// NALデコーダークラス
class NalDecoder {
private:
    ComPtr<IMFTransform> decoder;
    IMFMediaType* inputMediaType;
    IMFMediaType* outputMediaType;
    DWORD inputStreamId;
    DWORD outputStreamId;
    bool initialized;
    UINT32 width;
    UINT32 height;

    // デコーダーの検索と作成
    HRESULT CreateDecoder() {
        // H.264デコーダーを検索
        IMFActivate** ppActivate = nullptr;
        UINT32 count = 0;
        
        MFT_REGISTER_TYPE_INFO inputType = {MFMediaType_Video, MFVideoFormat_H264};
        MFT_REGISTER_TYPE_INFO outputType = {MFMediaType_Video, MFVideoFormat_NV12};
        
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_HARDWARE,
            &inputType,
            &outputType,
            &ppActivate,
            &count
        );

        if (FAILED(hr) || count == 0) {
            std::cerr << "Decoder not found. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        std::cout << "Found " << count << " H.264 decoder(s)" << std::endl;

        // 最初のデコーダーを使用
        hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&decoder));
        
        // デコーダー名を表示
        WCHAR* friendlyName = nullptr;
        if (SUCCEEDED(ppActivate[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, nullptr))) {
            std::wcout << L"Using decoder: " << friendlyName << std::endl;
            CoTaskMemFree(friendlyName);
        }
        
        // アクティベートオブジェクトを解放
        for (UINT32 i = 0; i < count; i++) {
            ppActivate[i]->Release();
        }
        CoTaskMemFree(ppActivate);

        if (FAILED(hr)) {
            std::cerr << "Failed to activate decoder. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // 入力・出力ストリームIDを取得
        hr = decoder->GetStreamIDs(1, &inputStreamId, 1, &outputStreamId);
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            // ストリームIDが必要ない場合
            inputStreamId = 0;
            outputStreamId = 0;
            hr = S_OK;
        }
        
        std::cout << "Stream IDs: Input=" << inputStreamId << ", Output=" << outputStreamId << std::endl;
        return hr;
    }

    // メディアタイプの設定
    HRESULT SetMediaTypes() {
        // 入力メディアタイプの設定
        HRESULT hr = MFCreateMediaType(&inputMediaType);
        if (FAILED(hr)) {
            std::cerr << "Failed to create input media type. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        hr = inputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) return hr;

        hr = inputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (FAILED(hr)) return hr;

        // 追加設定 - H.264デコーダーには必要な場合がある
        hr = inputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr)) {
            std::cerr << "Warning: Failed to set interlace mode. Error code: 0x" << std::hex << hr << std::endl;
            // 続行する
        }

        // デフォルトの解像度を設定（推測値）
        width = 640;
        height = 480;
        hr = MFSetAttributeSize(inputMediaType, MF_MT_FRAME_SIZE, width, height);
        if (FAILED(hr)) {
            std::cerr << "Warning: Failed to set frame size. Error code: 0x" << std::hex << hr << std::endl;
            // 続行する
        }

        hr = MFSetAttributeRatio(inputMediaType, MF_MT_FRAME_RATE, 30, 1);
        if (FAILED(hr)) {
            std::cerr << "Warning: Failed to set frame rate. Error code: 0x" << std::hex << hr << std::endl;
            // 続行する
        }

        hr = decoder->SetInputType(inputStreamId, inputMediaType, 0);
        if (FAILED(hr)) {
            std::cerr << "Failed to set input type. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        std::cout << "Input media type set successfully" << std::endl;

        // 出力メディアタイプの設定
        // デコーダーが対応する出力タイプを列挙
        bool foundOutputType = false;
        for (DWORD i = 0; ; i++) {
            ComPtr<IMFMediaType> pOutputType;
            hr = decoder->GetOutputAvailableType(outputStreamId, i, &pOutputType);
            if (hr == MF_E_NO_MORE_TYPES || hr == E_NOTIMPL) {
                // もう列挙するタイプがない、または列挙がサポートされていない
                if (i == 0) {
                    std::cerr << "No supported output types available" << std::endl;
                    return E_FAIL;
                }
                break;
            }
            
            if (FAILED(hr)) {
                std::cerr << "Failed to get output type. Error code: 0x" << std::hex << hr << std::endl;
                continue;
            }

            // 出力タイプの情報を表示
            GUID subtype;
            if (SUCCEEDED(pOutputType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                WCHAR guidString[128];
                StringFromGUID2(subtype, guidString, 128);
                std::wcout << L"Available output format: " << guidString;
                if (subtype == MFVideoFormat_NV12) {
                    std::wcout << L" (NV12)";
                } else if (subtype == MFVideoFormat_YUY2) {
                    std::wcout << L" (YUY2)";
                } else if (subtype == MFVideoFormat_IYUV) {
                    std::wcout << L" (IYUV)";
                }
                std::wcout << std::endl;
            }

            // NV12フォーマットを優先
            if (SUCCEEDED(pOutputType->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_NV12) {
                // 出力フレームサイズを取得
                UINT32 outWidth = 0, outHeight = 0;
                if (SUCCEEDED(MFGetAttributeSize(pOutputType, MF_MT_FRAME_SIZE, &outWidth, &outHeight))) {
                    width = outWidth;
                    height = outHeight;
                    std::cout << "Output frame size: " << width << "x" << height << std::endl;
                }
                
                hr = decoder->SetOutputType(outputStreamId, pOutputType, 0);
                if (SUCCEEDED(hr)) {
                    outputMediaType = pOutputType;
                    pOutputType->AddRef();  // 保持する場合はAddRefが必要
                    foundOutputType = true;
                    std::cout << "Selected output format: NV12" << std::endl;
                    break;
                }
            }
        }

        if (!foundOutputType) {
            std::cerr << "Failed to set appropriate output type" << std::endl;
            return E_FAIL;
        }

        return S_OK;
    }

public:
    NalDecoder() : inputMediaType(nullptr), outputMediaType(nullptr), initialized(false), 
                   width(0), height(0), inputStreamId(0), outputStreamId(0) {}
    
    ~NalDecoder() {
        Shutdown();
    }

    UINT32 GetWidth() const { return width; }
    UINT32 GetHeight() const { return height; }

    // デコーダー初期化
    HRESULT Initialize() {
        if (initialized) {
            return S_OK;
        }

        // COMの初期化
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize COM. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // Media Foundationの初期化
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize Media Foundation. Error code: 0x" << std::hex << hr << std::endl;
            CoUninitialize();
            return hr;
        }

        // デコーダーの作成
        hr = CreateDecoder();
        if (FAILED(hr)) {
            std::cerr << "Failed to create decoder. Error code: 0x" << std::hex << hr << std::endl;
            Shutdown();
            return hr;
        }

        // メディアタイプの設定
        hr = SetMediaTypes();
        if (FAILED(hr)) {
            std::cerr << "Failed to set media types. Error code: 0x" << std::hex << hr << std::endl;
            Shutdown();
            return hr;
        }

        // MFTを処理状態に設定
        hr = decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        if (FAILED(hr)) {
            std::cerr << "Failed to flush decoder. Error code: 0x" << std::hex << hr << std::endl;
            Shutdown();
            return hr;
        }

        hr = decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (FAILED(hr)) {
            std::cerr << "Failed to notify begin streaming. Error code: 0x" << std::hex << hr << std::endl;
            Shutdown();
            return hr;
        }

        initialized = true;
        std::cout << "Decoder initialized successfully" << std::endl;
        return S_OK;
    }

    // NALユニットのデコード
    HRESULT DecodeNalUnit(const std::vector<uint8_t>& nalUnit, std::vector<uint8_t>& outputFrame) {
        if (!initialized) {
            std::cerr << "Decoder not initialized" << std::endl;
            return E_UNEXPECTED;
        }
        
        // NALユニットタイプの確認（デバッグ用）
        uint8_t unitType = GetNalUnitType(nalUnit);
        std::cout << "Processing NAL unit type: " << static_cast<int>(unitType);
        switch (unitType) {
            case SPS: std::cout << " (SPS)"; break;
            case PPS: std::cout << " (PPS)"; break;
            case CODED_SLICE_IDR: std::cout << " (IDR Slice)"; break;
            case CODED_SLICE_NON_IDR: std::cout << " (Non-IDR Slice)"; break;
            case AUD: std::cout << " (Access Unit Delimiter)"; break;
            case SEI: std::cout << " (SEI)"; break;
            default: break;
        }
        std::cout << ", Size: " << nalUnit.size() << " bytes" << std::endl;
        
        // SPS/PPS/AUDなどのメタデータNALユニットを処理
        if (unitType == SPS || unitType == PPS) {
            // SPSやPPSはデコーダーに送る必要がある
            std::cout << "Processing metadata NAL unit" << std::endl;
        } else if (unitType == AUD || unitType == SEI || 
                unitType == END_OF_SEQUENCE || unitType == END_OF_STREAM) {
            // これらのメタデータはスキップ可能
            std::cout << "Skipping optional metadata NAL unit" << std::endl;
            return S_FALSE;
        }

        // 入力サンプルの作成
        ComPtr<IMFSample> inputSample;
        HRESULT hr = MFCreateSample(&inputSample);
        if (FAILED(hr)) {
            std::cerr << "Failed to create input sample. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // メディアバッファの作成
        ComPtr<IMFMediaBuffer> inputBuffer;
        DWORD bufferSize = nalUnit.size() > UINT_MAX ? UINT_MAX : static_cast<DWORD>(nalUnit.size());
        hr = MFCreateMemoryBuffer(bufferSize, &inputBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create input buffer. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // バッファにデータをコピー
        BYTE* pBuffer = nullptr;
        hr = inputBuffer->Lock(&pBuffer, nullptr, nullptr);
        if (FAILED(hr)) {
            std::cerr << "Failed to lock input buffer. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        memcpy(pBuffer, nalUnit.data(), nalUnit.size());
        hr = inputBuffer->Unlock();
        if (FAILED(hr)) {
            std::cerr << "Failed to unlock input buffer. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // バッファの長さを設定
        hr = inputBuffer->SetCurrentLength(bufferSize);
        if (FAILED(hr)) {
            std::cerr << "Failed to set buffer length. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // サンプルにバッファを追加
        hr = inputSample->AddBuffer(inputBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to add buffer to sample. Error code: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // プロセスの入力
        hr = decoder->ProcessInput(inputStreamId, inputSample, 0);
        if (hr == MF_E_NOTACCEPTING) {
            // デコーダーが入力を受け付けない場合は、出力を処理
            std::cout << "Decoder not accepting input, processing output..." << std::endl;
        }
        else if (FAILED(hr)) {
            std::cerr << "Failed to process input. Error code: 0x" << std::hex << hr << std::endl;
            // SPSやPPSでエラーが出ることがあるが、続行する
            if (unitType != SPS && unitType != PPS) {
                return hr;
            }
        }

        // 出力の処理
        bool haveOutput = false;
        do {
            MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
            outputDataBuffer.dwStreamID = outputStreamId;
            ComPtr<IMFSample> outputSample;
            DWORD status = 0;

            hr = MFCreateSample(&outputSample);
            if (FAILED(hr)) {
                std::cerr << "Failed to create output sample. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // 出力バッファの作成 - NV12フォーマットのサイズは幅x高さx1.5
            DWORD outputBufferSize = width * height * 3 / 2;
            ComPtr<IMFMediaBuffer> outputBuffer;
            hr = MFCreateMemoryBuffer(outputBufferSize, &outputBuffer);
            if (FAILED(hr)) {
                std::cerr << "Failed to create output buffer. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            hr = outputSample->AddBuffer(outputBuffer);
            if (FAILED(hr)) {
                std::cerr << "Failed to add buffer to output sample. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            outputDataBuffer.pSample = outputSample;

            // 出力を処理
            hr = decoder->ProcessOutput(0, 1, &outputDataBuffer, &status);
            
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                // さらに入力データが必要
                std::cout << "Decoder needs more input data" << std::endl;
                break;
            }
            else if (FAILED(hr)) {
                std::cerr << "Failed to process output. Error code: 0x" << std::hex << hr << std::endl;
                if (outputDataBuffer.pSample) {
                    outputDataBuffer.pSample->Release();
                }
                if (outputDataBuffer.pEvents) {
                    outputDataBuffer.pEvents->Release();
                }
                return hr;
            }

            // 出力バッファからデータを取得
            ComPtr<IMFMediaBuffer> buffer;
            hr = outputSample->GetBufferByIndex(0, &buffer);
            if (FAILED(hr)) {
                std::cerr << "Failed to get buffer. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // バッファのロックとデータの取得
            BYTE* pData = nullptr;
            DWORD cbData = 0;
            hr = buffer->Lock(&pData, nullptr, &cbData);
            if (FAILED(hr)) {
                std::cerr << "Failed to lock buffer. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // 出力データをoutputFrameにコピー
            outputFrame.resize(cbData);
            memcpy(outputFrame.data(), pData, cbData);

            hr = buffer->Unlock();
            if (FAILED(hr)) {
                std::cerr << "Failed to unlock buffer. Error code: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            haveOutput = true;
            std::cout << "Decoded frame size: " << cbData << " bytes" << std::endl;

            // リソース解放
            if (outputDataBuffer.pEvents) {
                outputDataBuffer.pEvents->Release();
            }
        } while (false);  // 1回の呼び出しで1つのフレームを処理

        return haveOutput ? S_OK : S_FALSE;
    }

    // デコーダーのシャットダウン
    void Shutdown() {
        if (inputMediaType) {
            inputMediaType->Release();
            inputMediaType = nullptr;
        }

        if (outputMediaType) {
            outputMediaType->Release();
            outputMediaType = nullptr;
        }

        if (decoder) {
            decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            decoder.Release();
        }

        MFShutdown();
        CoUninitialize();
        initialized = false;
        std::cout << "Decoder shutdown complete" << std::endl;
    }
};

// H.264ビデオをデコードしてYUVファイルに保存
bool DecodeH264ToYUV(const std::vector<std::vector<uint8_t>>& nalUnits, const std::string& outputFile) {
    NalDecoder decoder;
    HRESULT hr = decoder.Initialize();
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize decoder, HRESULT: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 出力ファイルを開く
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Failed to create output file: " << outputFile << std::endl;
        return false;
    }

    // 最初にSPSとPPSのNALユニットを処理する
    for (const auto& nalUnit : nalUnits) {
        uint8_t nalType = GetNalUnitType(nalUnit);
        if (nalType == SPS || nalType == PPS) {
            std::vector<uint8_t> dummy;
            hr = decoder.DecodeNalUnit(nalUnit, dummy);
            if (FAILED(hr)) {
                std::cerr << "Failed to process " << (nalType == SPS ? "SPS" : "PPS") 
                         << " NAL unit. Error code: 0x" << std::hex << hr << std::endl;
                // エラーが出ても続行
            }
        }
    }

    // 各NALユニットをデコード
    int framesDecoded = 0;
    for (const auto& nalUnit : nalUnits) {
        uint8_t nalType = GetNalUnitType(nalUnit);
        // SPSとPPSは既に処理済み
        if (nalType != SPS && nalType != PPS) {
            std::vector<uint8_t> decodedFrame;
            hr = decoder.DecodeNalUnit(nalUnit, decodedFrame);
            
            if (hr == S_OK && !decodedFrame.empty()) {
                // デコードされたフレームをファイルに書き込む
                outFile.write(reinterpret_cast<const char*>(decodedFrame.data()), decodedFrame.size());
                framesDecoded++;
            } else if (FAILED(hr)) {
                std::cerr << "Failed to decode NAL unit, HRESULT: 0x" << std::hex << hr << std::endl;
            }
        }
    }
    
    // クリーンアップ
    outFile.close();
    decoder.Shutdown();
    
    if (framesDecoded > 0) {
        std::cout << "Successfully decoded " << framesDecoded << " frames to " << outputFile << std::endl;
        return true;
    } else {
        std::cerr << "No frames were decoded" << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_h264_file> [output_yuv_file]" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = (argc > 2) ? argv[2] : "output.yuv";

    // NALユニットの読み込み
    std::vector<std::vector<uint8_t>> nalUnits = ReadNalUnitsFromFile(inputFile);
    if (nalUnits.empty()) {
        std::cerr << "No NAL units found in file" << std::endl;
        return 1;
    }

    // NALユニットの基本情報を表示
    std::cout << "First NAL unit size: " << nalUnits[0].size() << " bytes" << std::endl;
    if (nalUnits[0].size() > 4) {
        uint8_t nalType = GetNalUnitType(nalUnits[0]);
        std::cout << "First NAL unit type: " << static_cast<int>(nalType) << std::endl;
    }

    // H.264のデコード
    bool success = DecodeH264ToYUV(nalUnits, outputFile);
    
    return success ? 0 : 1;
}