cmake_minimum_required(VERSION 3.10)
project(windowsmedia_avcencoder)  

# C++11標準を使用
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studioでは、ソースファイルをUTF-8として扱うための設定
if(MSVC)
  add_compile_options(/utf-8)
endif()

# AVCEncoderアプリケーション
add_executable(avc_encoder AVCEncoder.cpp)

# NAL Encoder & Decoderアプリケーション
add_executable(nal_encode_decode nal_encode_decode.cpp)

# NAL Decoderアプリケーション
add_executable(nal_decoder nal_decoder.cpp)

# Windows固有のリンク設定
if(WIN32)
    # AVCEncoderのライブラリ
    target_link_libraries(avc_encoder
        mfplat
        mfuuid
        mfreadwrite
        strmiids    # DirectShowのライブラリを追加
    )
    
    # NAL Encoder & Decoderのライブラリ
    target_link_libraries(nal_encode_decode
        mfplat
        mfuuid
        mfreadwrite
        ole32       # CoInitializeEx/CoUninitializeのため
        # strmiidsライブラリを削除（AMGetErrorTextを使用しないため）
    )
    
    # NAL Decoderのライブラリ
    target_link_libraries(nal_decoder
        mfplat
        mfuuid
        mfreadwrite
        ole32       # CoInitializeEx/CoUninitializeのため
        mf          # 追加のMedia Foundationライブラリ
        # wmcodercdspライブラリは利用できないため削除
    )
endif()

# 出力ディレクトリの設定
set_target_properties(avc_encoder nal_encode_decode nal_decoder
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# インストールターゲット
install(TARGETS avc_encoder nal_encode_decode nal_decoder
    RUNTIME DESTINATION bin
)