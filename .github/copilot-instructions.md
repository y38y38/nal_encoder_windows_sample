# rule
回答は、日本語でお願いします。

# coding rule

## ファイル名
ファイル名はすべて小文字とします。アンダースコア(_)を含めても構いません。

## 型名

型の名前は大文字で始め、単語ごとの頭文字を大文字にします。アンダースコアは使いません。


# ビルド方法

```
del build
mkdir build
cd build
cmake ../ 
cmake --build . --config Release
```
# 単体テスト(あれば)

```
ctest -C Debug --output-on-failure
```

# ログ

consoleの出力は英語です

# コミットメッセージ
Conventional Commits」と呼ばれるコミットメッセージの標準フォーマットを使用すること

feat: - 新機能の追加
fix: - バグ修正
docs: - ドキュメントの変更
style: - コードスタイルの変更（フォーマットなど）
refactor: - リファクタリング（機能変更のないコード修正）
perf: - パフォーマンス改善
test: - テストの追加・修正


# ライブラリの方針

openh264は、動的リンクを使用する
googletestは、静的リンクを使用する

