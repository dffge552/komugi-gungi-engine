/*
  Komugi — 獨立軍儀棋引擎 v1.0
  完全不依賴 Fairy-Stockfish，只使用 C++17 標準庫

  棋子對應（與你的 movegen.cpp 一致）：
  白方小寫，黑方大寫
  y/Y = 帥   q/Q = 大將  x/X = 中將  w/W = 謀
  o/O = 小將  i/I = 侍    u/U = 槍    v/V = 弓
  z/Z = 忍   n/N = 騎馬  p/P = 兵    a/A = 砲
  l/L = 筒   j/J = 砦

  疊棋規則：
  - 每格最多 3 層
  - 攻擊方段數 >= 防守方段數才能吃子（滿層例外：3段吃子無限制）
  - 帥不能被疊

  勝負：帥被吃掉 = 輸
*/

#pragma once
#ifndef KOMUGI_ENGINE_H
#define KOMUGI_ENGINE_H

// ---- NNUE 支援（可選）----
// 若不需要 NNUE，把下面這行註解掉即可
#define NNUE_ENABLED

#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>

#ifdef NNUE_ENABLED
  // gungi_nnue.h 須與本檔案放在同一目錄
  // 並在本檔案之後 include（NNUE 需要 Position 型別）
  // 實際 include 在 namespace 結束後，見檔案底部
#endif

namespace Komugi {

// ---- NNUE forward declaration（Position 定義後才 include 完整實作，這裡先宣告介面）----
// gungi_nnue.h 的實際 include 在 Position class 定義結束後（見下方）

// ============================================================
// 基本常數
// ============================================================

constexpr int BOARD_SIZE  = 9;
constexpr int NUM_SQUARES = 81;
constexpr int MAX_STACK   = 3;
constexpr int MAX_MOVES   = 512;
constexpr int MAX_PLY     = 512;  // 增加到512防止長局Segfault
constexpr int MAX_DEPTH   = 64;
constexpr int INF         = 1000000;
constexpr int MATE        = 900000;

// ============================================================
// 棋子定義
// ============================================================

enum Color : int { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
inline Color operator~(Color c) { return Color(c ^ 1); }

// 棋子 ID (0=空, 1..14=白方棋子, 15..28=黑方棋子)
// 白方=小寫字元: y q x w o i u v z n p a l j
// 黑方=大寫字元: Y Q X W O I U V Z N P A L J
enum PieceID : int {
    NO_PIECE = 0,
    // 白方 (1-14)
    W_SHOU = 1,   // y 帥
    W_DAI  = 2,   // q 大將
    W_CHUU = 3,   // x 中將
    W_HOU  = 4,   // w 謀
    W_SHOU2= 5,   // o 小將
    W_SHI  = 6,   // i 侍
    W_YARI = 7,   // u 槍
    W_YUM  = 8,   // v 弓
    W_NIN  = 9,   // z 忍
    W_KIBA = 10,  // n 騎馬
    W_HEI  = 11,  // p 兵
    W_HOU2 = 12,  // a 砲
    W_TSUT = 13,  // l 筒
    W_TORIDE=14,  // j 砦
    // 黑方 (15-28)
    B_SHOU = 15,  // Y 帥
    B_DAI  = 16,  // Q 大將
    B_CHUU = 17,  // X 中將
    B_HOU  = 18,  // W 謀
    B_SHOU2= 19,  // O 小將
    B_SHI  = 20,  // I 侍
    B_YARI = 21,  // U 槍
    B_YUM  = 22,  // V 弓
    B_NIN  = 23,  // Z 忍
    B_KIBA = 24,  // N 騎馬
    B_HEI  = 25,  // P 兵
    B_HOU2 = 26,  // A 砲
    B_TSUT = 27,  // L 筒
    B_TORIDE=28,  // J 砦
    PIECE_NB = 29
};

inline Color color_of(PieceID p) {
    if (p == NO_PIECE) return NO_COLOR;
    return p <= 14 ? WHITE : BLACK;
}
inline int type_of(PieceID p) {
    if (p == NO_PIECE) return 0;
    return p <= 14 ? p : p - 14;
}
// type_index: 1=帥 2=大將 3=中將 4=謀 5=小將 6=侍 7=槍 8=弓 9=忍 10=騎馬 11=兵 12=砲 13=筒 14=砦
inline PieceID make_piece(Color c, int type) {
    if (type == 0) return NO_PIECE;
    return PieceID(c == WHITE ? type : type + 14);
}

// 字元對應
inline char piece_to_char(PieceID p) {
    static const char tbl[] = {
        '.', 'y','q','x','w','o','i','u','v','z','n','p','a','l','j',
             'Y','Q','X','W','O','I','U','V','Z','N','P','A','L','J'
    };
    return tbl[p];
}

inline PieceID char_to_piece(char c) {
    switch(c) {
        case 'y': return W_SHOU;  case 'Y': return B_SHOU;
        case 'q': return W_DAI;   case 'Q': return B_DAI;
        case 'x': return W_CHUU; case 'X': return B_CHUU;
        case 'w': return W_HOU;  case 'W': return B_HOU;
        case 'o': return W_SHOU2;case 'O': return B_SHOU2;
        case 'i': return W_SHI;  case 'I': return B_SHI;
        case 'u': return W_YARI; case 'U': return B_YARI;
        case 'v': return W_YUM;  case 'V': return B_YUM;
        case 'z': return W_NIN;  case 'Z': return B_NIN;
        case 'n': return W_KIBA; case 'N': return B_KIBA;
        case 'p': return W_HEI;  case 'P': return B_HEI;
        case 'a': return W_HOU2; case 'A': return B_HOU2;
        case 'l': return W_TSUT; case 'L': return B_TSUT;
        case 'j': return W_TORIDE;case 'J':return B_TORIDE;
        default:  return NO_PIECE;
    }
}

// 棋子基礎價值（與 gungi_evaluate.cpp 一致）
constexpr int BASE_VALUE[15] = {
    0,      // 0: 無
    10000,  // 1: 帥
    900,    // 2: 大將
    600,    // 3: 中將
    350,    // 4: 謀
    400,    // 5: 小將
    280,    // 6: 侍
    250,    // 7: 槍
    270,    // 8: 弓
    220,    // 9: 忍
    230,    // 10: 騎馬
    120,    // 11: 兵
    300,    // 12: 砲
    260,    // 13: 筒
    210,    // 14: 砦
};

// ============================================================
// 座標系統
// 格子編號：rank * 9 + file，rank 0 = 白方底線，rank 8 = 黑方底線
// ============================================================

inline int make_sq(int file, int rank) { return rank * 9 + file; }
inline int file_of(int sq) { return sq % 9; }
inline int rank_of(int sq) { return sq / 9; }
inline bool in_board(int sq) { return sq >= 0 && sq < 81; }
inline bool in_board(int f, int r) { return f >= 0 && f < 9 && r >= 0 && r < 9; }

// 方向（格子偏移）
constexpr int DIR_N  =  9;
constexpr int DIR_S  = -9;
constexpr int DIR_E  =  1;
constexpr int DIR_W  = -1;
constexpr int DIR_NE = 10;
constexpr int DIR_NW =  8;
constexpr int DIR_SE = -8;
constexpr int DIR_SW = -10;

// ============================================================
// 移動表示
// from=-1 表示投放（drop）
// ============================================================

// move_flag 值：
//   0 = 普通移動（移到空格）或疊己方棋子
//   1 = 吃子（eat）：移除對方棋子移出場外，己方棋子保留
//   2 = 疊對方（stack enemy）：疊上去共存，對方棋子留在底層
//   3 = 謀的背叛（betray）：謀疊在對方棋子上，打出手牌同種棋子置換對方，
//       drop_type 存放置換的棋子種類
constexpr int8_t MF_NORMAL = 0;
constexpr int8_t MF_EAT    = 1;
constexpr int8_t MF_STACK  = 2;
constexpr int8_t MF_BETRAY = 3;

struct Move {
    int8_t  from;       // -1 = drop
    int8_t  to;
    int8_t  drop_type;  // 投放時的棋子類型 (1-14)；非投放時為 0
    int8_t  move_flag;  // MF_NORMAL / MF_EAT / MF_STACK

    bool is_drop()  const { return from == -1; }
    bool is_valid() const { return to >= 0 && to < 81; }
    bool is_eat()   const { return move_flag == MF_EAT; }
    bool is_stack_enemy() const { return move_flag == MF_STACK; }

    bool operator==(const Move& o) const {
        return from == o.from && to == o.to
            && drop_type == o.drop_type && move_flag == o.move_flag;
    }
    bool operator!=(const Move& o) const { return !(*this == o); }
};

constexpr Move MOVE_NONE = { -1, -1, 0, 0 };

inline Move make_move(int from, int to, int8_t flag = MF_NORMAL) {
    return Move{ (int8_t)from, (int8_t)to, 0, flag };
}
inline Move make_drop(int to, int type) {
    return Move{ -1, (int8_t)to, (int8_t)type, MF_NORMAL };
}
inline Move make_eat(int from, int to) {
    return Move{ (int8_t)from, (int8_t)to, 0, MF_EAT };
}
inline Move make_stack(int from, int to) {
    return Move{ (int8_t)from, (int8_t)to, 0, MF_STACK };
}
// 謀的背叛：from=謀的位置，to=目標格，drop_type=手牌中置換的棋子種類
inline Move make_betray(int from, int to, int drop_type) {
    return Move{ (int8_t)from, (int8_t)to, (int8_t)drop_type, MF_BETRAY };
}

// 移動轉字串（for UCI/輸出）
// 格式：
//   普通移動 / 疊己方：e5e6
//   吃子：              e5e6e
//   疊對方（共存）：    e5e6s
//   投放：              p@d5
inline std::string move_to_str(Move m) {
    if (!m.is_valid()) return "none";
    auto sq_str = [](int sq) -> std::string {
        return std::string(1, 'a' + file_of(sq)) + std::to_string(1 + rank_of(sq));
    };
    if (m.is_drop()) {
        static const char type_char[] = {'.','y','q','x','w','o','i','u','v','z','n','p','a','l','j'};
        return std::string(1, type_char[m.drop_type]) + "@" + sq_str(m.to);
    }
    std::string s = sq_str(m.from) + sq_str(m.to);
    if (m.move_flag == MF_EAT)    s += 'e';
    if (m.move_flag == MF_STACK)  s += 's';
    if (m.move_flag == MF_BETRAY) {
        static const char tc[] = {'.','y','q','x','w','o','i','u','v','z','n','p','a','l','j'};
        s += 'b'; s += tc[m.drop_type];  // 例如 e3e4bw = 謀背叛，打出謀
    }
    return s;
}

// 走法字串解析（共用實作）
// 格式：
//   投放：        p@d5
//   普通/疊己方：e5e6
//   吃子：        e5e6e
//   疊對方：      e5e6s
//   謀背叛：      e5e6bw  （b + 棋子字元，棋子種類即替換的對方棋子種類）
static inline Move parse_move_str(const std::string& s) {
    if (s.size() >= 3 && s[1] == '@') {
        PieceID p = char_to_piece(s[0]);
        int type = type_of(p);
        int f = s[2] - 'a', r = s[3] - '1';
        return make_drop(make_sq(f, r), type);
    }
    if (s.size() >= 4) {
        int ff = s[0] - 'a', fr = s[1] - '1';
        int tf = s[2] - 'a', tr = s[3] - '1';
        int from = make_sq(ff, fr), to = make_sq(tf, tr);
        if (s.size() >= 6 && s[4] == 'b') {
            // 謀背叛：e5e6bw → drop_type = type of s[5]
            PieceID rp = char_to_piece(s[5]);
            int drop_type = type_of(rp);
            return make_betray(from, to, drop_type);
        }
        int8_t flag = MF_NORMAL;
        if (s.size() >= 5) {
            if (s[4] == 'e') flag = MF_EAT;
            if (s[4] == 's') flag = MF_STACK;
        }
        return make_move(from, to, flag);
    }
    return MOVE_NONE;
}

inline Move str_to_move_ex(const std::string& s) { return parse_move_str(s); }
inline Move str_to_move(const std::string& s)    { return parse_move_str(s); }

// ============================================================
// 棋盤狀態
// ============================================================

struct Square {
    PieceID stack[MAX_STACK];  // stack[0]=底層, stack[height-1]=頂層
    int8_t  height;             // 當前疊高（0=空格）

    Square() : height(0) { stack[0] = stack[1] = stack[2] = NO_PIECE; }

    PieceID top() const { return height > 0 ? stack[height-1] : NO_PIECE; }
    int     tier() const { return height; }  // 段數 = 疊高
    bool    empty() const { return height == 0; }
    bool    full() const { return height >= MAX_STACK; }

    void push(PieceID p) {
        if (height < MAX_STACK) stack[height++] = p;
    }
    PieceID pop() {
        if (height == 0) return NO_PIECE;
        PieceID p = stack[--height];
        stack[height] = NO_PIECE;
        return p;
    }
};

// Zobrist 雜湊表
struct Zobrist {
    uint64_t psq[PIECE_NB][NUM_SQUARES][MAX_STACK+1]; // [棋子][格子][段數]
    uint64_t hand[PIECE_NB][20];  // 手駒
    uint64_t side;

    void init() {
        std::mt19937_64 rng(0xDEADBEEF12345678ULL);
        for (auto& a : psq) for (auto& b : a) for (auto& c : b) c = rng();
        for (auto& a : hand) for (auto& b : a) b = rng();
        side = rng();
    }
} g_zobrist;

// ============================================================
// 局面（Position）
// ============================================================

struct StateInfo {
    uint64_t key;
    Move     last_move;
    PieceID  captured[MAX_STACK];  // 被吃掉的棋子（可能是疊子）
    int8_t   capture_count;
    int8_t   from_height;   // 移動前 from 格的高度（用於撤銷）
};

class Position {
public:
    Square   board[NUM_SQUARES];
    int      hand[2][15];          // hand[color][type] = 數量
    Color    side_to_move;
    int      ply;
    StateInfo history[MAX_PLY];

    // Zobrist key
    uint64_t key;

    // ---- 快取：帥的位置（O(1) 查詢）----
    int king_sq[2];   // king_sq[WHITE], king_sq[BLACK]; -1 表示不在場

    // ---- 50步和棋計數器（吃子或投放歸零）----
    int halfmove_clock;

    // ---- 重複局面計數（此局面已出現幾次，含當前）----
    // 由 Engine 維護，這裡只供快速查詢
    int rep_count;   // 搜尋中使用

    Position() { reset(); }

    void reset() {
        for (auto& s : board) s = Square();
        for (auto& c : hand) for (auto& n : c) n = 0;
        side_to_move = WHITE;
        ply = 0;
        key = 0;
        king_sq[0] = king_sq[1] = -1;
        halfmove_clock = 0;
        rep_count = 0;
    }

    // 取得格子頂層棋子
    PieceID piece_on(int sq) const { return board[sq].top(); }
    int     tier(int sq) const { return board[sq].tier(); }
    bool    empty(int sq) const { return board[sq].empty(); }

    // 找帥的位置（優先用快取，快取無效時掃描）
    int find_king(Color c) const {
        if (king_sq[c] >= 0) return king_sq[c];
        // 快取失效，掃描（通常只在 reset 後第一次呼叫）
        PieceID king = make_piece(c, 1);
        for (int sq = 0; sq < NUM_SQUARES; ++sq)
            for (int layer = 0; layer < board[sq].height; ++layer)
                if (board[sq].stack[layer] == king) return sq;
        return -1;
    }

    bool has_king(Color c) const { return find_king(c) >= 0; }

    // ---- 吃子合法性（段數規則）----
    // 攻擊方段數 >= 防守方段數才能吃
    // 例外：攻擊方為3段時，可以吃任何段數
    bool can_capture(int from, int to) const {
        int atk_tier = board[from].tier();
        int def_tier = board[to].tier();
        // 滿層（3段）的格子，只有攻擊方也是3段才能吃
        if (board[to].full()) {
            return atk_tier >= def_tier;
        }
        return atk_tier >= def_tier;
    }

    // ---- 投放目標格合法性 ----
    // 打入規則：
    // 可以打到：空格、己方任何段數的棋子（只要不超過3層）
    // 不能打到：對方棋子、帥所在格、已滿3層
    bool can_drop_to(int sq, Color c) const {
        if (board[sq].full()) return false;           // 已3層：不能打
        if (board[sq].empty()) return true;           // 空格：可以打
        PieceID top = board[sq].top();
        if (type_of(top) == 1) return false;          // 帥不能被疊
        if (color_of(top) != c) return false;         // 對方棋子：不能打
        return true;                                   // 己方棋子（任意段數）：可以打
    }

    // ---- do_move / undo_move ----

    void do_move(Move m) {
        StateInfo& st = history[ply];
        st.key = key;
        st.last_move = m;
        st.capture_count = 0;
        st.from_height = m.is_drop() ? 0 : board[m.from].height;

        if (m.is_drop()) {
            // 投放
            PieceID pc = make_piece(side_to_move, m.drop_type);
            hand[side_to_move][m.drop_type]--;
            key ^= g_zobrist.hand[pc][hand[side_to_move][m.drop_type]];

            int new_tier = board[m.to].height + 1;
            board[m.to].push(pc);
            key ^= g_zobrist.psq[pc][m.to][new_tier];
        } else {
            // 普通移動
            PieceID moving = board[m.from].pop();
            int from_old_tier = board[m.from].height + 1;
            key ^= g_zobrist.psq[moving][m.from][from_old_tier];

            // 目標格有對方棋子 → 吃子（吃掉所有對方棋子）
            Color us = side_to_move;
            Color them = ~us;
            if (!board[m.to].empty()) {
                // 收集並吃掉對方棋子，保留我方棋子
                Square new_to;
                for (int i = 0; i < board[m.to].height; ++i) {
                    PieceID p = board[m.to].stack[i];
                    if (color_of(p) == them) {
                        st.captured[st.capture_count++] = p;
                        // 被吃子加入手駒
                        hand[us][type_of(p)]++;
                        key ^= g_zobrist.hand[p][hand[us][type_of(p)] - 1];
                        key ^= g_zobrist.psq[p][m.to][i + 1];
                    } else {
                        new_to.push(p);
                    }
                }
                board[m.to] = new_to;
            }

            // 放入移動的棋子
            int new_tier = board[m.to].height + 1;
            board[m.to].push(moving);
            key ^= g_zobrist.psq[moving][m.to][new_tier];
        }

        ply++;
        side_to_move = ~side_to_move;
        key ^= g_zobrist.side;
    }

    void undo_move(Move m) {
        --ply;
        side_to_move = ~side_to_move;

        StateInfo& st = history[ply];
        key = st.key; // 直接還原 key

        if (m.is_drop()) {
            PieceID pc = board[m.to].pop();
            hand[side_to_move][type_of(pc)]++;
        } else {
            // 移回頂層棋子
            PieceID moving = board[m.to].pop();

            // 還原被吃掉的棋子到目標格（插回對方位置）
            // 被吃掉的是對方棋子，原本在 to 格的各層
            // 需要重建 to 格的狀態：先把我方棋子移走，插回對方棋子，再放回我方
            // 簡化：我方棋子在被吃後的 to 格是還原前的 to 格，對方棋子在 captured[]
            // 原始 to 格 = 對方棋子（captured[0..n-1]）夾雜我方棋子
            // 最簡單：完整重建

            // 收集當前 to 格的我方棋子
            std::array<PieceID, MAX_STACK> my_pieces;
            int my_count = 0;
            for (int i = 0; i < board[m.to].height; ++i)
                my_pieces[my_count++] = board[m.to].stack[i];

            // 重建 to 格（順序：原本的混合順序，此處簡化為：底層先放我方，再放對方被吃的）
            // 精確還原需要記錄原始層序，這裡用簡化方案：
            // 對方棋子 → 依 captured[] 順序放回，我方棋子放回，最後放 moving
            // 更精確的還原：
            board[m.to] = Square();

            // 還原手駒
            for (int i = 0; i < st.capture_count; ++i) {
                PieceID cp = st.captured[i];
                hand[side_to_move][type_of(cp)]--;
            }

            // 重建 to 格：放回我方棋子（不包含 moving），然後插入被吃的對方棋子
            // 注意：原始層序需要精確記錄，此處用一個近似方案
            // 若需要完美 undo，應在 do_move 中記錄完整的 to 格狀態
            for (int i = 0; i < my_count; ++i)
                board[m.to].push(my_pieces[i]);

            // 將對方棋子插入（原始位置在被我方棋子擠出之前）
            // 這裡最安全的做法：記錄 to 格的原始完整內容
            // 先將 captured 棋子補回
            for (int i = 0; i < st.capture_count; ++i)
                if (board[m.to].height < MAX_STACK)
                    board[m.to].push(st.captured[i]);

            // 還原 from 格
            board[m.from].push(moving);
        }
    }

    // ---- 精確的 do/undo（記錄完整 to 格狀態）----
    // 我們改用一個更精確的版本：

    struct FullState {
        Square  from_sq;
        Square  to_sq;
        uint64_t key;
        Move    move;
        // 被吃的對方棋子（依原始層序）
        PieceID eaten[MAX_STACK];
        int     eaten_count;
        // 相容舊欄位
        int     hand_delta_type;
        int     hand_delta_count;
        // 50步和棋計數器還原
        int     halfmove_clock;
        // 走之前的 king_sq（供 undo 還原）
        int     king_sq_from[2];
    };

    std::array<FullState, MAX_PLY> full_history;

    void do_move_safe(Move m) {
        FullState& fs = full_history[ply];
        fs.key = key;
        fs.move = m;
        fs.from_sq = m.is_drop() ? Square() : board[m.from];
        fs.to_sq = board[m.to];
        fs.hand_delta_type = 0;
        fs.hand_delta_count = 0;
        fs.eaten_count = 0;
        fs.halfmove_clock = halfmove_clock;
        fs.king_sq_from[0] = king_sq[0];
        fs.king_sq_from[1] = king_sq[1];

        Color us = side_to_move;
        Color them = ~us;

        // 50步計數：吃子/投放/疊棋時歸零，否則+1
        bool is_active = m.is_drop() || (m.move_flag == MF_EAT)
                      || (m.move_flag == MF_STACK) || (m.move_flag == MF_BETRAY);
        halfmove_clock = is_active ? 0 : halfmove_clock + 1;

        if (m.is_drop()) {
            PieceID pc = make_piece(us, m.drop_type);
            key ^= g_zobrist.hand[pc][hand[us][m.drop_type] - 1];
            hand[us][m.drop_type]--;
            int new_tier = board[m.to].height + 1;
            board[m.to].push(pc);
            key ^= g_zobrist.psq[pc][m.to][new_tier];

        } else if (m.move_flag == MF_STACK) {
            // 疊對方棋子（stack enemy）：對方棋子留在底層，我方棋子放最上面
            int from_tier = board[m.from].height;
            PieceID moving = board[m.from].pop();
            key ^= g_zobrist.psq[moving][m.from][from_tier];

            // 更新 king_sq（帥移動）
            if (type_of(moving) == 1) king_sq[us] = m.to;

            int new_tier = board[m.to].height + 1;
            board[m.to].push(moving);
            key ^= g_zobrist.psq[moving][m.to][new_tier];

        } else if (m.move_flag == MF_BETRAY) {
            // 謀從 from 移動到 to，疊上對方棋子（ツケ），宣告背叛
            // 規則：
            //   - 保持 to 格原有疊子的相對層序不變
            //   - 將 to 格中所有 drop_type 種類的對方棋子逐一替換為手牌同種棋子
            //   - 被替換的對方棋子移出場外（不加入手牌）
            //   - 謀落在最上層

            // 1. 謀離開 from 格
            int from_tier = board[m.from].height;
            PieceID hou = board[m.from].pop();
            key ^= g_zobrist.psq[hou][m.from][from_tier];

            // 2. 重建 to 格：逐層掃描，同種對方棋子替換為手牌棋子
            //    先把 to 格的 Zobrist 貢獻全部 XOR 掉
            for (int i = 0; i < board[m.to].height; ++i)
                key ^= g_zobrist.psq[board[m.to].stack[i]][m.to][i + 1];

            // 逐層替換
            for (int i = 0; i < board[m.to].height; ++i) {
                PieceID p = board[m.to].stack[i];
                if (color_of(p) == them && type_of(p) == m.drop_type) {
                    // 記錄被移除的棋子
                    if (fs.eaten_count < MAX_STACK)
                        fs.eaten[fs.eaten_count++] = p;
                    if (type_of(p) == 1) king_sq[them] = -1;
                    // 從手牌取出替換棋子
                    PieceID hand_pc = make_piece(us, m.drop_type);
                    key ^= g_zobrist.hand[hand_pc][hand[us][m.drop_type] - 1];
                    hand[us][m.drop_type]--;
                    board[m.to].stack[i] = hand_pc;
                }
            }

            // 重新計算 to 格（替換後）的 Zobrist 貢獻
            for (int i = 0; i < board[m.to].height; ++i)
                key ^= g_zobrist.psq[board[m.to].stack[i]][m.to][i + 1];

            // 3. 謀落在 to 格最上層
            if (type_of(hou) == 1) king_sq[us] = m.to;
            int hou_tier = board[m.to].height + 1;
            board[m.to].push(hou);
            key ^= g_zobrist.psq[hou][m.to][hou_tier];

        } else {
            // 普通移動 or 吃子（MF_NORMAL or MF_EAT）
            int from_tier = board[m.from].height;
            PieceID moving = board[m.from].pop();
            key ^= g_zobrist.psq[moving][m.from][from_tier];

            // 更新 king_sq（帥移動）
            if (type_of(moving) == 1) king_sq[us] = m.to;

            // MF_EAT：移除 to 格對方棋子，保留己方棋子
            if (m.move_flag == MF_EAT && !board[m.to].empty()) {
                for (int i = 0; i < board[m.to].height; ++i) {
                    PieceID p = board[m.to].stack[i];
                    key ^= g_zobrist.psq[p][m.to][i + 1];
                }
                Square new_to;
                for (int i = 0; i < board[m.to].height; ++i) {
                    PieceID p = board[m.to].stack[i];
                    if (color_of(p) == them) {
                        if (fs.eaten_count < MAX_STACK)
                            fs.eaten[fs.eaten_count++] = p;
                        fs.hand_delta_count++;
                        // 被吃的棋子若是對方帥
                        if (type_of(p) == 1) king_sq[them] = -1;
                    } else {
                        new_to.push(p);
                    }
                }
                board[m.to] = new_to;
                for (int i = 0; i < board[m.to].height; ++i) {
                    PieceID p = board[m.to].stack[i];
                    key ^= g_zobrist.psq[p][m.to][i + 1];
                }
            }

            int new_tier = board[m.to].height + 1;
            board[m.to].push(moving);
            key ^= g_zobrist.psq[moving][m.to][new_tier];
        }

        ply++;
        side_to_move = ~side_to_move;
        key ^= g_zobrist.side;
    }

    void undo_move_safe(Move m) {
        --ply;
        side_to_move = ~side_to_move;

        FullState& fs = full_history[ply];
        key = fs.key;
        halfmove_clock = fs.halfmove_clock;
        king_sq[0] = fs.king_sq_from[0];
        king_sq[1] = fs.king_sq_from[1];

        Color us   = side_to_move;
        Color them = ~us;

        if (m.is_drop()) {
            board[m.to] = fs.to_sq;
            hand[us][m.drop_type]++;
        } else if (m.move_flag == MF_STACK) {
            board[m.from] = fs.from_sq;
            board[m.to]   = fs.to_sq;
        } else if (m.move_flag == MF_BETRAY) {
            // 謀移動了（from→to），需還原 from 和 to，以及手牌
            board[m.from] = fs.from_sq;
            board[m.to]   = fs.to_sq;
            hand[us][m.drop_type]++;
        } else {
            board[m.from] = fs.from_sq;
            board[m.to]   = fs.to_sq;
        }
    }

    // 重建 king_sq 快取（在 reset 或 FEN 解析後呼叫）
    void rebuild_king_sq() {
        king_sq[0] = king_sq[1] = -1;
        for (int sq = 0; sq < NUM_SQUARES; ++sq)
            for (int layer = 0; layer < board[sq].height; ++layer) {
                PieceID p = board[sq].stack[layer];
                if (type_of(p) == 1)
                    king_sq[color_of(p)] = sq;
            }
    }

    // 計算完整 Zobrist key（初始化用）
    uint64_t compute_key() const {
        uint64_t k = 0;
        for (int sq = 0; sq < NUM_SQUARES; ++sq) {
            for (int layer = 0; layer < board[sq].height; ++layer) {
                PieceID p = board[sq].stack[layer];
                k ^= g_zobrist.psq[p][sq][layer + 1];
            }
        }
        for (int c = 0; c < 2; ++c)
            for (int t = 1; t <= 14; ++t) {
                PieceID p = make_piece(Color(c), t);
                for (int n = 0; n < hand[c][t]; ++n)
                    k ^= g_zobrist.hand[p][n];
            }
        if (side_to_move == BLACK) k ^= g_zobrist.side;
        return k;
    }

    // ---- 列印棋盤 ----
    std::string to_string() const {
        std::ostringstream ss;
        ss << "   a b c d e f g h i\n";
        ss << "  +------------------+\n";
        for (int r = 8; r >= 0; --r) {
            ss << (r + 1) << " |";
            for (int f = 0; f < 9; ++f) {
                int sq = make_sq(f, r);
                PieceID top = board[sq].top();
                int h = board[sq].height;
                if (top == NO_PIECE) ss << " .";
                else ss << " " << piece_to_char(top);
                // 可選：顯示層數
            }
            ss << " | " << (r + 1) << "\n";
        }
        ss << "  +------------------+\n";
        ss << "   a b c d e f g h i\n";
        ss << (side_to_move == WHITE ? "White" : "Black") << " to move\n";
        return ss.str();
    }
};

// ============================================================
// NNUE 在 Position 定義後立即 include（仍在 namespace Gungi 內）
// 這樣 gungi_nnue.h 裡的 NNUE namespace 可使用 Komugi::Position
// 且後續的 evaluate()、uci_loop() 都能看到 NNUE::
// ============================================================
#ifdef NNUE_ENABLED
  #include "komugi_nnue.h"
#endif

// ============================================================
// 走法生成器
// ============================================================

// 邊界檢查（防止 file 溢出）
inline bool valid_step(int from, int to) {
    if (!in_board(to)) return false;
    // 防止跨越左右邊界（避免 file 溢出）
    int from_f = file_of(from), to_f = file_of(to);
    int df = to_f - from_f;
    if (df > 1 || df < -1) {
        // 水平方向大跳躍需特別處理
        // 只有騎馬和特殊棋子才可能，此處直接用 file 差異判斷
    }
    return true;
}

// 方向有效性（防止繞邊）
inline bool dir_ok(int from, int dir, int steps) {
    int f = file_of(from) + (dir % 9) * steps;
    int r = rank_of(from) + (dir / 9) * steps;
    // 針對斜向方向的 file 檢查
    int dir_f = 0;
    if (dir == DIR_E || dir == DIR_NE || dir == DIR_SE) dir_f =  1;
    if (dir == DIR_W || dir == DIR_NW || dir == DIR_SW) dir_f = -1;
    int new_f = file_of(from) + dir_f * steps;
    return new_f >= 0 && new_f < 9;
}

// 檢查路徑中間是否有段數更高的棋子阻擋（for 筒/砲/弓的飛越規則）
inline bool path_blocked_by_higher(const Position& pos, int from, int dir, int dist, int mover_tier) {
    for (int s = 1; s < dist; ++s) {
        int mid = from + s * dir;
        if (!in_board(mid)) return true;
        if (!pos.empty(mid)) {
            if (pos.tier(mid) > mover_tier) return true;
            // 段數 <= mover_tier：可飛越
        }
    }
    return false;
}

// 路徑是否完全空（for 一般滑行）
inline bool path_clear(const Position& pos, int from, int dir, int steps) {
    for (int s = 1; s < steps; ++s) {
        int mid = from + s * dir;
        if (!in_board(mid) || !pos.empty(mid)) return false;
    }
    return true;
}

class MoveList {
public:
    Move moves[MAX_MOVES];
    int  count = 0;

    void add(Move m) {
        if (count < MAX_MOVES) moves[count++] = m;
    }
    void add(int from, int to) { add(make_move(from, to)); }
    void add_drop(int to, int type) { add(make_drop(to, type)); }

    Move* begin() { return moves; }
    Move* end()   { return moves + count; }
};

// 判斷移動是否合法（段數規則），回傳可行的走法類型
// 回傳 0 = 不能移動
//      MF_NORMAL = 可普通移動（空格）或疊己方
//      MF_EAT    = 可吃子（對方，段數 >= 對方）
//      MF_EAT|MF_STACK<<4 = 可吃也可疊（對方，段數 > 對方）的編碼
// 為保持介面簡單，改用兩個獨立函數：

// 能否移動到目標格（任意種類）
inline bool can_move_to(const Position& pos, int from, int to, Color us) {
    if (!in_board(to)) return false;
    int atk_tier = pos.tier(from);
    PieceID to_pc = pos.piece_on(to);
    PieceID from_top = pos.board[from].top();

    if (to_pc == NO_PIECE) {
        return !pos.board[to].full();  // 空格，不超過3層
    }
    Color to_color = color_of(to_pc);
    // 帥不能被疊
    if (type_of(to_pc) == 1) return false;
    // 帥不能主動疊別人
    if (type_of(from_top) == 1) return false;
    // 不能超過3層
    if (pos.board[to].full()) return false;
    // 核心段數規則：攻擊方段數 >= 目標格段數才能有任何行動
    int def_tier = pos.tier(to);
    return atk_tier >= def_tier;
}

// 目標格是否有對方棋子且可以吃（段數 >= 對方）
inline bool can_eat(const Position& pos, int from, int to, Color us) {
    if (!in_board(to)) return false;
    if (pos.empty(to)) return false;
    // 頂層是對方棋子才能吃（若頂層是己方棋子代表己方在上面，不能吃底下的對方）
    PieceID to_top = pos.piece_on(to);
    if (color_of(to_top) == us) return false;   // 頂層是己方，不能吃
    if (type_of(to_top) == 1) return false;     // 頂層是帥，帥的吃法另計
    // 段數規則：攻擊方格子高度 >= 目標格高度
    int atk = pos.tier(from), def = pos.tier(to);
    if (atk < def) return false;
    // 吃完後 to 格的己方棋子數量 + 1（moving）不能超過3
    int own_count = 0;
    for (int i = 0; i < pos.board[to].height; ++i)
        if (color_of(pos.board[to].stack[i]) == us) ++own_count;
    return (own_count + 1) <= MAX_STACK;
}

// 目標格是否有對方棋子且可以疊（段數 > 對方，且疊完不超過3層）
inline bool can_stack_enemy(const Position& pos, int from, int to, Color us) {
    if (!in_board(to)) return false;
    if (pos.empty(to)) return false;
    PieceID to_top = pos.piece_on(to);
    // 頂層必須是對方棋子才能疊上去
    if (color_of(to_top) == us) return false;
    if (type_of(to_top) == 1) return false;     // 帥不能被疊
    PieceID from_top = pos.board[from].top();
    if (type_of(from_top) == 1) return false;   // 帥不能去疊別人
    int atk = pos.tier(from), def = pos.tier(to);
    // 攻擊方段數 >= 防守方段數才能疊（與吃子條件相同）
    if (atk < def) return false;
    // 疊完高度 = def + 1，不能超過3
    return (def + 1) <= MAX_STACK;
}

// 生成一個棋子的所有走法，加入 MoveList
void generate_piece_moves(const Position& pos, int from, Color us, MoveList& ml) {
    PieceID pc = pos.piece_on(from);
    if (pc == NO_PIECE || color_of(pc) != us) return;
	
	  Color them = ~us; 

    int type = type_of(pc);
    int tier = pos.tier(from);

    // 黑方視角：N↔S、NE↔SW、NW↔SE 翻轉
    // 白方 rank 0 在底（DIR_N = 前進），黑方 rank 8 在底（DIR_S = 前進）
    auto flip = [us](int dir) -> int {
        if (us == WHITE) return dir;
        switch (dir) {
            case DIR_N:  return DIR_S;
            case DIR_S:  return DIR_N;
            case DIR_NE: return DIR_SW;
            case DIR_SW: return DIR_NE;
            case DIR_NW: return DIR_SE;
            case DIR_SE: return DIR_NW;
            default:     return dir;  // E, W 不變
        }
    };

    // 輔助：嘗試加入走法（自動判斷：空格/疊己方=NORMAL，對方棋子=eat+stack）
    auto try_add = [&](int to) {
        if (!in_board(to)) return;
        PieceID to_pc = pos.piece_on(to);
        Color to_color = (to_pc != NO_PIECE) ? color_of(to_pc) : NO_COLOR;

        if (to_color == us) {
            // 疊己方棋子（NORMAL）
            if (can_move_to(pos, from, to, us))
                ml.add(make_move(from, to, MF_NORMAL));
        } else if (to_color == NO_COLOR) {
            // 空格（NORMAL）
            if (can_move_to(pos, from, to, us))
                ml.add(make_move(from, to, MF_NORMAL));
        } else {
            // 對方棋子：生成 eat 和 stack（各自判斷合法性）
            if (can_eat(pos, from, to, us))
                ml.add(make_eat(from, to));
            if (can_stack_enemy(pos, from, to, us))
                ml.add(make_stack(from, to));
        }
    };

    // 輔助：嘗試沿方向走 steps 步（需要路徑暢通）
    auto try_slide = [&](int dir, int steps) {
        if (!dir_ok(from, dir, steps)) return;
        int to = from + steps * dir;
        if (!in_board(to)) return;
        if (!path_clear(pos, from, dir, steps)) return;
        try_add(to);
    };

    // 輔助：飛越走法（中間可飛越段數<=自身的棋子）
    auto try_fly = [&](int dir, int steps) {
        if (!dir_ok(from, dir, steps)) return;
        int to = from + steps * dir;
        if (!in_board(to)) return;
        if (path_blocked_by_higher(pos, from, dir, steps, tier)) return;
        try_add(to);
    };

    switch (type) {

    // ========== 1: 帥 (y/Y) ==========
    // 8方向，段數步數
    case 1: {
        const int dirs[] = { DIR_N, DIR_NE, DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    // ========== 2: 大將 (q/Q) ==========
    // 斜向 segment 步，正向無限滑行（與中將對稱）
    case 2: {
        // 斜向：走 1..tier 步（路徑需空）
        const int diag[] = { DIR_NE, DIR_NW, DIR_SE, DIR_SW };
        for (int d : diag) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        // 正向：無限滑行（直到遇到棋子或邊界）
        const int orth[] = { DIR_N, DIR_S, DIR_E, DIR_W };
        for (int d : orth) {
            for (int s = 1; s <= 8; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                bool has_piece = !pos.empty(to);
                try_add(to);
                if (has_piece) break;
            }
        }
        break;
    }

    // ========== 3: 中將 (x/X) ==========
    // 正向：走 1..tier 步；斜向：無限滑行
    case 3: {
        // 正向：tier 步
        const int orth[] = { DIR_N, DIR_S, DIR_E, DIR_W };
        for (int d : orth) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        // 斜向：無限滑行
        const int diag[] = { DIR_NE, DIR_NW, DIR_SE, DIR_SW };
        for (int d : diag) {
            for (int s = 1; s <= 8; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                bool has_piece = !pos.empty(to);
                try_add(to);
                if (has_piece) break;
            }
        }
        break;
    }

    // ========== 4: 謀 (w/W) ==========
    // 方向：左上(NW)、右上(NE)、正下(S)，走 1..tier 步
    // 特殊：背叛能力——謀移動並疊上對方棋子（ツケ），
    //        若手牌有與目標格頂層對方棋子同種的棋子，
    //        可打出手牌置換對方棋子（對方棋子移出場外，謀留在目標格原位）
    //        → 生成 MF_BETRAY 走法（from→to，drop_type=對方棋子種類）
    case 4: {
        const int dirs[] = { flip(DIR_NW), flip(DIR_NE), flip(DIR_S) };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;

                PieceID to_pc = pos.piece_on(to);
                Color to_color = (to_pc != NO_PIECE) ? color_of(to_pc) : NO_COLOR;

                if (to_color == NO_COLOR || to_color == us) {
                    // 空格或疊己方：普通移動
                    if (can_move_to(pos, from, to, us))
                        ml.add(make_move(from, to, MF_NORMAL));
                } else {
                    // 對方棋子：可吃、可疊，疊時額外檢查背叛
                    if (can_eat(pos, from, to, us))
                        ml.add(make_eat(from, to));
                    if (can_stack_enemy(pos, from, to, us)) {
                        ml.add(make_stack(from, to));
                        // 背叛：謀疊上對方棋子（ツケ）的同時，可選擇宣告背叛
                        // 規則：對方疊子中所有同種棋子必須全部替換（不可只換部分）
                        //       手牌數量必須 >= 目標格中同種對方棋子的數量
                        // enemy_type = 目標格頂層對方棋子的種類（背叛針對的種類）
                        int enemy_type = type_of(to_pc);
                        if (enemy_type != 1) {  // 帥不能被背叛
                            // 計算 to 格中有幾個 enemy_type 的對方棋子
                            int need = 0;
                            for (int i = 0; i < pos.board[to].height; ++i) {
                                PieceID p = pos.board[to].stack[i];
                                if (color_of(p) == them && type_of(p) == enemy_type)
                                    ++need;
                            }
                            // 手牌需 >= need 才能合法背叛（必須全部替換）
                            if (need > 0 && pos.hand[us][enemy_type] >= need) {
                                ml.add(make_betray(from, to, enemy_type));
                            }
                        }
                    }
                }
            }
        }
        break;
    }

    // ========== 5: 小將 (o/O) ==========
    // 6方向(N,NW,NE,W,E,S)，走 1..tier 步
    case 5: {
        const int dirs[] = { flip(DIR_N), flip(DIR_NW), flip(DIR_NE), DIR_W, DIR_E, flip(DIR_S) };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    // ========== 6: 侍 (i/I) ==========
    // 4方向(N,NW,NE,S)，走 1..tier 步
    case 6: {
        const int dirs[] = { flip(DIR_N), flip(DIR_NW), flip(DIR_NE), flip(DIR_S) };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    // ========== 7: 槍 (u/U) ==========
    // 從 movegen.cpp 精確還原：
    // N: 1格, NW:1格, NE:1格, S:1格（1段基本走法）
    // N方向：1段=1,2格 2段=+3格 3段=+4格（滑行，路徑需空）
    // NW,NE：1段=1 2段=2 3段=3格
    // S：1段=1 2段=2 3段=3格
    case 7: {
        const int FN = flip(DIR_N), FS = flip(DIR_S);
        const int FNW = flip(DIR_NW), FNE = flip(DIR_NE);
        // 正前（N）：1格和2格（1段），+3格（2段），+4格（3段）
        { // 1格
            if (in_board(from + FN) && can_move_to(pos, from, from + FN, us))
                ml.add(from, from + FN);
        }
        { // 2格（路徑空）
            int to = from + 2 * FN;
            if (in_board(to) && pos.empty(from + FN) && can_move_to(pos, from, to, us))
                ml.add(from, to);
        }
        if (tier >= 2) {
            int to = from + 3 * FN;
            if (in_board(to) && pos.empty(from + FN) && pos.empty(from + 2 * FN)
                && can_move_to(pos, from, to, us))
                ml.add(from, to);
        }
        if (tier >= 3) {
            int to = from + 4 * FN;
            if (in_board(to) && pos.empty(from + FN) && pos.empty(from + 2 * FN)
                && pos.empty(from + 3 * FN) && can_move_to(pos, from, to, us))
                ml.add(from, to);
        }
        // NW, NE, S: 走 1..tier 格（路徑空）
        const int dirs2[] = { FNW, FNE, FS };
        for (int d : dirs2) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    // ========== 8: 弓 (v/V) ==========
    // S方向: 滑行 tier 格
    // N方向: 飛越，距離 2..tier+1 格（中間可飛越段數<=自身棋子）
    // 左路(NW+N): tier >= 1,2,3
    // 右路(NE+N): 對稱
    case 8: {
        const int FN = flip(DIR_N), FS = flip(DIR_S);
        const int FNW = flip(DIR_NW), FNE = flip(DIR_NE);
        // 後方（S）滑行
        for (int s = 1; s <= tier; ++s) {
            int to = from + s * FS;
            if (!in_board(to)) break;
            if (!path_clear(pos, from, FS, s)) break;
            try_add(to);
            if (!pos.empty(to)) break;
        }
        // 前方（N）飛越 (2..tier+1 格)
        for (int dist = 2; dist <= tier + 1; ++dist) {
            int to = from + dist * FN;
            if (!in_board(to)) break;
            try_fly(FN, dist);
        }
        // 左路：FNW 然後 FN
        if (tier >= 1) {
            int s1 = from + FNW;
            if (in_board(s1) && dir_ok(from, FNW, 1)) {
                int to = s1 + FN;
                if (in_board(to)) {
                    bool blocked = !pos.empty(s1) && pos.tier(s1) > tier;
                    if (!blocked) try_add(to);
                }
            }
        }
        if (tier >= 2) {
            int s1 = from + FNW;
            int s2 = s1 + FN;
            if (in_board(s1) && in_board(s2)) {
                int to = s2 + FNW;
                if (in_board(to)) {
                    bool blocked = (!pos.empty(s1) && pos.tier(s1) > tier)
                                || (!pos.empty(s2) && pos.tier(s2) > tier)
                                || (!pos.empty(to) && pos.tier(to) > tier);
                    if (!blocked) try_add(to);
                }
            }
        }
        if (tier >= 3) {
            int s1 = from + FNW;
            int s2 = s1 + FN;
            int s3 = s2 + FNW;
            if (in_board(s1) && in_board(s2) && in_board(s3)) {
                int to = s3 + FNW;
                if (in_board(to)) {
                    bool blocked = (!pos.empty(s1) && pos.tier(s1) > tier)
                                || (!pos.empty(s2) && pos.tier(s2) > tier)
                                || (!pos.empty(s3) && pos.tier(s3) > tier);
                    if (!blocked) try_add(to);
                }
            }
        }
        // 右路：FNE 然後 FN（對稱）
        if (tier >= 1) {
            int s1 = from + FNE;
            if (in_board(s1) && dir_ok(from, FNE, 1)) {
                int to = s1 + FN;
                if (in_board(to)) {
                    bool blocked = !pos.empty(s1) && pos.tier(s1) > tier;
                    if (!blocked) try_add(to);
                }
            }
        }
        if (tier >= 2) {
            int s1 = from + FNE;
            int s2 = s1 + FN;
            if (in_board(s1) && in_board(s2)) {
                int to = s2 + FNE;
                if (in_board(to)) {
                    bool blocked = (!pos.empty(s1) && pos.tier(s1) > tier)
                                || (!pos.empty(s2) && pos.tier(s2) > tier)
                                || (!pos.empty(to) && pos.tier(to) > tier);
                    if (!blocked) try_add(to);
                }
            }
        }
        if (tier >= 3) {
            int s1 = from + FNE;
            int s2 = s1 + FN;
            int s3 = s2 + FNE;
            if (in_board(s1) && in_board(s2) && in_board(s3)) {
                int to = s3 + FNE;
                if (in_board(to)) {
                    bool blocked = (!pos.empty(s1) && pos.tier(s1) > tier)
                                || (!pos.empty(s2) && pos.tier(s2) > tier)
                                || (!pos.empty(s3) && pos.tier(s3) > tier);
                    if (!blocked) try_add(to);
                }
            }
        }
        break;
    }

    // ========== 9: 忍 (z/Z) ==========
    // 斜向滑行：1段=1,2格 2段=+3格 3段=+4格
    case 9: {
        const int diag[] = { DIR_NW, DIR_NE, DIR_SW, DIR_SE };
        for (int d : diag) {
            // 1格（所有段數）
            if (dir_ok(from, d, 1)) {
                int to1 = from + d;
                if (in_board(to1)) try_add(to1);
            }
            // 2格（路徑空）
            if (dir_ok(from, d, 2)) {
                int to2 = from + 2 * d;
                if (in_board(to2) && pos.empty(from + d)) try_add(to2);
            }
            // 3格（tier >= 2，路徑空）
            if (tier >= 2 && dir_ok(from, d, 3)) {
                int to3 = from + 3 * d;
                if (in_board(to3) && pos.empty(from + d) && pos.empty(from + 2 * d))
                    try_add(to3);
            }
            // 4格（tier >= 3，路徑空）
            if (tier >= 3 && dir_ok(from, d, 4)) {
                int to4 = from + 4 * d;
                if (in_board(to4) && pos.empty(from + d) && pos.empty(from + 2 * d)
                    && pos.empty(from + 3 * d))
                    try_add(to4);
            }
        }
        break;
    }

    // ========== 10: 騎馬 (n/N) ==========
    // N: 1格(固定), N: 2格(1段), N: 3格(2段), N: 4格(3段)
    // W: 1格, 2格(2段), 3格(3段)
    // E: 1格, 2格(2段), 3格(3段)
    // S: 1格(固定), S: 2格, 3格(2段), 4格(3段)
    case 10: {
        const int FN = flip(DIR_N), FS = flip(DIR_S);
        struct DirStep { int dir; int s1, s2, s3; };
        DirStep dirs[] = {
            { FN, 1, 1, 1 },   // 正前固定1格（方向已翻轉）
            { FN, 2, 3, 4 },   // 正前段數步
            { DIR_W, 1, 2, 3 },
            { DIR_E, 1, 2, 3 },
            { FS, 1, 1, 1 },   // 正後固定1格
            { FS, 2, 3, 4 },
        };
        for (auto& d : dirs) {
            // 1段走法
            if (dir_ok(from, d.dir, d.s1)) {
                int to = from + d.s1 * d.dir;
                if (in_board(to) && path_clear(pos, from, d.dir, d.s1))
                    try_add(to);
            }
            // 2段走法（步數更多時才新增）
            if (tier >= 2 && d.s2 > d.s1 && dir_ok(from, d.dir, d.s2)) {
                int to = from + d.s2 * d.dir;
                if (in_board(to) && path_clear(pos, from, d.dir, d.s2))
                    try_add(to);
            }
            // 3段走法
            if (tier >= 3 && d.s3 > d.s2 && dir_ok(from, d.dir, d.s3)) {
                int to = from + d.s3 * d.dir;
                if (in_board(to) && path_clear(pos, from, d.dir, d.s3))
                    try_add(to);
            }
        }
        break;
    }

    // ========== 11: 兵 (p/P) ==========
    // N, S 方向，走 1..tier 步
    case 11: {
        const int dirs[] = { flip(DIR_N), flip(DIR_S) };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    // ========== 12: 砲 (a/A) ==========
    // N方向飛越：距離 3..tier+2，可飛越段數<=自身的中間棋子
    // W, E, S 方向：滑行 tier 格
    case 12: {
        const int FN = flip(DIR_N), FS = flip(DIR_S);
        // 前方（N）飛越（最小3格）
        for (int dist = 3; dist <= tier + 2; ++dist) {
            int to = from + dist * FN;
            if (!in_board(to)) break;
            try_fly(FN, dist);
        }
        // W, E, S 滑行
        const int orth[] = { DIR_W, DIR_E, FS };
        for (int d : orth) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                bool has_piece = !pos.empty(to);
                try_add(to);
                if (has_piece) break;
            }
        }
        break;
    }

    // ========== 13: 筒 (l/L) ==========
    // N方向飛越：距離 2..tier+1，可飛越段數<=自身的中間棋子
    // SW, SE 方向：滑行 tier 格
    case 13: {
        // 前方（N）飛越（最小2格）
        for (int dist = 2; dist <= tier + 1; ++dist) {
            int to = from + dist * flip(DIR_N);
            if (!in_board(to)) break;
            try_fly(flip(DIR_N), dist);
        }
        // 後斜（SW, SE）滑行
        const int diag[] = { flip(DIR_SW), flip(DIR_SE) };
        for (int d : diag) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                bool has_piece = !pos.empty(to);
                try_add(to);
                if (has_piece) break;
            }
        }
        break;
    }

    // ========== 14: 砦 (j/J) ==========
    // 5方向(N,W,E,SW,SE)，走 1..tier 步
    case 14: {
        const int dirs[] = { flip(DIR_N), DIR_W, DIR_E, flip(DIR_SW), flip(DIR_SE) };
        for (int d : dirs) {
            for (int s = 1; s <= tier; ++s) {
                if (!dir_ok(from, d, s)) break;
                int to = from + s * d;
                if (!in_board(to)) break;
                if (!path_clear(pos, from, d, s)) break;
                try_add(to);
            }
        }
        break;
    }

    } // switch
}

// 生成所有合法走法
void generate_all_moves(const Position& pos, MoveList& ml) {
    Color us = pos.side_to_move;

    // 盤面走法
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        if (color_of(pos.piece_on(sq)) != us) continue;
        generate_piece_moves(pos, sq, us, ml);
    }

    // 手駒投放
    // 規則1：只能投在己方陣地（前三排）
    //   白方：rank 0-2；黑方：rank 6-8
    // 規則2：只能投在己方棋子最遠那行「之前」（含該行）
    //   白方：不超過己方最大 rank；黑方：不低於己方最小 rank
    //   初始時（無棋）允許投在整個陣地

    // 計算己方棋子「從底線出發最遠到達的 rank」
    // 白方底線=rank0，前進方向=rank增加，最遠=最大rank
    // 黑方底線=rank8，前進方向=rank減少，最遠=最小rank
    int frontier_rank = -1;
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (int layer = 0; layer < pos.board[sq].height; ++layer) {
            PieceID p = pos.board[sq].stack[layer];
            if (color_of(p) != us) continue;
            int r = rank_of(sq);
            if (us == WHITE) {
                frontier_rank = std::max(frontier_rank, r);
            } else {
                if (frontier_rank < 0) frontier_rank = r;
                else frontier_rank = std::min(frontier_rank, r);
            }
        }
    }

    // 投放範圍：從底線到己方棋子最遠那行（含）
    // 若場上完全沒有己方棋子，允許打在前三排（初始部署）
    int drop_rank_min, drop_rank_max;
    if (us == WHITE) {
        drop_rank_min = 0;
        drop_rank_max = (frontier_rank >= 0) ? frontier_rank : 2;
    } else {
        drop_rank_min = (frontier_rank >= 0) ? frontier_rank : 6;
        drop_rank_max = 8;
    }

    for (int t = 1; t <= 14; ++t) {
        if (pos.hand[us][t] <= 0) continue;
        for (int r = drop_rank_min; r <= drop_rank_max; ++r) {
            for (int f = 0; f < BOARD_SIZE; ++f) {
                int sq = make_sq(f, r);
                if (pos.can_drop_to(sq, us))
                    ml.add_drop(sq, t);
            }
        }
    }
}

// ============================================================
// 評估函數
// ============================================================

// 段數加成（與 gungi_evaluate.cpp 一致）
constexpr double TIER_MULT[4] = { 0.0, 1.0, 1.5, 2.0 };

int piece_value(int type, int tier) {
    if (type <= 0 || type > 14) return 0;
    return static_cast<int>(BASE_VALUE[type] * TIER_MULT[tier]);
}

// 切比雪夫距離
inline int chebyshev(int sq1, int sq2) {
    return std::max(std::abs(file_of(sq1) - file_of(sq2)),
                    std::abs(rank_of(sq1) - rank_of(sq2)));
}

// 計算一個格子是否受到指定顏色的棋子攻擊（簡化：鄰格威脅）
// 用於帥安全評估：檢查帥周圍幾格有多少敵方棋子
inline int count_attackers_near(const Position& pos, int king_sq, Color attacker) {
    int count = 0;
    int kr = rank_of(king_sq), kf = file_of(king_sq);
    // 檢查 3x3 範圍內的敵方棋子
    for (int dr = -2; dr <= 2; ++dr) {
        for (int df = -2; df <= 2; ++df) {
            if (dr == 0 && df == 0) continue;
            int r = kr + dr, f = kf + df;
            if (!in_board(f, r)) continue;
            int sq = make_sq(f, r);
            if (!pos.empty(sq) && color_of(pos.piece_on(sq)) == attacker)
                count++;
        }
    }
    return count;
}

// 計算帥的可逃格數（快速版：只看8方向第1格是否被封）
// 比完整走法生成快 10x，足夠用於評估函數
int king_mobility(const Position& pos, Color c) {
    int king_sq = pos.find_king(c);
    if (king_sq < 0) return 0;
    Color enemy = ~c;
    int count = 0;
    const int dirs[] = { DIR_N, DIR_NE, DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW };
    for (int d : dirs) {
        int dir_f = 0;
        if (d == DIR_E || d == DIR_NE || d == DIR_SE) dir_f =  1;
        if (d == DIR_W || d == DIR_NW || d == DIR_SW) dir_f = -1;
        int new_f = file_of(king_sq) + dir_f;
        if (new_f < 0 || new_f >= 9) continue;
        int to = king_sq + d;
        if (!in_board(to)) continue;
        // 目標格：空格或可吃的敵方棋子，且段數規則允許
        PieceID to_pc = pos.piece_on(to);
        if (to_pc == NO_PIECE) { ++count; continue; }
        if (color_of(to_pc) == c) continue;  // 己方棋子擋路
        // 敵方棋子：帥段數(1) >= 對方段數才能吃
        if (pos.tier(king_sq) >= pos.tier(to)) ++count;
    }
    return count;
}

// ============================================================
// ★ 修復4：真正的攻擊偵測（基於走法生成）
// 原本只看鄰格有沒有棋子，完全錯誤：槍/弓/砲可以遠距離攻擊
// 改為：生成攻擊方所有走法，看有沒有走法能到達目標格
// ============================================================
bool sq_attacked_by(const Position& pos, int target_sq, Color attacker) {
    // 生成攻擊方所有走法，檢查是否有走法可以到達 target_sq
    // 注意：這裡不能用 generate_all_moves（它包含投放），只看盤面棋子
    for (int from = 0; from < NUM_SQUARES; ++from) {
        if (pos.empty(from)) continue;
        PieceID p = pos.piece_on(from);
        if (color_of(p) != attacker) continue;

        // 用 generate_piece_moves 生成該棋子走法，看有沒有到 target_sq
        MoveList ml;
        generate_piece_moves(pos, from, attacker, ml);
        for (int i = 0; i < ml.count; ++i) {
            if (ml.moves[i].to == target_sq) return true;
        }
    }
    return false;
}

// ============================================================
// ★ 修復5：真正的將軍偵測（基於走法生成）
// 判斷 color c 的帥是否被對方將軍（即帥的位置被對方任何棋子攻擊）
// ============================================================
bool is_in_check(const Position& pos, Color c) {
    int ksq = pos.find_king(c);
    if (ksq < 0) return false;  // 帥不在場（已被吃）
    return sq_attacked_by(pos, ksq, ~c);
}

// ============================================================
// 計算目標格附近有多少我方棋子可以攻擊到（簡化：直接計算在1格鄰格內的棋子）
// 用於偵測我方棋子是否在敵帥攻擊範圍
int count_direct_threats(const Position& pos, int king_sq, Color attacker) {
    int count = 0;
    int kr = rank_of(king_sq), kf = file_of(king_sq);
    // 1格鄰格內（切比雪夫距離=1）的攻擊方棋子
    for (int dr = -1; dr <= 1; ++dr) {
        for (int df = -1; df <= 1; ++df) {
            if (dr == 0 && df == 0) continue;
            int r = kr + dr, f = kf + df;
            if (!in_board(f, r)) continue;
            int sq = make_sq(f, r);
            if (!pos.empty(sq) && color_of(pos.piece_on(sq)) == attacker)
                count++;
        }
    }
    return count;
}

// ============================================================
// SEE：靜態換子評估（非遞迴簡化版）
// 只算「吃子後對方能反吃嗎？」一層
// 回傳正值=吃了划算，負值=不划算
// ============================================================
int see(const Position& pos, int to_sq, Color attacker) {
    PieceID target = pos.piece_on(to_sq);
    if (target == NO_PIECE) return 0;
    int gain = BASE_VALUE[type_of(target)] * pos.tier(to_sq);

    // 找攻擊方最小價值的鄰近攻擊棋子
    int min_atk_val = INF;
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        PieceID p = pos.piece_on(sq);
        if (color_of(p) != attacker) continue;
        if (chebyshev(sq, to_sq) > 2) continue;
        if (!can_move_to(pos, sq, to_sq, attacker)) continue;
        int val = BASE_VALUE[type_of(p)];
        if (val < min_atk_val) min_atk_val = val;
    }
    if (min_atk_val == INF) return 0;  // 沒人能吃

    // 對方能反吃嗎？找對方最小價值的鄰近棋子
    Color defender = ~attacker;
    int min_def_val = INF;
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        PieceID p = pos.piece_on(sq);
        if (color_of(p) != defender) continue;
        if (chebyshev(sq, to_sq) > 2) continue;
        if (!can_move_to(pos, sq, to_sq, defender)) continue;
        int val = BASE_VALUE[type_of(p)];
        if (val < min_def_val) min_def_val = val;
    }

    // 吃子得益 - 若對方能反吃，最多損失攻擊棋子的價值
    if (min_def_val == INF) return gain;  // 對方無法反吃，純賺
    return gain - min_atk_val;           // 能反吃：淨得 = 吃到的 - 自己損失
}

// 攻王動線：計算從 from 到 king_sq 方向是否暢通
// 回傳 0=完全暢通, 1=一個阻擋, 2+=嚴重阻擋
int count_obstacles(const Position& pos, int from, int king_sq) {
    int fr = rank_of(from), ff = file_of(from);
    int kr = rank_of(king_sq), kf = file_of(king_sq);
    int dr = (kr > fr) ? 1 : (kr < fr) ? -1 : 0;
    int df = (kf > ff) ? 1 : (kf < ff) ? -1 : 0;
    if (dr == 0 && df == 0) return 0;
    // 只沿直線/斜線走
    int obstacles = 0;
    int r = fr + dr, f = ff + df;
    while (r != kr || f != kf) {
        if (!in_board(f, r)) break;
        int sq = make_sq(f, r);
        if (!pos.empty(sq)) ++obstacles;
        r += dr; f += df;
    }
    return obstacles;
}

// 攻王動線評估：計算我方棋子對敵帥的攻擊通道品質
// 長兵器（大將/中將/槍/筒/弓）需要暢通，砲/筒需要借子飛越
int attack_lane_bonus(const Position& pos, int from, int king_sq, int piece_type, Color us) {
    if (king_sq < 0) return 0;
    int dist = chebyshev(from, king_sq);
    if (dist > 6) return 0;  // 太遠無影響

    int obs = count_obstacles(pos, from, king_sq);
    int bonus = 0;

    switch (piece_type) {
    case 2:  // 大將：斜向/正向無限滑行，暢通時很強
    case 3:  // 中將：同上
        if (obs == 0) bonus = (7 - dist) * 25;  // 暢通：強力攻擊
        else if (obs == 1) bonus = 5;             // 一個阻擋：還行
        break;
    case 7:  // 槍：正前方滑行，暢通極強
        if (obs == 0) bonus = (7 - dist) * 30;
        break;
    case 8:  // 弓：飛越攻擊，有棋子反而可以借
        // 暢通時：正常飛越攻擊
        // 有1個阻擋：借子飛越，更強
        if (obs == 0) bonus = (7 - dist) * 20;
        else if (obs == 1) bonus = (7 - dist) * 35;  // 借子飛越最強
        else bonus = 5;
        break;
    case 12: // 砲：前方飛越，有棋子更好
        if (obs == 1) bonus = (7 - dist) * 35;
        else if (obs == 0) bonus = (7 - dist) * 10;  // 暢通反而不能飛越
        else bonus = 5;
        break;
    case 13: // 筒：同砲邏輯
        if (obs == 1) bonus = (7 - dist) * 30;
        else if (obs == 0) bonus = (7 - dist) * 15;
        break;
    case 9:  // 忍：斜跳，不需要暢通
        bonus = (5 - std::min(dist, 5)) * 15;
        break;
    case 10: // 騎馬：跳躍
        bonus = (5 - std::min(dist, 5)) * 12;
        break;
    default:
        if (obs == 0) bonus = (6 - std::min(dist, 6)) * 8;
        break;
    }
    return bonus;
}


// ============================================================
// 帥暴露度評分：逃格越少 = 越危險，非線性放大
// 同時考慮「被幾個敵方棋子直接攻擊」
// ============================================================
int king_danger_score(const Position& pos, Color king_color) {
    int ksq = pos.find_king(king_color);
    if (ksq < 0) return 3000;

    Color enemy = ~king_color;
    Color own   = king_color;
    int danger  = 0;

    // ---- 1. 敵方棋子威脅（核心：敵方靠近才是危險）----
    // 2格內：每個敵方棋子 +100
    int enemy_near = count_attackers_near(pos, ksq, enemy);
    danger += enemy_near * 100;

    // 1格直接威脅：額外 +120（降低單威脅跳幅，原 200）
    int direct = count_direct_threats(pos, ksq, enemy);
    danger += direct * 120;

    // ---- 2. 帥的真實逃格計算（用真正的走法生成判斷每格是否被攻擊）----
    // ★ 修復10：原本 sq_attacked_by 只看鄰格，現在已改為真正的走法生成
    int trapped = 0;
    int safe_escape = 0;
    const int dirs[] = {DIR_N,DIR_NE,DIR_E,DIR_SE,DIR_S,DIR_SW,DIR_W,DIR_NW};
    for (int d : dirs) {
        int dir_f = 0;
        if (d==DIR_E||d==DIR_NE||d==DIR_SE) dir_f =  1;
        if (d==DIR_W||d==DIR_NW||d==DIR_SW) dir_f = -1;
        int nf = file_of(ksq) + dir_f;
        if (nf < 0 || nf >= 9) continue;
        int to = ksq + d;
        if (!in_board(to)) continue;
        PieceID to_pc = pos.piece_on(to);
        if (to_pc != NO_PIECE && color_of(to_pc) == own) continue;
        bool covered = sq_attacked_by(pos, to, enemy);
        if (covered) ++trapped;
        else         ++safe_escape;
    }
    // 被封死的逃格：非線性懲罰（降低跳幅，讓評分更穩定）
    if (safe_escape == 0 && direct >= 1) {
        // 真正將死威脅：敵方直接威脅且無安全逃格
        danger += 400;  // 原 800 → 400
    } else if (safe_escape <= 1 && enemy_near >= 2) {
        danger += 200;  // 原 300 → 200
    } else if (safe_escape <= 2) {
        danger += 80;   // 原 100 → 80
    }

    // ---- 3. 己方棋子護衛加分（保護帥是好事，抵消危險）----
    int own_guard = count_attackers_near(pos, ksq, own);
    danger -= own_guard * 30;  // 每個護衛棋子減30危險分
    danger = std::max(0, danger);  // 危險度最低為0

    return danger;
}

// 主評估函數（從行動方角度）
int evaluate(const Position& pos) {
#ifdef NNUE_ENABLED
    if (NNUE::g_loaded) return NNUE::evaluate(pos);
#endif
    Color us   = pos.side_to_move;
    Color them = ~us;

    int our_king   = pos.find_king(us);
    int their_king = pos.find_king(them);

    if (our_king < 0)   return -(MATE - 1);
    if (their_king < 0) return  (MATE - 1);

    int score = 0;

    // ---- 物質分數（含段數） ----
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (int layer = 0; layer < pos.board[sq].height; ++layer) {
            PieceID p = pos.board[sq].stack[layer];
            if (p == NO_PIECE) continue;
            int t = layer + 1;
            int val = piece_value(type_of(p), t);
            if (color_of(p) == us)   score += val;
            else                     score -= val;
        }
    }

    // ---- 手駒價值（40%基礎值）----
    for (int t = 1; t <= 14; ++t) {
        int my_hand    = pos.hand[us][t];
        int their_hand = pos.hand[them][t];
        score += (my_hand - their_hand) * BASE_VALUE[t] * 40 / 100;
    }

    // ---- 帥疊段獎勵（天然屏障 + 逃生空間）----
    // 帥在高段位有雙重優勢：
    //   防禦：只有對方同段以上才能威脅
    //   機動：段數越高移動格數越多（1段=8格, 2段=16格, 3段=24格）
    // 但底層棋子暫時失去行動力，需要扣除損失
    for (int c_i = 0; c_i < 2; ++c_i) {
        Color c = Color(c_i);
        int ksq = pos.find_king(c);
        if (ksq < 0) continue;
        int ktier = pos.board[ksq].height;

        // 段數基礎獎勵：2段=300, 3段=600（非線性）
        int tier_bonus = 0;
        if (ktier == 2) tier_bonus = 300;
        if (ktier == 3) tier_bonus = 600;

        // 扣除底層棋子的行動力損失（壓著的棋子 × 40% 基礎值）
        // 邏輯：棋子沒死，只是暫時不能動，損失約 40%
        for (int layer = 0; layer < ktier - 1; ++layer) {
            PieceID under = pos.board[ksq].stack[layer];
            if (under == NO_PIECE) continue;
            int utype = type_of(under);
            if (color_of(under) == c) {
                // 己方棋子被壓：扣損失
                // 兵/侍(120-280)壓著 = 小損失，值得疊
                // 大將/中將(600-900)壓著 = 大損失，不值得疊
                tier_bonus -= BASE_VALUE[utype] * 40 / 100;
            }
            // 對方棋子在底層（stack enemy後帥疊上去）：不計損失，是優勢
        }

        // 淨加分：壓弱棋子划算，壓強棋子虧損
        if (c == us) score += tier_bonus;
        else         score -= tier_bonus;
    }

    // ---- 帥危險度（進攻權重略高於防守）----
    {
        int their_danger = king_danger_score(pos, them);
        int our_danger   = king_danger_score(pos, us);
        // 進攻加分 x1.3，防守扣分 x1.0（讓引擎更積極進攻）
        score += their_danger * 13 / 10;
        score -= our_danger;
    }

    // ---- 進攻性：距離加權 + 攻王動線評估（加強版）----
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        PieceID p = pos.piece_on(sq);
        Color c = color_of(p);
        int type = type_of(p);
        if (type == 1) continue;

        int enemy_king = (c == us) ? their_king : our_king;
        int dist = chebyshev(sq, enemy_king);

        // 基礎距離分（加強：dist<=2 時暴增）
        int proximity;
        if (dist == 0) proximity = 300;       // 疊在帥旁（不可能但保底）
        else if (dist == 1) proximity = 250;  // 直接威脅帥
        else if (dist == 2) proximity = 180;  // 一步就到
        else proximity = std::max(0, (8 - dist)) * (8 - dist);

        // 攻王動線加分（權重提升 1.5x）
        int lane_bonus = attack_lane_bonus(pos, sq, enemy_king, type, c) * 3 / 2;

        if (c == us) score += proximity + lane_bonus;
        else         score -= proximity + lane_bonus;
    }

    // ---- 棋子在敵陣獎勵（最後兩排爆炸性加分）----
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        PieceID p = pos.piece_on(sq);
        Color c = color_of(p);
        if (type_of(p) == 1) continue;  // 帥不計前進獎勵
        int r = rank_of(sq);
        int adv;
        if (c == WHITE) {
            if      (r >= 7) adv = r * 8 + 200;  // rank 7-8：爆炸性獎勵
            else if (r >= 5) adv = r * 8 + 80;   // rank 5-6：進入敵陣加分
            else             adv = r * 8;
        } else {
            int inv = 8 - r;
            if      (inv >= 7) adv = inv * 8 + 200;
            else if (inv >= 5) adv = inv * 8 + 80;
            else               adv = inv * 8;
        }
        if (c == us) score += adv;
        else         score -= adv;
    }

    // ---- 疊棋獎勵（依棋子種類和位置）----
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        int h = pos.board[sq].height;
        if (h < 2) continue;  // 2段以上才計疊棋獎勵
        PieceID top = pos.piece_on(sq);
        Color c = color_of(top);
        int type = type_of(top);
        if (type == 1) continue;  // 帥不計疊棋獎勵（帥不應該疊）

        // 棋子價值越高，疊棋價值越大（高價值棋子叠高段更強）
        int piece_val = BASE_VALUE[type];
        int val_factor = (piece_val >= 600) ? 2 : (piece_val >= 300) ? 1 : 0;

        int enemy_king = (c == us) ? their_king : our_king;
        int dist = chebyshev(sq, enemy_king);

        // 基礎疊棋獎勵
        int base_bonus = h * (20 + val_factor * 10);

        // 攻王疊棋：靠近敵帥的高段棋子極度危險
        int attack_bonus;
        if (dist <= 1)      attack_bonus = h * (100 + val_factor * 30);
        else if (dist <= 2) attack_bonus = h * (60 + val_factor * 20);
        else if (dist <= 4) attack_bonus = h * (20 + val_factor * 5);
        else                attack_bonus = 0;

        // 防守疊棋（靠近己方帥）：不給進攻加分，只給小額基礎加分
        int our_king_dist = chebyshev(sq, (c == us) ? our_king : their_king);
        if (our_king_dist <= 1) attack_bonus = attack_bonus / 4;  // 防守疊棋打折

        if (c == us) score += base_bonus + attack_bonus;
        else         score -= base_bonus + attack_bonus;
    }

    // ---- 棋子協調：多棋子包圍敵帥（平滑版）----
    {
        int our_near  = count_attackers_near(pos, their_king, us);
        int their_near = count_attackers_near(pos, our_king,  them);
        // 修復：降低協調加分跨步驟跳幅，改用線性加分
        // 原 n=2給250，n=3給600，差350；現在每個棋子固定加分，更平滑
        auto coord_bonus = [](int n) -> int {
            if (n <= 1) return 0;
            return n * 100;  // 每個圍攻棋子 100 分，線性累積
        };
        score += coord_bonus(our_near);
        score -= coord_bonus(their_near);
    }

    // ---- 護衛獎勵（己方護衛加分，對方護衛扣分）----
    {
        score += count_attackers_near(pos, our_king,   us)   * 15;
        score -= count_attackers_near(pos, their_king, them) * 10;
    }

    // ---- 直接威脅獎勵（獨立計算：1格內貼臉威脅）----
    // 修復：降低單一威脅加分（原 n=1 給 500，n=2 給 1200，跨步驟差 700 導致分數崩）
    // 改用更平滑的線性加分，讓分數不會因一步棋進退就暴漲暴跌
    {
        int dt_them = count_direct_threats(pos, their_king, us);
        int dt_us   = count_direct_threats(pos, our_king,   them);
        auto threat_val = [](int n) -> int {
            if (n == 0) return 0;
            if (n == 1) return 250;   // 原 500 → 250（降低單威脅跳幅）
            if (n == 2) return 550;   // 原 1200 → 550
            return 900;               // 原 2000 → 900
        };
        score += threat_val(dt_them);
        score -= threat_val(dt_us);
    }

    // ---- 時鐘壓力：非線性懲罰，讓「拖延」越來越貴 ----
    // ★ 修復3：原本每步只扣2分，到第50步才扣100分，完全不夠阻止循環
    // 改用非線性：前30步輕微，之後快速增加，模擬西洋棋引擎的 contempt + tempo 壓力
    {
        int clk = pos.halfmove_clock;
        int clk_penalty;
        if (clk <= 10)       clk_penalty = clk * 3;
        else if (clk <= 30)  clk_penalty = 30 + (clk - 10) * 8;
        else if (clk <= 60)  clk_penalty = 190 + (clk - 30) * 15;
        else                 clk_penalty = 640 + (clk - 60) * 25;
        score -= clk_penalty;
    }

    // ---- 中心控制（輕微）----
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        if (pos.empty(sq)) continue;
        PieceID p = pos.piece_on(sq);
        Color c = color_of(p);
        int center_dist = std::abs(file_of(sq) - 4) + std::abs(rank_of(sq) - 4);
        int bonus = std::max(0, 4 - center_dist) * 7;  // 從 3 提升到 7
        if (c == us) score += bonus;
        else         score -= bonus;
    }

    return score;
}

// ============================================================
// 搜尋引擎
// ============================================================

// 轉置表
struct TTEntry {
    uint64_t key;
    int16_t  score;
    uint8_t  depth;
    uint8_t  bound; // 0=none 1=exact 2=alpha 3=beta
    Move     best_move;
};

constexpr int TT_SIZE = 1 << 22; // ~4M 條目

struct TranspositionTable {
    TTEntry entries[TT_SIZE];

    void clear() { std::memset(entries, 0, sizeof(entries)); }

    bool probe(uint64_t key, TTEntry& out) const {
        const TTEntry& e = entries[key % TT_SIZE];
        if (e.key == key) { out = e; return true; }
        return false;
    }

    void store(uint64_t key, int score, int depth, int bound, Move best) {
        TTEntry& e = entries[key % TT_SIZE];
        if (e.key != key || depth >= e.depth) {
            e.key  = key;
            e.score = (int16_t)std::clamp(score, -30000, 30000);
            e.depth = (uint8_t)std::min(depth, 255);
            e.bound = (uint8_t)bound;
            e.best_move = best;
        }
    }
} g_tt;

// 移動評分（用於排序）
struct ScoredMove {
    Move move;
    int  score;
    bool operator>(const ScoredMove& o) const { return score > o.score; }
};

// MVV-LVA 吃子分數
int mvv_lva(const Position& pos, Move m) {
    if (m.is_drop()) return 0;
    PieceID victim   = pos.piece_on(m.to);
    PieceID attacker = pos.piece_on(m.from);
    if (victim == NO_PIECE) return 0;
    int v_val = BASE_VALUE[type_of(victim)]   * pos.tier(m.to);
    int a_val = BASE_VALUE[type_of(attacker)] * pos.tier(m.from);
    return v_val * 100 - a_val;
}

class Engine {
public:
    std::atomic<bool> stopped { false };
    int64_t time_limit_ms = 0;
    int64_t start_time = 0;
    uint64_t nodes = 0;
    int seldepth = 0;

    Move     killer[MAX_PLY][2];
    int      history[2][NUM_SQUARES][NUM_SQUARES];
    // Countermove：對每個對方走法，記錄我方最佳反制走法
    Move     countermove[NUM_SQUARES][NUM_SQUARES];

    // 重複局面偵測
    static constexpr int MAX_GAME_PLY = 1024;
    uint64_t game_keys[MAX_GAME_PLY];
    int      game_ply = 0;

    Engine() {
        g_zobrist.init();
        clear();
    }

    void clear() {
        g_tt.clear();
        std::memset(killer, 0, sizeof(killer));
        std::memset(history, 0, sizeof(history));
        std::memset(countermove, 0, sizeof(countermove));
        std::memset(game_keys, 0, sizeof(game_keys));
        game_ply = 0;
    }

    void new_game_key(uint64_t key) {
        game_ply = 0;
        game_keys[game_ply++] = key;
    }

    void push_game_key(uint64_t key) {
        if (game_ply < MAX_GAME_PLY)
            game_keys[game_ply++] = key;
    }

    // 只清局面歷史，保留 TT（跨局面可重用）
    void reset_game_keys() {
        std::memset(game_keys, 0, sizeof(game_keys));
        game_ply = 0;
    }

    // 計算 key 在歷史對局路徑中的重複次數（不含當前局面本身）
    // game_keys[game_ply-1] 是當前局面，只往前找同方走後的局面（每2步一個）
    int count_history_reps(uint64_t key) const {
        int count = 0;
        // 從 game_ply-3 開始往前，每次跳2步（同方向局面）
        for (int i = (int)game_ply - 3; i >= 0; i -= 2)
            if (game_keys[i] == key) ++count;
        return count;
    }

    // 搜尋路徑內出現幾次（rep_path 是祖先節點走後的 key 列表，只比對同方向）
    int count_path_reps(uint64_t key,
                        const uint64_t* search_path, int search_len) const {
        int count = 0;
        // 從 search_len-2 開始往前每隔2步比對（同方走後的局面）
        for (int i = search_len - 2; i >= 0; i -= 2)
            if (search_path[i] == key) ++count;
        return count;
    }

    int64_t now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    bool time_up() const {
        if (time_limit_ms <= 0) return false;
        return (now_ms() - start_time) >= time_limit_ms;
    }

    // 排序走法列表
    void score_moves(const Position& pos, MoveList& ml, Move tt_move, int ply) {
        for (int i = 0; i < ml.count; ++i) {
            Move m = ml.moves[i];
            int s = 0;
            if (m == tt_move) {
                s = 2000000;
            } else {
                bool is_capture = !m.is_drop() && !pos.empty(m.to)
                                  && color_of(pos.piece_on(m.to)) != pos.side_to_move;
                if (is_capture) {
                    s = 1000000 + mvv_lva(pos, m);
                } else if (!m.is_drop()) {
                    if (m == killer[ply][0]) s = 900000;
                    else if (m == killer[ply][1]) s = 800000;
                    else s = history[pos.side_to_move][m.from][m.to];
                } else {
                    // 投放：依棋子價值
                    s = 500000 + BASE_VALUE[m.drop_type];
                }
            }
            // 存入一個 scored 陣列暫時不用，直接用 selection sort
            ml.moves[i].drop_type = ml.moves[i].drop_type; // no-op
            // 把分數存到 _pad 位（借用，或用另一種方式）
            // 我們改用 selection sort 中直接計算
            (void)s;
        }
    }

    int get_move_score(const Position& pos, Move m, Move tt_move, int ply) {
        if (m == tt_move) return 2000000;
        bool is_capture = !m.is_drop() && m.move_flag == MF_EAT;
        if (is_capture) {
            // 吃帥：最高優先
            if (type_of(pos.piece_on(m.to)) == 1) return 3000000;
            // SEE 正值（划算的吃子）優先，負值（虧本吃子）降級
            int see_val = see(pos, m.to, pos.side_to_move);
            if (see_val >= 0)
                return 1000000 + mvv_lva(pos, m);
            else
                return 200000 + mvv_lva(pos, m);  // 虧本吃子排在 killer 之後
        }
        // 找對方帥位置，判斷走法是否靠近敵帥
        Color us = pos.side_to_move;
        Color them = ~us;
        int their_king = pos.find_king(them);
        int threat_bonus = 0;
        if (their_king >= 0) {
            int dist = chebyshev(m.to, their_king);
            if (dist <= 1) threat_bonus = 400000;  // 直接威脅帥
            else if (dist <= 2) threat_bonus = 100000;  // 近距離
        }
        if (!m.is_drop()) {
            if (ply < MAX_PLY && m == killer[ply][0]) return 900000 + threat_bonus;
            if (ply < MAX_PLY && m == killer[ply][1]) return 800000 + threat_bonus;
            // Countermove heuristic
            if (ply > 0) {
                TTEntry tte_prev;
                // 用 history 的前一步走法取得 countermove
                // 簡化：直接用 pos.full_history[pos.ply-1].move
                if (pos.ply > 0) {
                    Move prev = pos.full_history[pos.ply - 1].move;
                    if (!prev.is_drop() && prev.is_valid()) {
                        if (countermove[prev.from][prev.to] == m)
                            return 750000 + threat_bonus;
                    }
                }
            }
            return threat_bonus + history[pos.side_to_move][m.from][m.to];
        }
        // 投放：靠近敵帥的投放優先
        return 500000 + BASE_VALUE[m.drop_type] + threat_bonus;
    }

    // 選擇最高分走法放到 idx 位置
    Move pick_best(Position& pos, MoveList& ml, int start, Move tt_move, int ply) {
        int best_score = -INF;
        int best_idx   = start;
        for (int i = start; i < ml.count; ++i) {
            int s = get_move_score(pos, ml.moves[i], tt_move, ply);
            if (s > best_score) { best_score = s; best_idx = i; }
        }
        std::swap(ml.moves[start], ml.moves[best_idx]);
        return ml.moves[start];
    }

    // Quiescence Search
    int quiescence(Position& pos, int alpha, int beta, int ply) {
        if (stopped.load(std::memory_order_relaxed) || time_up()) return 0;
        ++nodes;
        if (ply > seldepth) seldepth = ply;
        if (ply >= MAX_PLY - 10) return evaluate(pos);
        if (pos.ply >= MAX_PLY - 2) return evaluate(pos);

        // ★ 修復8：被將軍時展開所有走法（不能只看吃子，否則將死無法偵測）
        bool in_check_qs = is_in_check(pos, pos.side_to_move);
        if (in_check_qs) {
            MoveList ml;
            generate_all_moves(pos, ml);
            if (ml.count == 0) return -(MATE - ply);

            int best_qs = -INF;
            for (int i = 0; i < ml.count; ++i) {
                Move m = pick_best(pos, ml, i, MOVE_NONE, ply);
                if (!m.is_drop()) {
                    if (m.move_flag == MF_EAT && !can_eat(pos, m.from, m.to, pos.side_to_move)) continue;
                    if (m.move_flag == MF_STACK && !can_stack_enemy(pos, m.from, m.to, pos.side_to_move)) continue;
                    if (m.move_flag == MF_NORMAL && !can_move_to(pos, m.from, m.to, pos.side_to_move)) continue;
                }
                pos.do_move_safe(m);
                int score;
                if (pos.find_king(pos.side_to_move) < 0) {
                    score = MATE - ply - 1;
                } else {
                    score = -quiescence(pos, -beta, -alpha, ply + 1);
                }
                pos.undo_move_safe(m);
                if (score > best_qs) best_qs = score;
                if (score >= beta) return score;
                if (score > alpha) alpha = score;
            }
            return best_qs;
        }

        // 非將軍狀態：stand_pat + 只展開吃子/威脅走法
        int stand_pat = evaluate(pos);
        if (stand_pat >= beta) return stand_pat;
        if (stand_pat > alpha) alpha = stand_pat;

        Color qs_us = pos.side_to_move;
        int qs_their_king = pos.find_king(~qs_us);

        MoveList ml2;
        generate_all_moves(pos, ml2);

        for (int i = 0; i < ml2.count; ++i) {
            Move m = pick_best(pos, ml2, i, MOVE_NONE, ply);

            bool is_capture = !m.is_drop() && m.move_flag == MF_EAT;
            if (m.move_flag == MF_STACK) continue;

            bool is_threat = !m.is_drop()
                             && (qs_their_king >= 0)
                             && chebyshev(m.to, qs_their_king) <= 1;

            if (!is_capture && !is_threat) continue;
            if (!m.is_drop()) {
                if (m.move_flag == MF_EAT && !can_eat(pos, m.from, m.to, qs_us)) continue;
                if (m.move_flag == MF_NORMAL && !can_move_to(pos, m.from, m.to, qs_us)) continue;
            }

            // Delta pruning
            if (is_capture && !is_threat) {
                int cap_val = BASE_VALUE[type_of(pos.piece_on(m.to))] * pos.tier(m.to);
                if (stand_pat + cap_val + 200 < alpha) continue;
            }

            pos.do_move_safe(m);
            int score;
            if (pos.find_king(pos.side_to_move) < 0) {
                score = MATE - ply - 1;
            } else {
                score = -quiescence(pos, -beta, -alpha, ply + 1);
            }
            pos.undo_move_safe(m);

            if (score >= beta) return score;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    // Alpha-Beta Negamax
    int alpha_beta(Position& pos, int depth, int alpha, int beta, int ply,
                   bool is_pv, uint64_t* rep_path, int rep_len) {
        if (stopped.load(std::memory_order_relaxed) || time_up()) return 0;
        ++nodes;
        // 棧溢出保護
        if (ply >= MAX_PLY - 2 || pos.ply >= MAX_PLY - 2) return evaluate(pos);

        // ---- 50步和棋 ----
        if (pos.halfmove_clock >= 100) {
            // 優勢方（score > 0）接受和棋虧損；劣勢方接受和棋是好事
            // 讓引擎在優勢時不輕易接受50步和棋
            int static_score = evaluate(pos);
            if (static_score > 200)  return -200;  // 優勢方：和棋是壞事
            if (static_score < -200) return  200;  // 劣勢方：和棋是好事
            return 0;
        }

        // ---- 重複局面偵測（循環 = 行動方輸）----
        // ★ 修復2：任何重複（>=1次）直接判負，讓引擎永遠不願意循環
        // 西洋棋引擎（Stockfish）做法：搜尋路徑中出現重複 → 返回 contempt（輕蔑值）
        // 軍儀棋規則：循環走法 = 行動方輸，所以直接返回 LOSS
        int path_reps = count_path_reps(pos.key, rep_path, rep_len);
        int hist_reps = count_history_reps(pos.key);
        int total_reps = path_reps + hist_reps;

        if (total_reps >= 1) {
            // 循環懲罰：不是固定的 MATE 值，而是依 ply 遞減
            // 這樣引擎會傾向「越早避免循環越好」，而非在深層才迴避
            return -(MATE - ply - 1);
        }

        // 深度耗盡 → 靜態搜尋
        if (depth <= 0) return quiescence(pos, alpha, beta, ply);

        // Mate 距離剪枝
        int alpha_orig = alpha;
        alpha = std::max(alpha, -(MATE - ply));
        beta  = std::min(beta,   MATE - ply + 1);
        if (alpha >= beta) return alpha;

        // 轉置表
        uint64_t key = pos.key;
        TTEntry tte;
        Move tt_move = MOVE_NONE;
        if (g_tt.probe(key, tte)) {
            tt_move = tte.best_move;
            if (!is_pv && tte.depth >= depth) {
                int ts = tte.score;
                if (tte.bound == 1) return ts;
                if (tte.bound == 2 && ts <= alpha) return alpha;
                if (tte.bound == 3 && ts >= beta)  return beta;
            }
        }

        // ---- 靜態評估（供 RFP / Razoring / NMP 使用）----
        // 只在非 PV 且非根節點時計算，避免 PV 節點被過早截斷
        int static_eval = -INF;
        bool eval_computed = false;
        auto get_eval = [&]() -> int {
            if (!eval_computed) { static_eval = evaluate(pos); eval_computed = true; }
            return static_eval;
        };

        if (!is_pv && ply > 0) {
            int ev = get_eval();

            // ---- Reverse Futility Pruning (RFP / Static NMP) ----
            // 只在 depth <= 4 啟用（depth <= 7 時 margin 過大，容易截斷將殺序列）
            if (depth <= 4 && ev >= beta + 120 * depth)
                return ev;

            // ---- Razoring ----
            // 若靜態分數遠低於 alpha，跳進 qsearch 而不做完整展開
            if (depth <= 2 && ev + 400 * depth <= alpha) {
                int razor_score = quiescence(pos, alpha, beta, ply);
                if (razor_score <= alpha) return razor_score;
            }

            // ---- Null Move Pruning ----
            // ★ 修復6：用真正的將軍偵測（原本只看1格鄰近，會漏掉遠程棋子的將軍）
            {
                bool in_check = is_in_check(pos, pos.side_to_move);
                if (depth >= 3 && !in_check && ply < MAX_PLY - 4) {
                    if (ev >= beta) {
                        int R = 3 + depth / 6 + std::min(3, (ev - beta) / 300);
                        pos.side_to_move = ~pos.side_to_move;
                        pos.key ^= g_zobrist.side;
                        int null_score = -alpha_beta(pos, depth - R - 1, -beta, -beta + 1,
                                                     ply + 1, false, rep_path, rep_len);
                        pos.side_to_move = ~pos.side_to_move;
                        pos.key ^= g_zobrist.side;
                        if (null_score >= beta) return beta;
                    }
                }
            }
        }

        // IID：沒有 TT 走法且深度夠時，先做淺搜找好走法
        if (is_pv && tt_move == MOVE_NONE && depth >= 6) {
            alpha_beta(pos, depth - 3, alpha, beta, ply, false, rep_path, rep_len);
            if (g_tt.probe(key, tte)) tt_move = tte.best_move;
        }

        // Singular Extension：TT 命中且走法明顯優於其他走法時，延伸搜尋
        // ply < 8 限制：防止深層遞迴棧溢出
        bool singular_extension = false;
        TTEntry tte_sing;
        bool tte_hit = g_tt.probe(key, tte_sing);
        if (!is_pv && depth >= 6 && ply < 8 && tt_move.is_valid() && tte_hit
            && tte_sing.depth >= depth - 3 && tte_sing.bound == 3) {
            int sing_beta  = (int)tte_sing.score - depth * 2;
            int sing_score = alpha_beta(pos, depth / 2, sing_beta - 1, sing_beta,
                                        ply, false, rep_path, rep_len);
            if (sing_score < sing_beta) singular_extension = true;
        }

        // 生成走法
        MoveList ml;
        generate_all_moves(pos, ml);

        if (ml.count == 0) return -(MATE - ply); // 無合法走法

        int  best_score = -INF;
        Move best_move  = MOVE_NONE;
        int  move_count = 0;

        // Futility Pruning 預算（depth <= 2）
        int futility_eval = (depth <= 2) ? get_eval() : -INF;

        // LMP 閾值：依深度決定，超過幾個走法就跳過靜態走法
        // 軍儀棋分支因子大，用比西洋棋更保守的值
        constexpr int LMP_TABLE[6] = { 0, 6, 12, 24, 40, 60 };
        int lmp_threshold = (depth <= 5) ? LMP_TABLE[depth] : 9999;

        for (int i = 0; i < ml.count; ++i) {
            Move m = pick_best(pos, ml, i, tt_move, ply);

            // 驗證走法合法性（段數規則）
            if (!m.is_drop()) {
                if (m.move_flag == MF_EAT) {
                    if (!can_eat(pos, m.from, m.to, pos.side_to_move)) continue;
                } else if (m.move_flag == MF_STACK) {
                    if (!can_stack_enemy(pos, m.from, m.to, pos.side_to_move)) continue;
                } else {
                    if (!can_move_to(pos, m.from, m.to, pos.side_to_move)) continue;
                }
            }

            ++move_count;
            bool is_capture = !m.is_drop() && m.move_flag == MF_EAT;

            // ---- Futility Pruning（depth <= 2）----
            if (depth <= 2 && !is_capture && move_count > 1) {
                int margin = depth == 1 ? 300 : 500;
                if (futility_eval + margin <= alpha) continue;
            }

            // ---- LMP：走法數量超過閾值，跳過靜態走法 ----
            if (!is_pv && !is_capture && move_count > lmp_threshold) continue;

            // ---- SEE Pruning：負 SEE 的吃子（虧本的）在深層可以跳過 ----
            if (!is_pv && is_capture && depth >= 3) {
                int see_val = see(pos, m.to, pos.side_to_move);
                if (see_val < -50 * depth) continue;
            }
            // 非吃子走法：SEE 非常差時在淺層跳過
            if (!is_pv && !is_capture && !m.is_drop() && depth <= 5) {
                int see_val = see(pos, m.to, pos.side_to_move);
                if (see_val < -80 * depth) continue;
            }

            // ---- LMR（Late Move Reductions）----
            int new_depth = depth - 1;
            int reduction = 0;
            if (depth >= 3 && move_count > 3 && !is_capture) {
                // 基礎公式（與 Stockfish 類似）
                reduction = (int)(0.75 + std::log(depth) * std::log(move_count) / 2.25);

                // 調整項
                if (is_pv) reduction = std::max(0, reduction - 1);  // PV 節點少減
                if (ply < MAX_PLY && (m == killer[ply][0] || m == killer[ply][1]))
                    reduction = std::max(0, reduction - 1);          // killer 少減
                // History 高的走法少減
                if (!m.is_drop()) {
                    int hist_val = history[pos.side_to_move][m.from][m.to];
                    if (hist_val > 8000) reduction = std::max(0, reduction - 1);
                }
                reduction = std::min(reduction, depth - 2);  // 不能減到 <= 0 深度
                reduction = std::max(0, reduction);
            }

            // Singular Extension：只有 TT 走法才延伸
            if (singular_extension && m == tt_move) new_depth += 1;

            pos.do_move_safe(m);

            // ★ 修復7：將軍延伸 — 用真正的 is_in_check
            {
                Color them_after = pos.side_to_move;
                if (is_in_check(pos, them_after)) {
                    new_depth += 1;

                    // ★ 修復11：立即將死偵測
                    // 對方被將軍且無合法走法 → 將死，不需要繼續遞迴
                    MoveList check_ml;
                    generate_all_moves(pos, check_ml);
                    bool has_legal = false;
                    for (int ci = 0; ci < check_ml.count && !has_legal; ++ci) {
                        Move cm = check_ml.moves[ci];
                        if (!cm.is_drop()) {
                            if (cm.move_flag == MF_EAT && !can_eat(pos, cm.from, cm.to, pos.side_to_move)) continue;
                            if (cm.move_flag == MF_STACK && !can_stack_enemy(pos, cm.from, cm.to, pos.side_to_move)) continue;
                            if (cm.move_flag == MF_NORMAL && !can_move_to(pos, cm.from, cm.to, pos.side_to_move)) continue;
                        }
                        // 走了之後帥還安全嗎？
                        pos.do_move_safe(cm);
                        bool still_check = is_in_check(pos, ~pos.side_to_move);
                        pos.undo_move_safe(cm);
                        if (!still_check) { has_legal = true; }
                    }
                    if (!has_legal) {
                        pos.undo_move_safe(m);
                        int mate_score = MATE - ply - 1;
                        if (mate_score > best_score) {
                            best_score = mate_score;
                            best_move  = m;
                        }
                        if (mate_score >= beta) {
                            g_tt.store(key, mate_score, depth, 3, m);
                            return mate_score;
                        }
                        if (mate_score > alpha) alpha = mate_score;
                        continue;
                    }
                }
            }

            bool captured_king = (pos.find_king(pos.side_to_move) < 0);

            // 修復：存走後的 key（走前的 key 子節點永遠比對不到）
            if (rep_len < MAX_PLY) rep_path[rep_len] = pos.key;
            int new_rep_len = std::min(rep_len + 1, MAX_PLY - 1);

            int score;
            if (captured_king) {
                score = MATE - ply - 1;
            } else if (move_count == 1) {
                score = -alpha_beta(pos, new_depth, -beta, -alpha, ply + 1, is_pv,
                                    rep_path, new_rep_len);
            } else {
                // PVS + LMR
                score = -alpha_beta(pos, new_depth - reduction, -alpha - 1, -alpha, ply + 1, false,
                                    rep_path, new_rep_len);
                // LMR re-search at full depth
                if (score > alpha && reduction > 0)
                    score = -alpha_beta(pos, new_depth, -alpha - 1, -alpha, ply + 1, false,
                                        rep_path, new_rep_len);
                // PV re-search with full window
                if (score > alpha && is_pv)
                    score = -alpha_beta(pos, new_depth, -beta, -alpha, ply + 1, true,
                                        rep_path, new_rep_len);
            }

            pos.undo_move_safe(m);

            if (stopped.load(std::memory_order_relaxed) || time_up()) return 0;

            if (score > best_score) {
                best_score = score;
                best_move  = m;
                if (score > alpha) {
                    alpha = score;
                    if (score >= beta) {
                        // Beta 截斷
                        if (!is_capture && !m.is_drop() && ply < MAX_PLY) {
                            if (m != killer[ply][0]) {
                                killer[ply][1] = killer[ply][0];
                                killer[ply][0] = m;
                            }
                            int bonus = depth * depth;
                            history[pos.side_to_move][m.from][m.to] +=
                                bonus - history[pos.side_to_move][m.from][m.to] * std::abs(bonus) / 16384;
                            if (pos.ply > 0) {
                                Move prev = pos.full_history[pos.ply - 1].move;
                                if (!prev.is_drop() && prev.is_valid())
                                    countermove[prev.from][prev.to] = m;
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (move_count == 0) return -(MATE - ply);

        int bound = best_score >= beta ? 3 : (best_score > alpha_orig ? 1 : 2);
        g_tt.store(key, best_score, depth, bound, best_move);

        return best_score;
    }

    // 反覆運算深化
    Move search(Position& pos, int max_depth, int64_t time_ms) {
        stopped.store(false);
        nodes     = 0;
        seldepth  = 0;
        start_time   = now_ms();
        time_limit_ms = time_ms;

        Move best = MOVE_NONE;
        int  best_score = -INF;

        for (int depth = 1; depth <= max_depth; ++depth) {
            if (time_up() || stopped.load()) break;

            int alpha = -INF, beta = INF;
            int delta = 25;  // Aspiration window 初始寬度

            // Aspiration windows（depth >= 5）
            if (depth >= 5 && std::abs(best_score) < MATE - 100) {
                alpha = best_score - delta;
                beta  = best_score + delta;
            }

            int score;
            while (true) {
                // ★ 修復1：把歷史對局 key 當作 rep_path 前綴傳入
                // 原本 rep_path 每次都是空的，搜尋無法偵測跨搜尋的循環
                uint64_t rep_path[MAX_PLY] = {};
                int rep_len = 0;
                {
                    int copy_len = std::min((int)game_ply, MAX_PLY - 2);
                    for (int k = 0; k < copy_len; ++k)
                        rep_path[k] = game_keys[k];
                    rep_len = copy_len;
                }
                score = alpha_beta(pos, depth, alpha, beta, 0, true, rep_path, rep_len);

                if (stopped.load() || time_up()) break;

                if (score <= alpha) {
                    // fail-low：只縮小 alpha，beta 保持不動（原本錯誤地縮小了 beta）
                    alpha = std::max(score - delta, -INF);
                    delta += delta / 2;
                } else if (score >= beta) {
                    beta = std::min(score + delta, INF);
                    delta += delta / 2;
                } else {
                    break;
                }
            }

            if (stopped.load() || time_up()) break;

            // 從 TT 取得最佳走法
            TTEntry tte;
            if (g_tt.probe(pos.key, tte) && tte.best_move.is_valid()) {
                best = tte.best_move;
                best_score = score;

                int64_t ms = now_ms() - start_time;
                uint64_t nps = ms > 0 ? nodes * 1000 / ms : 0;

                std::cout << "info depth " << depth
                          << " seldepth " << seldepth
                          << " score ";
                if (std::abs(score) >= MATE - MAX_PLY)
                    std::cout << "mate " << ((score > 0 ? MATE - score + 1 : -(MATE + score + 1)) / 2);
                else
                    std::cout << "cp " << score;
                std::cout << " nodes " << nodes
                          << " nps " << nps
                          << " time " << ms
                          << " pv " << move_to_str(best)
                          << "\n";
                std::cout.flush();
            }

            if (std::abs(score) >= MATE - MAX_PLY) break; // 找到必勝
            if (depth > 1 && time_up()) break;
        }

        // 後備：返回第一個合法走法
        if (!best.is_valid()) {
            MoveList ml;
            generate_all_moves(pos, ml);
            for (int i = 0; i < ml.count; ++i) {
                Move m = ml.moves[i];
                if (m.is_drop() || can_move_to(pos, m.from, m.to, pos.side_to_move)) {
                    best = m;
                    break;
                }
            }
        }

        return best;
    }
};

// 全局引擎實例
inline Engine& engine() {
    static Engine e;
    return e;
}

// ============================================================
// 簡易 UCI 迴圈
// ============================================================

inline void uci_loop() {
    Position pos;
    g_zobrist.init();

    std::string line, token;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        ss >> token;

        if (token == "uci") {
            std::cout << "id name Komugi v4.6\n"
                      << "id author KomugiEngine\n"
                      << "option name Hash type spin default 64 min 1 max 2048\n"
                      << "uciok\n";

        } else if (token == "isready") {
#ifdef NNUE_ENABLED
            if (!NNUE::g_loaded) {
                try {
                    NNUE::load("gungi_model.onnx");
                    std::cerr << "[NNUE] 模型載入成功\n";
                } catch (const std::exception& e) {
                    std::cerr << "[NNUE] 載入失敗，使用手工評估: " << e.what() << "\n";
                }
            }
#endif
            std::cout << "readyok\n";

        } else if (token == "ucinewgame") {
            engine().clear();
            pos.reset();

        } else if (token == "position") {
            ss >> token;
            pos.reset();
            engine().reset_game_keys();  // ★ 每次 position 指令必須清空歷史 key，避免累積誤判重複

            // FEN 裡大寫=白方、小寫=黑方；引擎內部小寫=白方、大寫=黑方，需翻轉
            // 翻轉函數：FEN字元 → 引擎字元
            auto fen_char_to_piece = [](char c) -> PieceID {
                // FEN大寫(白方) → 引擎小寫; FEN小寫(黑方) → 引擎大寫
                if (isupper(c)) return char_to_piece(tolower(c)); // 白方
                if (islower(c)) return char_to_piece(toupper(c)); // 黑方
                return NO_PIECE;
            };

            // 解析棋盤字串，支援疊棋 (ji) 格式
            auto parse_board = [&](const std::string& board_str) {
                // FEN 第一行 = rank 8（黑方底線），最後行 = rank 0（白方底線）
                // 標準 FEN：從最高 rank 往下解析
                int rank = 8;
                int file = 0;
                size_t i = 0;
                while (i < board_str.size()) {
                    char c = board_str[i];
                    if (c == '/') {
                        --rank;  // 每個 / 往下一行（rank 遞減）
                        file = 0;
                        ++i;
                    } else if (c >= '1' && c <= '9') {
                        file += (c - '0');
                        ++i;
                    } else if (c == '(') {
                        // 疊棋：(底層...頂層)
                        ++i;
                        int sq = make_sq(file, rank);
                        while (i < board_str.size() && board_str[i] != ')') {
                            PieceID p = fen_char_to_piece(board_str[i]);
                            if (p != NO_PIECE && sq < 81)
                                pos.board[sq].push(p);
                            ++i;
                        }
                        if (i < board_str.size()) ++i; // 跳過 ')'
                        ++file;
                    } else {
                        PieceID p = fen_char_to_piece(c);
                        int sq = make_sq(file, rank);
                        if (p != NO_PIECE && sq < 81)
                            pos.board[sq].push(p);
                        ++file;
                        ++i;
                    }
                }
            };

            // 解析手駒字串 [ALNOOPPUUWZalnooppuuvwz]
            auto parse_hand = [&](const std::string& hand_str) {
                for (char c : hand_str) {
                    if (c == '[' || c == ']') continue;
                    PieceID p = fen_char_to_piece(c);
                    if (p != NO_PIECE) {
                        Color c_color = color_of(p);
                        int   c_type  = type_of(p);
                        pos.hand[c_color][c_type]++;
                    }
                }
            };

            const std::string START_FEN =
                "3XYQ3/1NV1U1VZ1/P1JIPIJ1P/9/9/9/p1jipij1p/1zv1u1vn1/3qyx3";
            const std::string START_HAND =
                "ALNOOPPUUWZalnooppuuwz";

            if (token == "startpos") {
                parse_board(START_FEN);
                parse_hand(START_HAND);
                pos.side_to_move = WHITE;
                pos.key = pos.compute_key();      // ★ 先算初始 key，之後靠增量更新
                pos.rebuild_king_sq();
                engine().push_game_key(pos.key);  // 記錄起始局面
                // 讀取後續 moves
                std::string tmp;
                ss >> tmp; // 消耗 "moves" (若有)
                if (tmp == "moves") {
                    while (ss >> tmp) {
                        Move m = str_to_move(tmp);
                        if (m.is_valid()) {
                            pos.do_move_safe(m);
                            engine().push_game_key(pos.key);  // 記錄每步局面key供重複偵測
                        }
                    }
                }
            } else if (token == "fen") {
                // 讀取各欄位
                std::string board_str, stm_str, rest_token;
                ss >> board_str;  // 棋盤
                ss >> stm_str;    // "w" 或 "b"

                parse_board(board_str);

                pos.side_to_move = (stm_str == "b") ? BLACK : WHITE;

                // 讀取剩餘欄位，直到 moves 或手駒 [...]
                while (ss >> rest_token) {
                    if (rest_token == "moves") break;
                    if (rest_token[0] == '[') {
                        // 手駒欄位可能是單一 token "[ABC...]" 或跨多個
                        std::string hand_str = rest_token;
                        if (hand_str.back() != ']') {
                            std::string more;
                            while (ss >> more) {
                                hand_str += more;
                                if (more.find(']') != std::string::npos) break;
                            }
                        }
                        parse_hand(hand_str);
                    }
                    // 其他欄位（castling "-", ep "-", halfmove, fullmove）忽略
                }
                // ★ fen 路徑：先算初始 key（replay 前），再推入歷史
                pos.key = pos.compute_key();
                pos.rebuild_king_sq();
                engine().push_game_key(pos.key);  // 記錄起始局面 key
                // 讀取走法序列
                if (rest_token == "moves") {
                    while (ss >> rest_token) {
                        Move m = str_to_move(rest_token);
                        if (m.is_valid()) {
                            pos.do_move_safe(m);
                            engine().push_game_key(pos.key);  // 記錄每步局面key
                        }
                    }
                }
            }
            pos.halfmove_clock = 0;

        } else if (token == "go") {
            int depth = MAX_DEPTH;
            int64_t movetime = 0;
            int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
            bool infinite = false;
            int movestogo = 30;

            while (ss >> token) {
                if      (token == "depth")     ss >> depth;
                else if (token == "movetime")  ss >> movetime;
                else if (token == "wtime")     ss >> wtime;
                else if (token == "btime")     ss >> btime;
                else if (token == "winc")      ss >> winc;
                else if (token == "binc")      ss >> binc;
                else if (token == "movestogo") ss >> movestogo;
                else if (token == "infinite")  infinite = true;
            }

            int64_t time_ms = 0;
            if (infinite) {
                time_ms = 0; // 無限
            } else if (movetime > 0) {
                time_ms = movetime - 10;
            } else {
                int64_t my_time = (pos.side_to_move == WHITE) ? wtime : btime;
                int64_t my_inc  = (pos.side_to_move == WHITE) ? winc  : binc;
                time_ms = std::max((int64_t)10, my_time / movestogo + my_inc * 3 / 4);
            }

            Move best = engine().search(pos, depth, time_ms);
            std::cout << "bestmove " << move_to_str(best) << "\n";
            std::cout.flush();

        } else if (token == "stop") {
            engine().stopped.store(true);

        } else if (token == "eval") {
            int sc = evaluate(pos);
#ifdef NNUE_ENABLED
            std::string src = NNUE::g_loaded ? " [NNUE]" : " [hand]";
#else
            std::string src = " [hand]";
#endif
            std::cout << "score: " << sc << src << "\n";

        } else if (token == "d") {
            std::cout << pos.to_string();

        } else if (token == "quit") {
#ifdef NNUE_ENABLED
            NNUE::unload();
#endif
            break;
        }
    }
}

} // namespace Komugi

#endif // KOMUGI_ENGINE_H
