# NAL Encoderプロジェクト

このプロジェクトは、Windows Media Foundationを使用してH.264エンコーディングを行い、NALユニットとして保存するサンプルアプリケーションです。

## 機能

- Windows Media FoundationのH.264エンコーダーを使用
- 生成されたテストパターン映像をH.264にエンコード
- 出力されたH.264ストリームをNALユニット形式で保存
- NALデータをデコードしてYUVファイルとして保存

## ビルド方法

```
del build
mkdir build
cd build
cmake ../ 
cmake --build . --config Release
```

## 実行方法

ビルドした実行ファイルを実行すると、テストパターンがエンコードされ、`output_nal.h264`というファイル名でNALユニットが保存されます。また、デコード処理によって`output.yuv`というYUVファイルも生成されます。

### YUVファイルの確認方法

生成されたYUVファイルはFFplayを使用して確認することができます。以下のコマンドを使用してください：

```
ffplay -f rawvideo -pixel_format nv12 -video_size 640x480 output.yuv
```

必要に応じて、`-video_size`パラメータをエンコード時に設定した解像度に合わせて変更してください。

## 要件

- Windows 7以降
- Visual Studio 2015以降
- CMake 3.10以降
- (オプション) FFplayによるYUVファイル確認
