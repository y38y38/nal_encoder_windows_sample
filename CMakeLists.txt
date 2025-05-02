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

# NAL Encoderアプリケーション
add_executable(nal_encoder nal_encoder.cpp)

# Windows固有のリンク設定
if(WIN32)
    # AVCEncoderのライブラリ
    target_link_libraries(avc_encoder
        mfplat
        mfuuid
        mfreadwrite
        strmiids    # DirectShowのライブラリを追加
    )
    
    # NAL Encoderのライブラリ
    target_link_libraries(nal_encoder
        mfplat
        mfuuid
        mfreadwrite
        ole32       # CoInitializeEx/CoUninitializeのため
        # strmiidsライブラリを削除（AMGetErrorTextを使用しないため）
    )
endif()

# 出力ディレクトリの設定
set_target_properties(avc_encoder nal_encoder
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# インストールターゲット
install(TARGETS avc_encoder nal_encoder
    RUNTIME DESTINATION bin
)