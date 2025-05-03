cmake_minimum_required(VERSION 3.10)
project(windowsmedia_avcencoder)  

# C++11標準を使用
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studioでは、ソースファイルをUTF-8として扱うための設定
if(MSVC)
  add_compile_options(/utf-8)
endif()

# NAL Encoder & Decoderアプリケーション
add_executable(nal_encode_decode nal_encode_decode.cpp)

# Windows固有のリンク設定
if(WIN32)
    # NAL Encoder & Decoderのライブラリ
    target_link_libraries(nal_encode_decode
        mfplat
        mfuuid
        mfreadwrite
        ole32       # CoInitializeEx/CoUninitializeのため
        # strmiidsライブラリを削除（AMGetErrorTextを使用しないため）
    )
endif()

# 出力ディレクトリの設定
set_target_properties(nal_encode_decode
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# インストールターゲット
install(TARGETS nal_encode_decode
    RUNTIME DESTINATION bin
)