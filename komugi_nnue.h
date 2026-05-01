/*
  gungi_nnue.h
  NNUE 評估函數接入層（ONNX Runtime）

  使用方式：
    1. 在 main.cpp 或引擎主檔 #include "gungi_nnue.h"
    2. 程式啟動時呼叫 Komugi::NNUE::load("gungi_model.onnx")
    3. 將 evaluate(pos) 替換成 Komugi::NNUE::evaluate(pos)

  特徵向量須與 gungi_selfplay.cpp 的 extract_features() 完全一致
  共 6833 維 float32
*/

#pragma once
#include <array>
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>

// ONNX Runtime C API（單一標頭）
#include <onnxruntime_c_api.h>

// 注意：本檔案被 include 在 namespace Komugi 內部（在 Position 定義之後）
// 因此這裡只需宣告 namespace NNUE，不需要外層 namespace Komugi

namespace NNUE {

// ============================================================
// 常數（須與 train_gungi.py / selfplay 一致）
// ============================================================
constexpr int FEATURE_SIZE = 6833;

// tanh 輸出 [-1, 1] → 引擎分數（整數，與手工評估同尺度）
// 乘以這個係數後轉成 int
constexpr float SCORE_SCALE = 800.0f;  // 可調整，讓 NNUE 分數與手工評估量級相近

// ============================================================
// 全局 ORT 物件（單例）
// ============================================================
static const OrtApi*    g_ort     = nullptr;
static OrtEnv*          g_env     = nullptr;
static OrtSession*      g_session = nullptr;
static OrtSessionOptions* g_opts  = nullptr;
static OrtMemoryInfo*   g_mem     = nullptr;
static bool             g_loaded  = false;

// ============================================================
// 初始化 / 載入模型
// ============================================================
inline void load(const std::string& model_path) {
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_ort) throw std::runtime_error("[NNUE] 無法取得 ORT API");

    // 建立環境（WARNING 等級，避免太多日誌）
    g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "GungiNNUE", &g_env);

    // Session 選項：單執行緒（搜尋已是多執行緒，推論用單核避免競爭）
    g_ort->CreateSessionOptions(&g_opts);
    g_ort->SetIntraOpNumThreads(g_opts, 1);
    g_ort->SetInterOpNumThreads(g_opts, 1);
    g_ort->SetSessionGraphOptimizationLevel(g_opts, ORT_ENABLE_ALL);

    // 載入模型（Windows 需要 wstring）
    std::wstring wpath(model_path.begin(), model_path.end());
    OrtStatus* status = g_ort->CreateSession(g_env, wpath.c_str(), g_opts, &g_session);
    if (status) {
        const char* msg = g_ort->GetErrorMessage(status);
        g_ort->ReleaseStatus(status);
        throw std::runtime_error(std::string("[NNUE] 載入模型失敗: ") + msg);
    }

    // CPU 記憶體
    g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_mem);

    g_loaded = true;
    // 印出確認訊息
    // std::cout << "[NNUE] 模型載入成功: " << model_path << "\n";
}

inline void unload() {
    if (g_session) { g_ort->ReleaseSession(g_session);       g_session = nullptr; }
    if (g_opts)    { g_ort->ReleaseSessionOptions(g_opts);   g_opts    = nullptr; }
    if (g_mem)     { g_ort->ReleaseMemoryInfo(g_mem);        g_mem     = nullptr; }
    if (g_env)     { g_ort->ReleaseEnv(g_env);               g_env     = nullptr; }
    g_loaded = false;
}

// ============================================================
// 特徵抽取（須與 selfplay 的 extract_features 完全一致）
// ============================================================
inline void extract_features(const Position& pos, std::array<float, FEATURE_SIZE>& feat) {
    feat.fill(0.0f);
    int idx = 0;

    Color us   = pos.side_to_move;
    Color them = ~us;

    // ---- 1. 棋盤特徵（81格 × 3層 × 28棋子 = 6804 維）----
    // 每格每層：one-hot 編碼（棋子 ID 1~28，共 28 個位元）
    // 順序：sq=0..80，layer=0..2，piece=1..28
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (int layer = 0; layer < MAX_STACK; ++layer) {
            if (layer < pos.board[sq].height) {
                PieceID p = pos.board[sq].stack[layer];
                if (p != NO_PIECE) {
                    int piece_idx = (int)p - 1;  // 0~27
                    feat[idx + piece_idx] = 1.0f;
                }
            }
            idx += 28;  // 28 棋子
        }
    }
    // idx = 81 * 3 * 28 = 6804

    // ---- 2. 手駒特徵（白方14種 + 黑方14種 = 28 維，數量歸一化）----
    // 最大手駒數約 10，除以 10 歸一化
    for (int t = 1; t <= 14; ++t) {
        feat[idx++] = pos.hand[WHITE][t] / 10.0f;
    }
    for (int t = 1; t <= 14; ++t) {
        feat[idx++] = pos.hand[BLACK][t] / 10.0f;
    }
    // idx = 6804 + 28 = 6832

    // ---- 3. 行棋方（1 維）----
    feat[idx++] = (us == WHITE) ? 1.0f : -1.0f;
    // idx = 6833
}

// ============================================================
// 推論（回傳與 evaluate(pos) 相同尺度的整數分數）
// ============================================================
inline int evaluate(const Position& pos) {
    if (!g_loaded) {
        // 若模型未載入，回傳 0（由呼叫端決定 fallback）
        return 0;
    }

    // 抽取特徵
    static thread_local std::array<float, FEATURE_SIZE> feat;
    extract_features(pos, feat);

    // 建立輸入 Tensor
    const int64_t shape[2] = { 1, FEATURE_SIZE };
    OrtValue* input_tensor = nullptr;
    g_ort->CreateTensorWithDataAsOrtValue(
        g_mem,
        feat.data(),
        FEATURE_SIZE * sizeof(float),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor
    );

    // 推論
    const char* input_names[]  = { "features" };
    const char* output_names[] = { "score" };
    OrtValue* output_tensor = nullptr;

    OrtStatus* status = g_ort->Run(
        g_session, nullptr,
        input_names,  &input_tensor,  1,
        output_names, 1, &output_tensor
    );

    g_ort->ReleaseValue(input_tensor);

    if (status) {
        g_ort->ReleaseStatus(status);
        return 0;
    }

    // 取出結果
    float* out_data = nullptr;
    g_ort->GetTensorMutableData(output_tensor, (void**)&out_data);
    float raw_score = out_data[0];  // tanh 範圍 [-1, 1]
    g_ort->ReleaseValue(output_tensor);

    // 轉換成整數分數（從行棋方視角）
    return static_cast<int>(raw_score * SCORE_SCALE);
}

} // namespace NNUE
// namespace Gungi 由 komugi_engine_v4_6.h 管理
