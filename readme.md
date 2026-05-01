# Komugi — 世界第一個軍儀棋 AI 引擎

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)

**Komugi** 是世界上第一個規則完整的軍儀棋 AI 引擎，也是第一個採用神經網路（NNUE）評估的軍儀棋 AI。

> 📖 **規則來源**：本引擎完全採用 **2022 年官方發行之正版軍儀棋規則**（Universal Music 發行，HUNTER×HUNTER 軍儀特別團隊監修）。

軍儀棋是出自漫畫《Hunter x Hunter》的虛構棋類，以其複雜的**疊層機制**和**段數系統**聞名，規則複雜度高於圍棋。

---

## 🏆 開創性

| 項目 | 說明 |
|:---|:---|
| **規則完整度** | ✅ 全球第一個完整實作 2022 官方正版規則的軍儀棋 AI |
| **技術架構** | ✅ 全球第一個使用 NNUE 神經網路評估的軍儀棋 AI |
| **開源先驅** | ✅ 全球第一個開源的軍儀棋 AI 引擎 |

---

## 🎯 功能特點

- ✅ 完整軍儀棋規則（疊層、段數、背叛、飛越攻擊、手駒投放）
- ✅ 完全採用 **2022 官方正版規則**
- ✅ UCI 協議支援（可對接 Arena、Cute Chess 等 GUI）
- ✅ NNUE 神經網路評估（全球首創）
- ✅ Alpha-Beta + 轉置表 + LMR + Killer + History
- ✅ 迭代深化 + 渴望視窗 + 時間控制

---

## 🚀 編譯與執行

### 需求

- C++17 編譯器 (g++ 11+ / clang 14+ / MSVC 2019+)
- （選用）ONNX Runtime — 使用 NNUE 時需要

### 編譯指令

**使用 Makefile：**
```bash
make
手動編譯（不使用 NNUE）：

bash
g++ -O3 -std=c++17 -o komugi main.cpp
注意：需手動註解 komugi_engine_v4_6.h 中的 #define NNUE_ENABLED

手動編譯（使用 NNUE，需安裝 ONNX Runtime）：

bash
g++ -O3 -std=c++17 -o komugi main.cpp -lonnxruntime
執行 UCI 模式
bash
./komugi
然後輸入：

text
uci
position startpos
go depth 10
📂 檔案說明
檔案	說明
komugi_engine_v4_6.h	主引擎程式碼
komugi_nnue.h	NNUE 評估模組
main.cpp	程式入口點
komugi_model.onnx	NNUE 模型檔（選用）
gungi_rulebook_jp.pdf	2022 官方正版規則書
📜 授權
本專案採用 MIT 授權。

text
MIT License

Copyright (c) 2025 Komugi Engine Authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
🙏 致謝
《Hunter x Hunter》原作 — 冨樫義博

2022 官方正版規則 — Universal Music / HUNTER×HUNTER 軍儀特別團隊

NNUE 技術 — Stockfish 團隊

📧 聯絡
若有任何問題或建議，歡迎開 Issue 討論。

⭐ 如果這個專案對你有幫助，請給一顆星！