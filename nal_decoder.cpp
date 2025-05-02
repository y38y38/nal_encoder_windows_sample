#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>

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

// NALデコーダークラス
class NalDecoder {
private:
    ComPtr<IMFTransform> decoder;
    MFT_REGISTER_TYPE_INFO inputType;
    MFT_REGISTER_TYPE_INFO outputType;
    DWORD inputStreamId;
    DWORD outputStreamId;
    IMFMediaType* inputMediaType;
    IMFMediaType* outputMediaType;
    bool initialized;

    // デコーダーの検索と作成
    HRESULT CreateDecoder() {
        // H.264デコーダーを検索
        IMFActivate** ppActivate = nullptr;
        UINT32 count = 0;
        
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            &inputType,
            &outputType,
            &ppActivate,
            &count
        );

        if (FAILED(hr) || count == 0) {
            std::cerr << "デコーダーが見つかりませんでした。エラーコード: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // 最初のデコーダーを使用
        hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&decoder));
        
        // アクティベートオブジェクトを解放
        for (UINT32 i = 0; i < count; i++) {
            ppActivate[i]->Release();
        }
        CoTaskMemFree(ppActivate);

        if (FAILED(hr)) {
            std::cerr << "デコーダーのアクティベートに失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
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
        
        return hr;
    }

    // メディアタイプの設定
    HRESULT SetMediaTypes() {
        // 入力メディアタイプの設定
        HRESULT hr = MFCreateMediaType(&inputMediaType);
        if (FAILED(hr)) return hr;

        hr = inputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) return hr;

        hr = inputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (FAILED(hr)) return hr;

        hr = decoder->SetInputType(inputStreamId, inputMediaType, 0);
        if (FAILED(hr)) {
            std::cerr << "入力タイプの設定に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // 出力メディアタイプの設定
        // デコーダーが対応する出力タイプを列挙
        for (DWORD i = 0; ; i++) {
            ComPtr<IMFMediaType> pOutputType;
            hr = decoder->GetOutputAvailableType(outputStreamId, i, &pOutputType);
            if (hr == MF_E_NO_MORE_TYPES || hr == E_NOTIMPL) {
                // もう列挙するタイプがない、または列挙がサポートされていない
                if (i == 0) {
                    std::cerr << "対応する出力タイプがありません" << std::endl;
                    return E_FAIL;
                }
                break;
            }
            
            if (FAILED(hr)) {
                std::cerr << "出力タイプの取得に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // NV12フォーマットを選択（一般的な非圧縮フォーマット）
            GUID subtype;
            hr = pOutputType->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (SUCCEEDED(hr) && subtype == MFVideoFormat_NV12) {
                hr = decoder->SetOutputType(outputStreamId, pOutputType, 0);
                if (SUCCEEDED(hr)) {
                    outputMediaType = pOutputType;
                    pOutputType->AddRef();  // 保持する場合はAddRefが必要
                    break;
                }
            }
        }

        if (!outputMediaType) {
            std::cerr << "適切な出力タイプを設定できませんでした" << std::endl;
            return E_FAIL;
        }

        return S_OK;
    }

public:
    NalDecoder() : initialized(false), inputMediaType(nullptr), outputMediaType(nullptr) {
        // 入力タイプにH.264を指定
        inputType.guidMajorType = MFMediaType_Video;
        inputType.guidSubtype = MFVideoFormat_H264;
        
        // 出力タイプに非圧縮ビデオを指定
        outputType.guidMajorType = MFMediaType_Video;
        outputType.guidSubtype = MFVideoFormat_NV12;
    }

    ~NalDecoder() {
        Shutdown();
    }

    // デコーダー初期化
    HRESULT Initialize() {
        if (initialized) {
            return S_OK;
        }

        // COMの初期化
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            std::cerr << "COMの初期化に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // Media Foundationの初期化
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            std::cerr << "Media Foundationの初期化に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
            CoUninitialize();
            return hr;
        }

        // デコーダーの作成
        hr = CreateDecoder();
        if (FAILED(hr)) {
            Shutdown();
            return hr;
        }

        // メディアタイプの設定
        hr = SetMediaTypes();
        if (FAILED(hr)) {
            Shutdown();
            return hr;
        }

        // MFTを処理状態に設定
        hr = decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        if (FAILED(hr)) {
            std::cerr << "デコーダーのフラッシュに失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
            Shutdown();
            return hr;
        }

        hr = decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (FAILED(hr)) {
            std::cerr << "ストリーミング開始通知に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
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
            std::cerr << "デコーダーが初期化されていません" << std::endl;
            return E_UNEXPECTED;
        }

        // 入力サンプルの作成
        ComPtr<IMFSample> inputSample;
        HRESULT hr = MFCreateSample(&inputSample);
        if (FAILED(hr)) return hr;

        // メディアバッファの作成
        ComPtr<IMFMediaBuffer> inputBuffer;
        // size_tからDWORDへの変換警告を抑制
        DWORD bufferSize = 
            nalUnit.size() > UINT_MAX ? UINT_MAX : static_cast<DWORD>(nalUnit.size());
        hr = MFCreateMemoryBuffer(bufferSize, &inputBuffer);
        if (FAILED(hr)) return hr;

        // バッファにデータをコピー
        BYTE* pBuffer = nullptr;
        hr = inputBuffer->Lock(&pBuffer, nullptr, nullptr);
        if (FAILED(hr)) return hr;

        memcpy(pBuffer, nalUnit.data(), nalUnit.size());
        hr = inputBuffer->Unlock();
        if (FAILED(hr)) return hr;

        // バッファの長さを設定
        hr = inputBuffer->SetCurrentLength(bufferSize);
        if (FAILED(hr)) return hr;

        // サンプルにバッファを追加
        hr = inputSample->AddBuffer(inputBuffer);
        if (FAILED(hr)) return hr;

        // プロセスの入力
        hr = decoder->ProcessInput(inputStreamId, inputSample, 0);
        if (hr == MF_E_NOTACCEPTING) {
            // デコーダーが入力を受け付けない場合は、出力を処理
            std::cout << "Decoder not accepting input, processing output..." << std::endl;
        }
        else if (FAILED(hr)) {
            std::cerr << "入力処理に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
            return hr;
        }

        // 出力の処理
        bool haveOutput = false;
        do {
            MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
            outputDataBuffer.dwStreamID = outputStreamId;
            ComPtr<IMFSample> outputSample;
            DWORD status = 0;

            hr = MFCreateSample(&outputSample);
            if (FAILED(hr)) return hr;

            // 出力バッファの作成
            ComPtr<IMFMediaBuffer> outputBuffer;
            hr = MFCreateMemoryBuffer(1024 * 1024, &outputBuffer); // 1MBのバッファを用意
            if (FAILED(hr)) return hr;

            hr = outputSample->AddBuffer(outputBuffer);
            if (FAILED(hr)) return hr;

            outputDataBuffer.pSample = outputSample;

            // 出力を処理
            hr = decoder->ProcessOutput(0, 1, &outputDataBuffer, &status);
            
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                // さらに入力データが必要
                break;
            }
            else if (FAILED(hr)) {
                std::cerr << "出力処理に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
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
                std::cerr << "バッファの取得に失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // バッファのロックとデータの取得
            BYTE* pData = nullptr;
            DWORD cbData = 0;
            hr = buffer->Lock(&pData, nullptr, &cbData);
            if (FAILED(hr)) {
                std::cerr << "バッファのロックに失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
                return hr;
            }

            // 出力データをoutputFrameにコピー
            outputFrame.resize(cbData);
            memcpy(outputFrame.data(), pData, cbData);

            hr = buffer->Unlock();
            if (FAILED(hr)) {
                std::cerr << "バッファのアンロックに失敗しました。エラーコード: 0x" << std::hex << hr << std::endl;
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

// NALユニットをファイルから読み込む関数
std::vector<std::vector<uint8_t>> ReadNalUnitsFromFile(const std::string& filename) {
    std::vector<std::vector<uint8_t>> nalUnits;
    std::ifstream file(filename, std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "ファイルを開けませんでした: " << filename << std::endl;
        return nalUnits;
    }

    // ファイル全体を読み込む
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        std::cerr << "ファイルの読み込みに失敗しました" << std::endl;
        return nalUnits;
    }

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
        size_t nalUnitStart = startCodePos + startCodeSize;
        size_t nalUnitSize = (nextStartCodePos < buffer.size() - 2) ? 
                            nextStartCodePos - nalUnitStart : 
                            buffer.size() - nalUnitStart;

        std::vector<uint8_t> nalUnit(buffer.begin() + startCodePos, buffer.begin() + nalUnitStart + nalUnitSize);
        nalUnits.push_back(nalUnit);

        // 次のNALユニットへ
        pos = (nextStartCodePos < buffer.size() - 2) ? nextStartCodePos : buffer.size();
    }

    std::cout << "Found " << nalUnits.size() << " NAL units in file" << std::endl;
    return nalUnits;
}

// メイン関数
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

    // デコーダーの初期化
    NalDecoder decoder;
    HRESULT hr = decoder.Initialize();
    if (FAILED(hr)) {
        std::cerr << "デコーダーの初期化に失敗しました" << std::endl;
        return 1;
    }

    // 出力ファイルを開く
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "出力ファイルを作成できませんでした: " << outputFile << std::endl;
        return 1;
    }

    // 各NALユニットをデコード
    int framesDecoded = 0;
    for (const auto& nalUnit : nalUnits) {
        std::vector<uint8_t> decodedFrame;
        hr = decoder.DecodeNalUnit(nalUnit, decodedFrame);
        
        if (hr == S_OK && !decodedFrame.empty()) {
            // デコードされたフレームをファイルに書き込む
            outFile.write(reinterpret_cast<const char*>(decodedFrame.data()), decodedFrame.size());
            framesDecoded++;
        }
    }

    // デコーダーのシャットダウン
    decoder.Shutdown();
    outFile.close();

    std::cout << "Decoding complete. " << framesDecoded << " frames decoded to " << outputFile << std::endl;
    return 0;
}