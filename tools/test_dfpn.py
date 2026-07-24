"""Test SFEN conversion quality using df-pn solver (not full verification).
Reports whether the engine finds a mate, and whether the mate length matches."""
import sys, subprocess, re
sys.stdout.reconfigure(encoding='utf-8')
ENGINE = r"C:\Users\lovek\Documents\KomoringHeights\source\KomoringHeights-by-gcc.exe"
TSV    = r"G:\My Drive\Shogi\T_Shogi\all_problems.tsv"

def get_mate(sfen, time_ms=3000):
    cmd = ['usi', 'setoption name USI_Hash value 256',
           'isready', f'position sfen {sfen}', f'go mate {time_ms}', 'quit']
    try:
        r = subprocess.run([ENGINE], input='\n'.join(cmd)+'\n',
                           capture_output=True, text=True, timeout=time_ms/1000+3,
                           encoding='utf-8', errors='replace')
        for line in r.stdout.splitlines():
            m = re.search(r'checkmate (.+)', line)
            if m: return ('mate', m.group(1).strip().split())
            if 'nomate' in line: return ('nomate', [])
    except Exception as e:
        return ('error', [])
    return ('timeout', [])

with open(TSV, encoding='utf-8') as f:
    rows = [l.rstrip('\n').split('\t') for l in f.readlines()[1:]]

# Sample: first 3 of each difficulty 1-4 with 7-9 ply (quick to solve)
samples = {}
for r in rows:
    if len(r) < 5: continue
    src, diff_s, moves_s, sfen, usi = r[0], r[1], r[2], r[3], r[4]
    if not sfen: continue
    try: diff = int(diff_s); moves = int(moves_s)
    except: continue
    if moves not in (7, 9): continue
    samples.setdefault(diff, [])
    if len(samples[diff]) < 4:
        samples[diff].append((src, diff, moves, sfen))

print(f"{'SRC':<35} {'DIFF'} {'KIF-PLY'} {'FOUND':<8} {'ENGINE-PLY'}")
print('-'*75)
ok = short = no = err = 0
for diff_key in sorted(samples):
    for src, diff, kif_ply, sfen in samples[diff_key]:
        status, moves = get_mate(sfen)
        if status == 'mate':
            ply = len(moves)
            if ply == kif_ply:
                tag = 'OK'; ok += 1
            elif ply < kif_ply:
                tag = f'SHORTER({ply})'; short += 1
            else:
                tag = f'LONGER({ply})'; ok += 1
            print(f"{src:<35} {diff}    {kif_ply}    {tag}")
        elif status == 'nomate':
            print(f"{src:<35} {diff}    {kif_ply}    NOMATE"); no += 1
        else:
            print(f"{src:<35} {diff}    {kif_ply}    {status.upper()}"); err += 1

print(f"\nSummary: OK={ok}  SHORTER={short}  NOMATE/ERROR={no+err}  (out of {ok+short+no+err})")
