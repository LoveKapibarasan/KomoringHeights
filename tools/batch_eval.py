"""
Batch evaluate all_problems.tsv against the KomoringHeights engine.
Outputs a TSV dataset with all X-variable values + difficulty label for regression.
"""
import sys, os, subprocess, json, time, re
sys.stdout.reconfigure(encoding='utf-8')

ENGINE = r"C:\Users\lovek\Documents\KomoringHeights\source\KomoringHeights-by-gcc.exe"
TSV_IN = r"G:\My Drive\Shogi\T_Shogi\all_problems.tsv"
OUT    = r"G:\My Drive\Shogi\T_Shogi\eval_dataset.tsv"

MAX_PLY   = 31
MAX_NODES = 2_000_000  # board-position metrics only; no full verification needed

def run_verify(sfen, usi_moves="", max_ply=MAX_PLY, max_nodes=MAX_NODES, timeout=15):
    """Run tsume_verify on a SFEN; return JSON (even if unverified) for X-variable extraction."""
    cmd = f"user tsume_verify {max_ply} {max_nodes}"
    if usi_moves:
        cmd += " " + usi_moves
    cmd_lines = [
        "usi",
        "isready",
        f"position sfen {sfen}",
        cmd,
        "quit",
    ]
    inp = "\n".join(cmd_lines) + "\n"
    try:
        result = subprocess.run(
            [ENGINE],
            input=inp, capture_output=True, text=True, timeout=timeout,
            encoding="utf-8", errors="replace"
        )
        for line in result.stdout.splitlines():
            if line.startswith("info string tsume_json "):
                return json.loads(line[len("info string tsume_json "):])
    except subprocess.TimeoutExpired:
        return None
    except Exception:
        return None
    return None

header_written = False

with open(TSV_IN, encoding="utf-8") as f, open(OUT, "w", encoding="utf-8") as out:
    lines = f.readlines()[1:]  # skip header
    total = len(lines)

    for idx, row in enumerate(lines, 1):
        parts = row.rstrip("\n").split("\t")
        if len(parts) < 4:
            continue
        if len(parts) < 5:
            continue
        src, diff_str, moves_str, sfen, usi = parts[0], parts[1], parts[2], parts[3], parts[4]
        try:
            diff  = int(diff_str)
            moves = int(moves_str)
        except ValueError:
            continue
        if moves <= 0 or moves % 2 == 0 or moves > MAX_PLY:
            continue  # skip 0-move, even-ply, overly long

        rec = run_verify(sfen, usi_moves=usi, max_ply=moves + 2, max_nodes=MAX_NODES)
        if rec is None:
            print(f"[{idx}/{total}] TIMEOUT/ERROR  {src}", flush=True)
            continue

        scores = rec.get("scores", {})
        metrics = rec.get("aestheticMetrics", {})
        mate_ply = rec.get("matePly", -1)
        nodes    = rec.get("nodes", 0)
        status   = rec.get("status", "")

        # Skip only if diff==0 (ZIP problems have no difficulty label)
        if diff == 0:
            continue

        # X variables (X0 from TSV as authoritative; X5-X8,X10 from board position)
        x0  = moves                                        # X0: 手数 (from KIF)
        x1  = metrics.get("sacrifices", 0)                 # X1: 捨て駒
        x2  = metrics.get("nonPromotions", 0)              # X2: 不成
        x3  = metrics.get("capturesByAttacker", 0)         # X3: 駒取り
        x4  = metrics.get("kingOpenness", 0)               # X4: 詰み局面の王の開放度
        x5  = scores.get("x5Spread", 0.0)                  # X5: 配置広さ
        x6  = scores.get("x6Count", 0)                     # X6: 盤上駒数
        x7  = scores.get("x7Weighted", 0.0)                # X7: 駒種重み付き
        x8  = scores.get("x8Ratio", 0.0)                   # X8: 駒種/駒数比
        x9  = metrics.get("pieceUtilization", 0.0)         # X9: 駒使用率
        x10 = scores.get("x10Difficulty", 0.0)             # X10: 難解さ
        x11 = metrics.get("discoveredChecks", 0) + metrics.get("doubleChecks", 0)  # X11
        x12 = metrics.get("kingFinalEdgeDist", 0)          # X12: 玉の辺距離

        aesthetic = scores.get("aestheticScore", 0.0)
        perfect   = rec.get("perfect", False)

        if not header_written:
            out.write("src\tdiff\tmoves\tstatus\tx0\tx5\tx6\tx7\tx8\tx10\taesthetic\n")
            header_written = True

        out.write(f"{src}\t{diff}\t{moves}\t{status}\t{x0}\t{x5:.2f}\t{x6}\t{x7:.2f}\t{x8:.2f}\t{x10:.2f}\t{aesthetic:.2f}\n")
        out.flush()
        print(f"[{idx}/{total}] {status.upper():<12} {src}  x0={x0} x5={x5:.0f} x6={x6} x10={x10:.0f} diff={diff}", flush=True)

print("Done.", flush=True)
