#include <random>

#include "komoring_heights.hpp"
#include "path_keys.hpp"
#include "thread_initialization.hpp"
#include "tsume_generator.hpp"
#include "tsume_validator.hpp"
#include "tsume_workbench.hpp"
#include "typedefs.hpp"

#if defined(USER_ENGINE)

namespace {
komori::KomoringHeights g_searcher;
komori::EngineOption g_option;
std::atomic_bool g_path_key_init_flag;

komori::NodeState g_search_result = komori::NodeState::kUnknown;

/// tsume_generate / tsume_batch_generate 実行中に checkmate/bestmove 出力を抑制するフラグ
bool g_tsume_gen_silent = false;

/// 局面が OR node っぽいかどうかを調べる。困ったら OR node として処理する。
bool IsPosOrNode(const Position& root_pos) {
  const Color us = root_pos.side_to_move();
  const Color them = ~us;

  if (root_pos.king_square(us) == SQ_NB) {
    return true;
  } else if (root_pos.king_square(them) == SQ_NB) {
    return false;
  }

  if (root_pos.in_check() && g_option.root_is_and_node_if_checked) {
    return false;
  }
  return true;
}

enum class LoseKind {
  kTimeout,
  kNoMate,
  kMate,
};

void PrintResult(bool is_mate_search, LoseKind kind, const std::string& pv_moves = "resign") {
  if (g_tsume_gen_silent) return;

  if (is_mate_search) {
    switch (kind) {
      case LoseKind::kTimeout:
        sync_cout << "checkmate timeout" << sync_endl;
        break;
      case LoseKind::kNoMate:
        sync_cout << "checkmate nomate" << sync_endl;
        break;
      default:
        sync_cout << "checkmate " << pv_moves << sync_endl;
    }
  }
  // `go` モードの場合は KomoringHeights::Search() 内で出力済み
}

// -----------------------------------------------------------------------
// 内部ヘルパー: 指定手数の詰将棋を count 問生成して found へ追加する
//
// 詰将棋ルール準拠:
//   - 攻め方は毎手王手のみ（OR ノードで王手のみ生成）
//   - 打ち歩詰め禁止（合法手生成で除外済み）
//   - 連続王手の千日手は攻め方の負け（repetition 検出）
//   - 受け方は最善手で応じる（AND ノードで全回避手評価）
// -----------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 邪魔駒確認: 盤上の各駒（玉以外）を除いても同じ手数で詰むなら除去する
// ---------------------------------------------------------------------------
static komori::TsumeGeneratedProblem RemoveUnnecessaryPieces(
    const komori::TsumeGeneratedProblem& problem, std::uint64_t time_limit_ms) {
  std::istringstream ss(problem.sfen);
  std::string board_str, turn, hand_str, move_num;
  ss >> board_str >> turn >> hand_str >> move_num;

  komori::detail::SfenBoard board;
  komori::detail::ParseSfenBoard(board_str, board);

  bool changed = true;
  std::vector<std::string> current_sol = problem.solution;

  while (changed) {
    changed = false;
    for (int r = 0; r < 9 && !changed; ++r) {
      for (int f = 0; f < 9 && !changed; ++f) {
        const std::string& piece = board[r][f];
        if (piece.empty() || piece == "k" || piece == "K") continue;

        auto test_board = board;
        test_board[r][f] = "";
        const std::string test_sfen =
            komori::detail::BuildSfenBoard(test_board) + " " + turn + " " + hand_str + " " + move_num;

        Position tp; StateListPtr st(new StateList(1));
        tp.set(test_sfen, &st->back(), Threads.main());
        if (!komori::ValidateTsumePosition(tp).empty() || tp.in_check()) continue;

        Search::LimitsType limits; limits.mate = static_cast<int>(time_limit_ms);
        Time.reset();
        Threads.start_thinking(tp, st, limits, false);
        Threads.main()->wait_for_search_finished();

        if (g_search_result == komori::NodeState::kProven) {
          const auto& bm = g_searcher.BestMoves();
          if (static_cast<int>(bm.size()) == problem.mate_in) {
            board[r][f] = "";
            board_str = komori::detail::BuildSfenBoard(board);
            current_sol.clear();
            for (const auto& m : bm) current_sol.push_back(USI::move(m));
            changed = true;
            sync_cout << "info string [邪魔駒] 除去: " << piece
                      << " (" << (9 - f) << static_cast<char>('a' + r) << ")" << sync_endl;
          }
        }
      }
    }
  }

  return {komori::detail::BuildSfenBoard(board) + " " + turn + " " + hand_str + " " + move_num,
          problem.mate_in, current_sol, ""};
}

// ---------------------------------------------------------------------------
// 手数伸ばし: 攻め方の盤上駒を別マスに動かして再探索、長くなれば採用
// ---------------------------------------------------------------------------
static komori::TsumeGeneratedProblem TryExtendMateLen(
    const komori::TsumeGeneratedProblem& problem, int max_target,
    std::mt19937& rng, std::uint64_t time_limit_ms) {
  std::istringstream ss(problem.sfen);
  std::string board_str, turn, hand_str, move_num;
  ss >> board_str >> turn >> hand_str >> move_num;

  komori::detail::SfenBoard board;
  komori::detail::ParseSfenBoard(board_str, board);

  // 攻め方の盤上駒（大文字・玉以外）を収集
  std::vector<std::pair<int,int>> atk;
  for (int r = 0; r < 9; ++r)
    for (int f = 0; f < 9; ++f) {
      const auto& p = board[r][f];
      if (!p.empty() && std::isupper(static_cast<unsigned char>(p.back())) && p != "K")
        atk.push_back({r, f});
    }
  if (atk.empty()) return problem;

  komori::TsumeGeneratedProblem best = problem;
  std::uniform_int_distribution<int> rd(0, 8), fd(0, 8);
  std::uniform_int_distribution<int> pd(0, static_cast<int>(atk.size()) - 1);

  for (int attempt = 0; attempt < 200; ++attempt) {
    auto [pr, pf] = atk[pd(rng)];
    const std::string piece = board[pr][pf];

    int nr = -1, nf = -1;
    for (int t = 0; t < 30; ++t) {
      int tr = rd(rng), tf = fd(rng);
      if (!board[tr][tf].empty()) continue;
      if (!komori::detail::IsValidBoardPlacement(piece.back(), tr)) continue;
      nr = tr; nf = tf; break;
    }
    if (nr < 0) continue;

    auto test_board = board;
    test_board[pr][pf] = "";
    test_board[nr][nf] = piece;
    const std::string test_sfen =
        komori::detail::BuildSfenBoard(test_board) + " " + turn + " " + hand_str + " " + move_num;

    Position tp; StateListPtr st(new StateList(1));
    tp.set(test_sfen, &st->back(), Threads.main());
    if (!komori::ValidateTsumePosition(tp).empty() || tp.in_check()) continue;

    Search::LimitsType limits; limits.mate = static_cast<int>(time_limit_ms * 2);
    Time.reset();
    Threads.start_thinking(tp, st, limits, false);
    Threads.main()->wait_for_search_finished();

    if (g_search_result == komori::NodeState::kProven) {
      const auto& bm = g_searcher.BestMoves();
      const int new_mate = static_cast<int>(bm.size());
      if (new_mate > best.mate_in && new_mate <= max_target) {
        std::vector<std::string> sol;
        for (const auto& m : bm) sol.push_back(USI::move(m));
        best = {test_sfen, new_mate, sol, ""};
        sync_cout << "info string [手数伸ばし] " << problem.mate_in << "→" << new_mate
                  << "手: " << test_sfen << sync_endl;
      }
    }
  }
  return best;
}

static std::string VerificationSignature(const komori::tsume::VerifyResult& r) {
  std::vector<std::string> branches, lines;
  for (const auto& b : r.attacks)
    branches.push_back(USI::move(b.move) + ":" + komori::tsume::detail::ProofName(b.proof) +
                       ":" + std::to_string(b.mate_ply));
  for (const auto& l : r.longest_lines) lines.push_back(komori::tsume::detail::UsiLine(l.moves));
  std::sort(branches.begin(), branches.end()); std::sort(lines.begin(), lines.end());
  std::ostringstream out;
  out << komori::tsume::detail::ProofName(r.proof) << ':' << r.mate_ply << ':' << r.unique << ':';
  for (const auto& x : branches) out << x << ';'; out << '|';
  for (const auto& x : lines) out << x << ';';
  return out.str();
}

static std::string IsomorphismSignature(const komori::tsume::VerifyResult& r) {
  std::vector<std::string> branches;
  for (const auto& b : r.attacks)
    branches.push_back(std::string(komori::tsume::detail::ProofName(b.proof)) + ":" +
                       std::to_string(b.mate_ply) + ":" + std::to_string(b.principal.size()));
  std::sort(branches.begin(), branches.end());
  std::vector<std::size_t> variation_lengths;
  for (const auto& l : r.longest_lines) variation_lengths.push_back(l.moves.size());
  std::sort(variation_lengths.begin(), variation_lengths.end());
  std::ostringstream out; out << komori::tsume::detail::ProofName(r.proof) << ':' << r.mate_ply
                              << ':' << r.unique << ':' << r.complete << '|';
  for (const auto& x : branches) out << x << ';'; out << '|';
  for (auto x : variation_lengths) out << x << ';';
  return out.str();
}

static bool VerifyIsomorphic(const std::string& sfen, const komori::tsume::VerifyOptions& options,
                             const std::string& wanted, komori::tsume::VerifyResult* result = nullptr) {
  Position p; StateListPtr states(new StateList(1)); p.set(sfen, &states->back(), Threads.main());
  if (!komori::ValidateTsumePosition(p).empty()) return false;
  komori::tsume::ExhaustiveVerifier verifier(options); auto checked = verifier.Run(p);
  if (!checked.complete || IsomorphismSignature(checked) != wanted) return false;
  if (result) *result = std::move(checked);
  return true;
}

// Normalize only through re-proven isomorphisms: maximize upward movement,
// then rightward movement, then consider a mirror under the documented tie-break.
static std::string CanonicalizeVerified(const std::string& sfen,
                                        const komori::tsume::VerifyOptions& options,
                                        const komori::tsume::VerifyResult& original) {
  std::istringstream in(sfen); std::string board_text, turn, hands, move_no;
  in >> board_text >> turn >> hands >> move_no;
  const std::string wanted = IsomorphismSignature(original);
  auto normalize = [&](komori::detail::SfenBoard board) {
    for (;;) {
      bool can = true; for (int f = 0; f < 9; ++f) if (!board[0][f].empty()) can = false;
      if (!can) break;
      auto next = komori::detail::TransformBoard(board, false, 0, 1);
      const std::string candidate = komori::detail::BuildSfenBoard(next) + " " + turn + " " + hands + " " + move_no;
      if (!VerifyIsomorphic(candidate, options, wanted)) break; board = std::move(next);
    }
    for (;;) {
      bool can = true; for (int r = 0; r < 9; ++r) if (!board[r][8].empty()) can = false;
      if (!can) break;
      auto next = komori::detail::TransformBoard(board, false, -1, 0);
      const std::string candidate = komori::detail::BuildSfenBoard(next) + " " + turn + " " + hands + " " + move_no;
      if (!VerifyIsomorphic(candidate, options, wanted)) break; board = std::move(next);
    }
    return board;
  };
  komori::detail::SfenBoard source; komori::detail::ParseSfenBoard(board_text, source);
  auto normal = normalize(source);
  auto mirrored = normalize(komori::detail::TransformBoard(source, true, 0, 0));
  const std::string ns = komori::detail::BuildSfenBoard(normal) + " " + turn + " " + hands + " " + move_no;
  const std::string ms = komori::detail::BuildSfenBoard(mirrored) + " " + turn + " " + hands + " " + move_no;
  const bool mirror_ok = VerifyIsomorphic(ms, options, wanted);
  if (!mirror_ok) return ns;
  auto priority = [](const komori::detail::SfenBoard& b) {
    int king = -1, major = 0;
    for (int r = 0; r < 9; ++r) for (int f = 0; f < 9; ++f) {
      if (b[r][f] == "k") king = f;
      if (b[r][f] == "R" || b[r][f] == "+R" || b[r][f] == "B" || b[r][f] == "+B") major += f;
    }
    return std::pair<int,int>{king, major};
  };
  const auto np = priority(normal), mp = priority(mirrored);
  if (mp > np) return ms; if (np > mp) return ns;
  return std::min(ns, ms);
}

// A piece is unnecessary only if removing it (and consequently restoring it
// to the defender reserve) preserves the complete bounded solution tree.
static void AnalyzeUnnecessaryPieces(const std::string& completed_sfen,
                                     const komori::tsume::VerifyOptions& options,
                                     komori::tsume::VerifyResult& original) {
  if (!original.complete) return;
  std::istringstream in(completed_sfen);
  std::string board_text, turn, hands, move_no; in >> board_text >> turn >> hands >> move_no;
  komori::detail::SfenBoard board; komori::detail::ParseSfenBoard(board_text, board);
  const std::string signature = VerificationSignature(original);
  for (int r = 0; r < 9; ++r) for (int f = 0; f < 9; ++f) {
    if (board[r][f].empty() || board[r][f] == "K" || board[r][f] == "k") continue;
    auto reduced = board; reduced[r][f].clear();
    std::string test_sfen = komori::detail::BuildSfenBoard(reduced) + " " + turn + " " + hands + " " + move_no;
    test_sfen = komori::detail::CompleteDefenderReserve(test_sfen, options.double_king);
    Position test; StateListPtr states(new StateList(1)); test.set(test_sfen, &states->back(), Threads.main());
    if (!komori::ValidateTsumePosition(test).empty()) continue;
    komori::tsume::ExhaustiveVerifier verifier(options); auto result = verifier.Run(test);
    original.nodes += result.nodes;
    if (result.complete && VerificationSignature(result) == signature) {
      original.unnecessary_piece = true;
      original.unnecessary_squares.push_back(std::to_string(9 - f) + char('a' + r));
    }
  }
}

void GenerateProblemsForMoves(int target_moves, int count,
                               std::mt19937& rng,
                               std::vector<komori::TsumeGeneratedProblem>& found) {
  // 生成ループ用: 小さい TT + silent で Init（大 TT だと初回アクセス時のページフォルトで詰まる）
  komori::EngineOption gen_option = g_option;
  gen_option.hash_mb              = 64;
  gen_option.silent               = true;
  gen_option.pv_interval          = std::numeric_limits<std::uint64_t>::max() / 2;
  g_searcher.Init(gen_option, static_cast<std::uint32_t>(Threads.size()));

  const int target_count  = static_cast<int>(found.size()) + count;
  const int max_attempts  = count * 10000;

  // 手数に応じた探索時間上限（ms）
  const auto time_limit_ms = static_cast<std::uint64_t>(komori::detail::ComputeTimeLimitMs(target_moves));

  for (int attempt = 0; attempt < max_attempts && static_cast<int>(found.size()) < target_count;
       ++attempt) {
    // ランダム候補局面を生成（手数に応じた駒数を使用）
    const std::string sfen = komori::detail::GenerateCandidateSfen(rng, target_moves);

    Position gen_pos;
    StateListPtr st(new StateList(1));
    gen_pos.set(sfen, &st->back(), Threads.main());

    // 詰将棋ルール検証: 受け方の玉が盤上にある & 出題局面で王手でない
    if (!komori::ValidateTsumePosition(gen_pos).empty()) continue;
    if (gen_pos.in_check()) continue;

    // 詰み探索: limits.mate を時間上限(ms)として使用（KomoringHeights の慣習）
    Search::LimitsType limits;
    limits.mate = static_cast<int>(time_limit_ms);

    Time.reset();
    Threads.start_thinking(gen_pos, st, limits, false);
    Threads.main()->wait_for_search_finished();

    if (g_search_result == komori::NodeState::kProven) {
      const auto& best_moves = g_searcher.BestMoves();
      const int mate_in      = static_cast<int>(best_moves.size());

      if (mate_in == target_moves) {
        std::vector<std::string> sol;
        for (const auto& m : best_moves) sol.push_back(USI::move(m));
        komori::TsumeGeneratedProblem problem{sfen, mate_in, sol, ""};

        // 目標手数の生候補を失わないよう、延長より先に完全検証する。
        // 従来は未検証の延長候補で元の9手候補を上書きしていた。
        sync_cout << "info string [raw candidate] " << mate_in << " ply: " << sfen << sync_endl;
        if (komori::tsume::HasSurplusAttackerHand(gen_pos, best_moves)) {
          sync_cout << "info string [candidate rejected] df-pn principal leaves surplus attacker hand"
                    << sync_endl;
          continue;
        }

        // 邪魔駒確認: 不要な駒を除去して局面を簡潔に
        problem = RemoveUnnecessaryPieces(problem, time_limit_ms);

        // df-pnで見つかった候補を作品検査用の全幅AND/OR探索で再検証する。
        komori::tsume::VerifyOptions verify_options;
        verify_options.max_ply = problem.mate_in;
        verify_options.max_nodes = std::max<std::uint64_t>(20'000'000,
            komori::detail::ComputeNodesLimit(problem.mate_in));
        verify_options.double_king = false;
        Position candidate; StateListPtr candidate_states(new StateList(1));
        candidate.set(problem.sfen, &candidate_states->back(), Threads.main());
        komori::tsume::ExhaustiveVerifier complete_verifier(verify_options);
        auto complete_result = complete_verifier.Run(candidate);
        const bool surplus = komori::tsume::HasSurplusAttackerHand(candidate, complete_result.principal);
        if (!complete_result.complete || complete_result.proof != komori::tsume::Proof::kMate ||
            !complete_result.unique || complete_result.mate_ply != problem.mate_in || surplus) {
          sync_cout << "info string [candidate rejected] nodes=" << complete_result.nodes
                    << " complete=" << complete_result.complete
                    << " mate=" << (complete_result.proof == komori::tsume::Proof::kMate)
                    << " unique=" << complete_result.unique
                    << " matePly=" << complete_result.mate_ply
                    << " surplus=" << surplus << sync_endl;
          continue;
        }

        AnalyzeUnnecessaryPieces(problem.sfen, verify_options, complete_result);
        if (complete_result.unnecessary_piece) {
          sync_cout << "info string [candidate rejected] unnecessary piece" << sync_endl;
          continue;
        }
        problem.sfen = CanonicalizeVerified(problem.sfen, verify_options, complete_result);

        Position normalized; StateListPtr normalized_states(new StateList(1));
        normalized.set(problem.sfen, &normalized_states->back(), Threads.main());
        komori::tsume::ExhaustiveVerifier normalized_verifier(verify_options);
        complete_result = normalized_verifier.Run(normalized);
        if (!complete_result.complete || !complete_result.unique ||
            complete_result.mate_ply != problem.mate_in) continue;
        problem.solution.clear();
        for (Move m : complete_result.principal) problem.solution.push_back(USI::move(m));
        auto record = komori::tsume::MakeRecord(normalized, problem.sfen, verify_options,
                                                std::move(complete_result));
        if (!record.aesthetic_pass) {
          sync_cout << "info string [candidate rejected] aesthetic gate:";
          for (const auto& reason : record.aesthetic_reasons) sync_cout << ' ' << reason << ';';
          sync_cout << sync_endl;
          continue;
        }
        problem.record_json = komori::tsume::ToJson(record, normalized);

        // 右上チェック（正規形後）
        komori::detail::SfenBoard cb;
        komori::detail::ParseSfenBoard(problem.sfen.substr(0, problem.sfen.find(' ')), cb);
        bool all_right = true, all_upper = true;
        for (int r = 0; r < 9; ++r)
          for (int f = 0; f < 9; ++f)
            if (!cb[r][f].empty()) {
              if (f > 2) all_right = false;
              if (r > 2) all_upper = false;
            }

        found.push_back(problem);
        sync_cout << "info string [" << problem.mate_in << "手詰め] 発見 "
                  << (static_cast<int>(found.size()) - target_count + count)
                  << "/" << count
                  << (all_right ? " [右寄り]" : "") << (all_upper ? " [上寄り]" : "")
                  << ": " << problem.sfen << sync_endl;
      }
    }
  }

  if (static_cast<int>(found.size()) < target_count) {
    sync_cout << "info string [" << target_moves
              << "手詰め] 試行回数上限（" << max_attempts << "回）に達しました" << sync_endl;
  }

  // TT とオプションを元に戻す
  g_searcher.Init(g_option, static_cast<std::uint32_t>(Threads.size()));
}
}  // namespace

// USI拡張コマンド "user" のディスパッチャ
//
// サブコマンド:
//   user tsume_solve
//       現在の局面を詰将棋ルール検証付きで解答する
//       出力: checkmate <手順> / checkmate nomate / checkmate timeout
//
//   user tsume_generate <target_moves> [count] [output_file]
//       指定手数の詰将棋を count 問生成してファイルに保存する
//       target_moves : 目標手数 (default: 3)
//       count        : 生成問題数 (default: 1)
//       output_file  : 出力ファイル (default: tsume_output.sfen)
//
//   user tsume_batch_generate <start_moves> <end_moves> <step> [count_each] [output_file]
//       start_moves から end_moves まで step 刻みで各手数の詰将棋を生成する
//       count_each   : 各手数の生成問題数 (default: 1)
//       output_file  : 出力ファイル (default: tsume_batch.sfen)
//       例: user tsume_batch_generate 5 31 2 2 batch.sfen
//           → 5,7,9,11,...,31手詰めを各2問生成
void user_test(Position& pos, std::istringstream& is) {
  std::string token;
  is >> token;

  auto verify_and_output = [&](const std::string& input_sfen, komori::tsume::VerifyOptions options,
                               const std::string& save_file) {
    Position verify_pos; StateListPtr verify_states(new StateList(1));
    verify_pos.set(input_sfen, &verify_states->back(), Threads.main());
    const std::string canonical_input_sfen = verify_pos.sfen();
    const auto err = komori::ValidateTsumePosition(verify_pos);
    if (!err.empty()) { sync_cout << "info string [tsume] position error: " << err << sync_endl; return; }
    komori::tsume::ExhaustiveVerifier verifier(options);
    auto result = verifier.Run(verify_pos);
    const std::string normalized = result.complete
        ? CanonicalizeVerified(canonical_input_sfen, options, result) : canonical_input_sfen;
    AnalyzeUnnecessaryPieces(canonical_input_sfen, options, result);
    auto record = komori::tsume::MakeRecord(verify_pos, normalized, options, std::move(result));
    sync_cout << "info string tsume_json " << komori::tsume::ToJson(record, verify_pos) << sync_endl;
    if (!save_file.empty())
      sync_cout << "info string [tsume_save] "
                << (komori::tsume::SaveJson(record, verify_pos, save_file) ? "saved " : "save failed ")
                << save_file << sync_endl;
    if (record.verification.proof == komori::tsume::Proof::kMate)
      sync_cout << "checkmate " << komori::tsume::detail::UsiLine(record.verification.principal) << sync_endl;
    else if (record.verification.complete) sync_cout << "checkmate nomate" << sync_endl;
    else sync_cout << "checkmate timeout" << sync_endl;
  };

  // user tsume_load <jsonl> [max_ply] [max_nodes]
  if (token == "tsume_load") {
    std::string filename; is >> filename;
    std::string loaded_sfen;
    if (filename.empty() || !komori::tsume::LoadSfenFromJson(filename, loaded_sfen)) {
      sync_cout << "info string [tsume_load] load failed " << filename << sync_endl; return;
    }
    komori::tsume::VerifyOptions options; std::string arg;
    if (is >> arg) options.max_ply = std::max(1, std::stoi(arg));
    if (is >> arg) options.max_nodes = std::max<std::uint64_t>(1, std::stoull(arg));
    Position probe; StateListPtr states(new StateList(1)); probe.set(loaded_sfen, &states->back(), Threads.main());
    options.double_king = probe.king_square(probe.side_to_move()) != SQ_NB;
    verify_and_output(komori::detail::CompleteDefenderReserve(loaded_sfen, options.double_king), options, "");
    return;
  }

  // Local extension search from the current composition.
  // user tsume_extend <count> <max_ply> <max_nodes> <jsonl>
  if (token == "tsume_extend") {
    int wanted_count = 3; komori::tsume::VerifyOptions options; std::string arg, filename = "tsume_extensions.jsonl";
    if (is >> arg) wanted_count = std::max(1, std::stoi(arg));
    if (is >> arg) options.max_ply = std::max(7, std::stoi(arg));
    if (is >> arg) options.max_nodes = std::max<std::uint64_t>(1, std::stoull(arg));
    if (is >> arg) filename = arg;
    options.double_king = pos.king_square(pos.side_to_move()) != SQ_NB;
    const std::string base_sparse = pos.sfen();
    const std::string base_sfen = komori::detail::CompleteDefenderReserve(base_sparse, options.double_king);
    Position base; StateListPtr base_states(new StateList(1)); base.set(base_sfen, &base_states->back(), Threads.main());
    komori::tsume::ExhaustiveVerifier base_verifier(options); auto base_result = base_verifier.Run(base);
    if (!base_result.complete || base_result.proof != komori::tsume::Proof::kMate) {
      sync_cout << "info string [tsume_extend] base position is not a verified mate" << sync_endl; return;
    }
    const std::string parent_id = komori::tsume::detail::StableId(base.sfen() + komori::tsume::detail::UsiLine(base_result.principal));
    std::istringstream parts(base_sparse); std::string board_text, turn, hands, move_no;
    parts >> board_text >> turn >> hands >> move_no;
    komori::detail::SfenBoard source; komori::detail::ParseSfenBoard(board_text, source);
    std::mt19937 rng(std::random_device{}()); std::uniform_int_distribution<int> sq(0, 8), type(0, 6);
    const std::array<char, 7> piece_chars{{'P','L','N','S','G','B','R'}};
    std::unordered_set<std::string> ids; int saved = 0;
    for (int attempt = 0; attempt < 1000 && saved < wanted_count; ++attempt) {
      auto board = source; const int operation = attempt % 6; std::string diff;
      std::vector<std::pair<int,int>> movable, empty;
      for (int r = 0; r < 9; ++r) for (int f = 0; f < 9; ++f) {
        if (board[r][f].empty()) empty.push_back({r,f});
        else if (board[r][f] != "k" && board[r][f] != "K") movable.push_back({r,f});
      }
      if (operation == 0 && !movable.empty() && !empty.empty()) {
        auto from = movable[rng() % movable.size()], to = empty[rng() % empty.size()];
        board[to.first][to.second] = board[from.first][from.second]; board[from.first][from.second].clear(); diff = "piece move";
      } else if (operation == 1 && !movable.empty()) {
        auto at = movable[rng() % movable.size()]; board[at.first][at.second].clear(); diff = "piece removal";
      } else if (operation == 2 && !movable.empty()) {
        auto at = movable[rng() % movable.size()]; const bool black = std::isupper(static_cast<unsigned char>(board[at.first][at.second].back()));
        char c = piece_chars[type(rng)]; board[at.first][at.second] = std::string(1, black ? c : static_cast<char>(std::tolower(c))); diff = "piece type change";
      } else if (operation == 3 && !movable.empty()) {
        auto at = movable[rng() % movable.size()]; auto& p = board[at.first][at.second];
        const char raw = static_cast<char>(std::toupper(static_cast<unsigned char>(p.back())));
        if (raw != 'G') { p = p.size() == 2 ? p.substr(1) : "+" + p; diff = "promotion toggle"; }
      } else if (operation == 4 && !empty.empty()) {
        bool done = false; for (int r = 0; r < 9 && !done; ++r) for (int f = 0; f < 9 && !done; ++f)
          if (board[r][f] == "k") { auto to = empty[rng() % empty.size()]; board[to.first][to.second] = "k"; board[r][f].clear(); done = true; diff = "defender king move"; }
      } else if (operation == 5 && !empty.empty()) {
        auto to = empty[rng() % empty.size()]; board[to.first][to.second] = std::string(1, piece_chars[type(rng)]); diff = "attacker piece addition";
      }
      if (diff.empty()) continue;
      std::string candidate_sparse = komori::detail::BuildSfenBoard(board) + " " + turn + " " + hands + " " + move_no;
      std::string candidate_sfen = komori::detail::CompleteDefenderReserve(candidate_sparse, options.double_king);
      Position candidate; StateListPtr states(new StateList(1)); candidate.set(candidate_sfen, &states->back(), Threads.main());
      if (!komori::ValidateTsumePosition(candidate).empty()) continue;
      komori::tsume::ExhaustiveVerifier verifier(options); auto result = verifier.Run(candidate);
      if (!result.complete || result.proof != komori::tsume::Proof::kMate || !result.unique) continue;
      AnalyzeUnnecessaryPieces(candidate.sfen(), options, result);
      auto normalized = CanonicalizeVerified(candidate.sfen(), options, result);
      auto record = komori::tsume::MakeRecord(candidate, normalized, options, std::move(result), parent_id);
      if (!record.aesthetic_pass || !ids.insert(record.canonical_id).second) continue;
      int same = 0, total = 0; for (int r = 0; r < 9; ++r) for (int f = 0; f < 9; ++f)
        if (!source[r][f].empty() || !board[r][f].empty()) { ++total; if (source[r][f] == board[r][f]) ++same; }
      record.parent_similarity = total ? double(same) / total : 1.0; record.parent_mate_ply = base_result.mate_ply;
      record.parent_diff = diff; record.extension_reason = record.verification.mate_ply > base_result.mate_ply
          ? "added a forced checking/escape phase" : "preserved the mating structure under local mutation";
      if (!komori::tsume::SaveJson(record, candidate, filename)) break;
      ++saved; sync_cout << "info string [tsume_extend] " << saved << '/' << wanted_count << ' '
                        << base_result.mate_ply << "->" << record.verification.mate_ply << " " << diff << sync_endl;
    }
    sync_cout << "info string [tsume_extend] saved " << saved << " candidates to " << filename << sync_endl;
    return;
  }

  // Bounded exhaustive composition verification.  Unlike tsume_solve this
  // enumerates every root attack and every legal defence and emits one JSON
  // object suitable for saving/reloading by tools.
  //   user tsume_verify [max_ply] [max_nodes] [known USI moves...]
  if (token == "tsume_verify") {
    const bool double_king = pos.king_square(pos.side_to_move()) != SQ_NB;
    const std::string completed_sfen =
        komori::detail::CompleteDefenderReserve(pos.sfen(), double_king);
    komori::tsume::VerifyOptions options;
    std::string arg;
    if (is >> arg) options.max_ply = std::max(1, std::stoi(arg));
    if (is >> arg) options.max_nodes = std::max<std::uint64_t>(1, std::stoull(arg));
    while (is >> arg) options.intended_usi.push_back(arg);
    options.double_king = double_king;
    verify_and_output(completed_sfen, options, "");
    return;
  }

  // user tsume_save <max_ply> <max_nodes> <jsonl> [known USI moves...]
  if (token == "tsume_save") {
    komori::tsume::VerifyOptions options; std::string arg, filename;
    if (is >> arg) options.max_ply = std::max(1, std::stoi(arg));
    if (is >> arg) options.max_nodes = std::max<std::uint64_t>(1, std::stoull(arg));
    is >> filename; while (is >> arg) options.intended_usi.push_back(arg);
    options.double_king = pos.king_square(pos.side_to_move()) != SQ_NB;
    verify_and_output(komori::detail::CompleteDefenderReserve(pos.sfen(), options.double_king), options, filename);
    return;
  }

  // -----------------------------------------------------------------------
  // モード1: 詰将棋解答モード
  // -----------------------------------------------------------------------
  if (token == "tsume_solve") {
    const auto err = komori::ValidateTsumePosition(pos);
    if (!err.empty()) {
      sync_cout << "info string [tsume_solve] 局面エラー: " << err << sync_endl;
      return;
    }

    sync_cout << "info string [tsume_solve] ルール検証: OK" << sync_endl;
    sync_cout << "info string [tsume_solve] 探索開始..." << sync_endl;

    const std::string sfen = pos.sfen();
    Position solve_pos;
    StateListPtr st(new StateList(1));
    solve_pos.set(sfen, &st->back(), Threads.main());

    Search::LimitsType limits;
    limits.mate = INT32_MAX;

    Time.reset();
    Threads.start_thinking(solve_pos, st, limits, false);
    Threads.main()->wait_for_search_finished();

    if (g_search_result == komori::NodeState::kProven) {
      sync_cout << "info string [tsume_solve] "
                << g_searcher.BestMoves().size() << "手詰め" << sync_endl;
    } else if (g_search_result == komori::NodeState::kDisproven) {
      sync_cout << "info string [tsume_solve] 不詰" << sync_endl;
    } else if (g_search_result == komori::NodeState::kRepetition) {
      sync_cout << "info string [tsume_solve] 連続王手の千日手（攻め方の負け）" << sync_endl;
    } else {
      sync_cout << "info string [tsume_solve] 探索打ち切り" << sync_endl;
    }
    return;
  }

  // -----------------------------------------------------------------------
  // モード2: 詰将棋単手数生成モード
  // user tsume_generate <target_moves> [count] [output_file]
  // -----------------------------------------------------------------------
  if (token == "tsume_generate") {
    int target_moves        = 9;
    int count               = 1;
    std::string output_file = "tsume_output.sfen";
    {
      std::string t;
      if (is >> t) target_moves = std::stoi(t);
      if (is >> t) count        = std::stoi(t);
      if (is >> t) output_file  = t;
    }
    if (target_moves < 7 || target_moves % 2 == 0) {
      sync_cout << "info string [tsume_generate] エラー: 正式生成は7手以上の奇数のみです" << sync_endl;
      return;
    }

    sync_cout << "info string [tsume_generate] 開始: "
              << target_moves << "手詰め × " << count << "問"
              << " → " << output_file << sync_endl;
    sync_cout << "info string [tsume_generate] 時間上限: "
              << komori::detail::ComputeTimeLimitMs(target_moves) << "ms/局面" << sync_endl;

    std::mt19937 rng(std::random_device{}());
    std::vector<komori::TsumeGeneratedProblem> found;

    g_tsume_gen_silent = true;
    GenerateProblemsForMoves(target_moves, count, rng, found);
    g_tsume_gen_silent = false;

    if (found.empty()) {
      sync_cout << "info string [tsume_generate] 問題を生成できませんでした" << sync_endl;
    } else {
      komori::SaveTsumeProblems(found, output_file);
    }
    return;
  }

  // -----------------------------------------------------------------------
  // モード3: 詰将棋バッチ生成モード
  // user tsume_batch_generate <start_moves> <end_moves> <step> [count_each] [output_file]
  // -----------------------------------------------------------------------
  if (token == "tsume_batch_generate") {
    int start_moves         = 7;
    int end_moves           = 11;
    int step                = 2;
    int count_each          = 1;
    std::string output_file = "tsume_batch.sfen";
    {
      std::string t;
      if (is >> t) start_moves = std::stoi(t);
      if (is >> t) end_moves   = std::stoi(t);
      if (is >> t) step        = std::stoi(t);
      if (is >> t) count_each  = std::stoi(t);
      if (is >> t) output_file = t;
    }

    // 生成する手数リストを構築
    std::vector<int> move_counts;
    for (int m = start_moves; m <= end_moves; m += step) {
      if (m >= 7 && m % 2 == 1) move_counts.push_back(m);
    }

    if (move_counts.empty()) {
      sync_cout << "info string [tsume_batch_generate] エラー: 有効な手数がありません" << sync_endl;
      return;
    }

    sync_cout << "info string [tsume_batch_generate] 開始: "
              << start_moves << "〜" << end_moves << "手(step=" << step << ") × 各"
              << count_each << "問 → " << output_file << sync_endl;

    std::mt19937 rng(std::random_device{}());
    std::vector<komori::TsumeGeneratedProblem> all_found;

    g_tsume_gen_silent = true;

    for (int moves : move_counts) {
      sync_cout << "info string [tsume_batch_generate] ===== " << moves
                << "手詰め生成開始 (時間上限: "
                << komori::detail::ComputeTimeLimitMs(moves) << "ms/局面) =====" << sync_endl;

      const std::size_t before = all_found.size();
      GenerateProblemsForMoves(moves, count_each, rng, all_found);
      const int got = static_cast<int>(all_found.size() - before);

      sync_cout << "info string [tsume_batch_generate] " << moves << "手詰め: "
                << got << "/" << count_each << "問 完了" << sync_endl;
    }

    g_tsume_gen_silent = false;

    sync_cout << "info string [tsume_batch_generate] 合計 " << all_found.size()
              << " 問生成完了" << sync_endl;

    if (!all_found.empty()) {
      komori::SaveTsumeProblems(all_found, output_file);
    }
    return;
  }

  // 不明なサブコマンド
  if (!token.empty()) {
    sync_cout << "info string [user] 不明なサブコマンド: " << token << sync_endl;
    sync_cout << "info string [user] 使い方:" << sync_endl;
    sync_cout << "info string [user]   user tsume_verify [上限手数] [上限ノード数] [既知USI手順...]" << sync_endl;
    sync_cout << "info string [user]   user tsume_save <上限手数> <上限ノード数> <JSONLファイル> [既知USI手順...]" << sync_endl;
    sync_cout << "info string [user]   user tsume_load <JSONLファイル> [上限手数] [上限ノード数]" << sync_endl;
    sync_cout << "info string [user]   user tsume_extend <件数> <上限手数> <上限ノード数> <JSONLファイル>" << sync_endl;
    sync_cout << "info string [user]   user tsume_solve" << sync_endl;
    sync_cout << "info string [user]   user tsume_generate <手数> [問題数] [ファイル]" << sync_endl;
    sync_cout << "info string [user]   user tsume_batch_generate <開始手数> <終了手数> <ステップ> [各問題数] [ファイル]" << sync_endl;
  }
}

// USIに追加オプションを設定したいときは、この関数を定義すること。
void USI::extra_option(USI::OptionsMap& o) {
  komori::EngineOption::Init(o);
}

void Search::init() {}

void Search::clear() {
  Threads.main()->wait_for_search_finished();
  Threads.clear();

  if (!g_path_key_init_flag) {
    g_path_key_init_flag = true;
    komori::PathKeyInit();
  }
  g_option.Reload(Options);

#if defined(USE_DEEP_DFPN)
  auto d = g_option.deep_dfpn_d_;
  auto e = g_option.deep_dfpn_e_;
  komori::DeepDfpnInit(d, e);
#endif

  g_searcher.Init(g_option, Threads.size());
}

void MainThread::search() {
  const bool is_mate_search  = Search::Limits.mate != 0;
  const bool is_root_or_node = IsPosOrNode(rootPos);

  g_searcher.NewSearch(rootPos, is_root_or_node);
  Threads.start_searching();
  Thread::search();
  Threads.stop = true;
  Threads.wait_for_search_finished();

  Move best_move = MOVE_NONE;
  if (g_search_result == komori::NodeState::kProven) {
    auto best_moves = g_searcher.BestMoves();
    PrintResult(is_mate_search, LoseKind::kMate, komori::ToString(best_moves));
    if (!best_moves.empty()) best_move = best_moves[0];
  } else {
    if (g_search_result == komori::NodeState::kDisproven ||
        g_search_result == komori::NodeState::kRepetition) {
      PrintResult(is_mate_search, LoseKind::kNoMate);
    } else {
      PrintResult(is_mate_search, LoseKind::kTimeout);
    }
  }

  if (Search::Limits.mate == 0) {
    while (!Threads.stop && Search::Limits.infinite) Tools::sleep(1);
    sync_cout << "bestmove " << (best_move == MOVE_NONE ? "resign" : USI::move(best_move))
              << sync_endl;
    return;
  }
}

void Thread::search() {
  komori::InitializeThread(id(), Threads.size());
  const auto result = g_searcher.Search(rootPos, IsPosOrNode(rootPos));
  if (id() == 0) g_search_result = result;
}

#endif  // USER_ENGINE
