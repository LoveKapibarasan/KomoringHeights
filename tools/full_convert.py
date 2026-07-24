"""
KIF -> SFEN + USI moves batch converter for tsume shogi problem collections.
Outputs all_problems.tsv with columns: src, diff, moves, sfen, usi
"""
import sys, os, re, zipfile
sys.stdout.reconfigure(encoding='utf-8')

BASE = "G:/My Drive/Shogi/T_Shogi"
OUT  = os.path.join(BASE, "all_problems.tsv")

JP_TO_SFEN = {
    '歩': 'P', '香': 'L', '桂': 'N', '銀': 'S', '金': 'G',
    '角': 'B', '飛': 'R', '玉': 'K', '王': 'K',
    'と': '+P', '成香': '+L', '成桂': '+N', '成銀': '+S',
    '馬': '+B', '竜': '+R', '龍': '+R',
}

HAND_JP = {
    '歩': 'P', '香': 'L', '桂': 'N', '銀': 'S', '金': 'G', '角': 'B', '飛': 'R',
}

KANJI_NUM = {'一':1,'二':2,'三':3,'四':4,'五':5,'六':6,'七':7,'八':8,'九':9,
             '十':10,'十一':11,'十二':12,'十三':13,'十四':14,'十五':15,'十六':16,
             '十七':17,'十八':18}

MONTH_DIFF = {'s':1, 'c':2, 'k':3, 't':4}

# KIF file number → halfwidth ASCII
FW_DIGIT = {chr(0xFF10+i): str(i) for i in range(10)}  # ０-９ → 0-9
FW_DIGIT.update({'１':'1','２':'2','３':'3','４':'4','５':'5','６':'6','７':'7','８':'8','９':'9','０':'0'})
# Rank kanji → USI rank letter
RANK_TO_USI = {'一':'a','二':'b','三':'c','四':'d','五':'e','六':'f','七':'g','八':'h','九':'i'}
# Piece to USI drop type (base type)
JP_DROP_TYPE = {
    '歩':'P','香':'L','桂':'N','銀':'S','金':'G','角':'B','飛':'R',
    'と':'P','成香':'L','成桂':'N','成銀':'S','馬':'B','竜':'R','龍':'R',
}

def parse_hand(text):
    result = {}
    if not text or '駒なし' in text or 'なし' in text:
        return result
    i = 0
    while i < len(text):
        if text[i] in (' ', '\u3000', '　', '\t'):
            i += 1; continue
        found = False
        for jp, sf in HAND_JP.items():
            if text[i:i+len(jp)] == jp:
                i += len(jp)
                num_str = ''
                j = i
                for klen in (2, 1):
                    cand = text[j:j+klen]
                    if cand in KANJI_NUM:
                        num_str = str(KANJI_NUM[cand]); i = j + klen; break
                if not num_str:
                    while i < len(text) and text[i].isdigit(): num_str += text[i]; i += 1
                if not num_str: num_str = '1'
                result[sf] = result.get(sf, 0) + int(num_str)
                found = True; break
        if not found: i += 1
    return result

def hand_to_sfen(hand_dict, uppercase):
    order = ['R','B','G','S','N','L','P']
    parts = []
    for p in order:
        c = hand_dict.get(p, 0)
        if c > 0:
            s = (p if uppercase else p.lower())
            parts.append(('' if c == 1 else str(c)) + s)
    return ''.join(parts)

def parse_board(lines):
    board = []
    for line in lines:
        line = line.strip()
        if not line.startswith('|'): continue
        line = re.sub(r'\|[一二三四五六七八九]$', '', line)
        line = line.lstrip('|').rstrip('|')
        row = []
        i = 0
        while i < len(line) and len(row) < 9:
            c = line[i]
            if c in (' ', '\u3000', '　'): i += 1; continue
            if c == 'v':
                i += 1
                if i >= len(line): break
                piece = None
                for plen in (2, 1):
                    cand = line[i:i+plen]
                    if cand in JP_TO_SFEN:
                        sf = JP_TO_SFEN[cand].lower()
                        if sf in ('+k','k'): sf = 'k'
                        row.append(sf); i += plen; piece = sf; break
                if piece is None: row.append(''); i += 1
            elif c == '・':
                row.append(''); i += 1
            else:
                piece = None
                for plen in (2, 1):
                    cand = line[i:i+plen]
                    if cand in JP_TO_SFEN:
                        sf = JP_TO_SFEN[cand]
                        row.append(sf); i += plen; piece = sf; break
                if piece is None: i += 1; continue
        while len(row) < 9: row.append('')
        board.append(row[:9])
    return board

def board_to_sfen_rows(board):
    ranks = []
    for row in board:
        rank_str = ''; empty = 0
        for cell in row:
            if cell == '': empty += 1
            else:
                if empty: rank_str += str(empty); empty = 0
                rank_str += cell
        if empty: rank_str += str(empty)
        ranks.append(rank_str)
    return '/'.join(ranks)

def kif_sq_to_usi(file_char, rank_char):
    """Convert KIF file (fullwidth digit) + rank (kanji) to USI square like '2d'."""
    f = FW_DIGIT.get(file_char, file_char)
    r = RANK_TO_USI.get(rank_char, '?')
    return f + r

def parse_kif_moves(lines):
    """
    Parse KIF move lines into USI move strings.
    Returns list of USI move strings for the main line.
    """
    moves = []
    last_dest = None  # USI square of last destination (for "同")

    for line in lines:
        line = line.rstrip()
        m = re.match(r'^\s*([0-9]+)\s+(.+?)(?:\s*\([0-9]+\))?\s*(?:\([0-9:]+/.*\))?$', line)
        if not m:
            continue
        move_num = int(m.group(1))
        move_text = m.group(2).strip()

        # Skip variations (不完全), comments, and termination
        if '変化' in move_text or '中断' in move_text:
            break
        if move_text in ('詰み', '投了', '反則', '千日手'):
            break
        if 'まで' in move_text:
            break

        # Try to parse the move
        usi_move = None

        # Check for "同" (same destination)
        is_same = move_text.startswith('同')
        if is_same:
            dest = last_dest
            rest = move_text[1:].lstrip('　 ')
        else:
            # First two chars should be file (fullwidth digit) + rank (kanji)
            if len(move_text) < 2:
                continue
            fc = move_text[0]
            rc = move_text[1]
            if fc not in FW_DIGIT or rc not in RANK_TO_USI:
                continue  # Not a move line
            dest = kif_sq_to_usi(fc, rc)
            rest = move_text[2:]

        # Determine piece and whether it's a drop
        is_drop = rest.endswith('打') or '打' in rest
        is_promo = rest.endswith('成') or '成' in rest and not any(rest.endswith(p) for p in ['成香','成桂','成銀'])

        # Find source square from (xy) pattern
        src_m = re.search(r'\(([0-9])([0-9])\)', line)
        if src_m:
            sf = src_m.group(1)
            sr = src_m.group(2)
            rank_map = {'1':'a','2':'b','3':'c','4':'d','5':'e','6':'f','7':'g','8':'h','9':'i'}
            src_sq = sf + rank_map.get(sr, '?')
        else:
            src_sq = None

        if is_drop:
            # Find piece type from rest
            piece_type = None
            for jp, sf in JP_DROP_TYPE.items():
                if jp in rest:
                    piece_type = sf; break
            if piece_type and dest:
                usi_move = f"{piece_type}*{dest}"
        elif src_sq and dest:
            promo_suffix = '+' if is_promo else ''
            usi_move = f"{src_sq}{dest}{promo_suffix}"

        if usi_move:
            moves.append(usi_move)
            last_dest = dest

    return moves

def parse_kif(text):
    lines = text.splitlines()
    sente_hand_str = ''; gote_hand_str = ''
    board_lines = []; move_lines = []
    in_board = False; past_header = False

    for line in lines:
        ls = line.rstrip()

        if '先手の持駒' in ls:
            m = re.search(r'先手の持駒[：:]\s*(.*)', ls)
            if m: sente_hand_str = m.group(1)
        if '後手の持駒' in ls:
            m = re.search(r'後手の持駒[：:]\s*(.*)', ls)
            if m: gote_hand_str = m.group(1)

        if '+--' in ls:
            in_board = not in_board
            if not in_board: past_header = True
            continue

        if in_board and ls.startswith('|'):
            board_lines.append(ls)
            continue

        if past_header and re.match(r'^\s*[0-9]+\s+', ls):
            move_lines.append(ls)

    if len(board_lines) != 9:
        return None

    board = parse_board(board_lines)
    if len(board) != 9:
        return None

    pos_str = board_to_sfen_rows(board)
    sente_hand = parse_hand(sente_hand_str)
    gote_hand  = parse_hand(gote_hand_str)
    sh = hand_to_sfen(sente_hand, uppercase=True)
    gh = hand_to_sfen(gote_hand,  uppercase=False)
    hand_str = (sh + gh) if (sh or gh) else '-'
    # Only output attacker's hand in SFEN — defender's hand is computed by
    # CompleteDefenderReserve (39-piece rule) inside the engine.
    sh_only = sh if sh else '-'
    sfen = f"{pos_str} b {sh_only} 1"

    usi_moves = parse_kif_moves(move_lines)
    move_count = len(usi_moves)
    return sfen, move_count, usi_moves

def process_kif_bytes(raw_bytes, src, diff):
    for enc in ('cp932', 'utf-8', 'shift_jis'):
        try:
            text = raw_bytes.decode(enc); break
        except Exception: continue
    else:
        return None

    result = parse_kif(text)
    if result is None:
        return None
    sfen, moves, usi_moves = result
    return (src, diff, moves, sfen, ' '.join(usi_moves))

problems = []
seen_sfens = set()

for folder in sorted(os.listdir(BASE)):
    if not re.match(r'^\d{6}$', folder): continue
    folder_path = os.path.join(BASE, folder)
    if not os.path.isdir(folder_path): continue
    for fn in sorted(os.listdir(folder_path)):
        if not fn.lower().endswith('.kif'): continue
        first_char = fn[0].lower()
        diff = MONTH_DIFF.get(first_char, 0)
        src = f"{folder}/{fn}"
        with open(os.path.join(folder_path, fn), 'rb') as f:
            raw = f.read()
        rec = process_kif_bytes(raw, src, diff)
        if rec:
            _, _, moves, sfen, usi = rec
            if sfen not in seen_sfens:
                seen_sfens.add(sfen)
                problems.append(rec)
        else:
            problems.append((src, diff, 0, '', ''))

zip_path = os.path.join(BASE, '9784839975418.zip')
if os.path.exists(zip_path):
    with zipfile.ZipFile(zip_path, 'r') as zf:
        for name in sorted(zf.namelist()):
            if not name.lower().endswith('.kif'): continue
            raw = zf.read(name)
            src = f"zip/{os.path.basename(name)}"
            rec = process_kif_bytes(raw, src, 0)
            if rec:
                _, _, moves, sfen, usi = rec
                if sfen not in seen_sfens:
                    seen_sfens.add(sfen)
                    problems.append(rec)
            else:
                problems.append((src, 0, 0, '', ''))

with open(OUT, 'w', encoding='utf-8') as f:
    f.write("src\tdiff\tmoves\tsfen\tusi\n")
    for src, diff, moves, sfen, usi in problems:
        f.write(f"{src}\t{diff}\t{moves}\t{sfen}\t{usi}\n")

print(f"Total: {len(problems)} problems")
from collections import Counter
ctr = Counter(moves for _, _, moves, _, _ in problems)
for k in sorted(ctr): print(f"  {k}手: {ctr[k]}")

# Show a sample
print("\nSample (c1.kif):")
for rec in problems[:1]:
    print(f"  SFEN: {rec[3]}")
    print(f"  USI:  {rec[4]}")
