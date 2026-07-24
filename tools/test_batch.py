"""Quick sanity test: run tsume_verify on a sample of converted problems."""
import sys, os, subprocess, json, time
sys.stdout.reconfigure(encoding='utf-8')

ENGINE = r"C:\Users\lovek\Documents\KomoringHeights\source\KomoringHeights-by-gcc.exe"
TSV    = r"G:\My Drive\Shogi\T_Shogi\all_problems.tsv"

def run_engine(sfen, usi_moves, max_ply, max_nodes=5_000_000, timeout=20):
    move_args = ' '.join(usi_moves)
    cmd_lines = ['usi', 'isready',
                 f'position sfen {sfen}',
                 f'user tsume_verify {max_ply} {max_nodes}{" " + move_args if move_args else ""}',
                 'quit']
    try:
        r = subprocess.run([ENGINE], input='\n'.join(cmd_lines)+'\n',
                           capture_output=True, text=True, timeout=timeout,
                           encoding='utf-8', errors='replace')
        for line in r.stdout.splitlines():
            if 'tsume_json' in line:
                idx = line.find('tsume_json ')
                if idx >= 0:
                    return json.loads(line[idx+11:])
    except Exception as e:
        return {'error': str(e)}
    return None

with open(TSV, encoding='utf-8') as f:
    rows = [l.rstrip('\n').split('\t') for l in f.readlines()[1:]]

# Sample: first 5 of each difficulty 1-4 with odd move count
samples = {}
for r in rows:
    if len(r) < 5: continue
    src, diff_s, moves_s, sfen, usi = r[0], r[1], r[2], r[3], r[4]
    if not sfen: continue
    try:
        diff = int(diff_s); moves = int(moves_s)
    except: continue
    if moves % 2 == 0 or moves < 7 or moves > 21: continue
    diff_key = diff
    samples.setdefault(diff_key, [])
    if len(samples[diff_key]) < 5:
        samples[diff_key].append((src, diff, moves, sfen, usi.split() if usi else []))

print(f"{'SRC':<30} {'DIFF'} {'PLY':>4} {'STATUS':<12} {'MATE?':>5} {'PERFECT':>7} {'SCORE':>6}")
print('-'*80)

for diff_key in sorted(samples):
    for src, diff, moves, sfen, usi in samples[diff_key]:
        rec = run_engine(sfen, usi, moves + 2)
        if rec is None:
            print(f"{src:<30} {diff}  {moves:>4}  {'TIMEOUT':<12}"); continue
        if 'error' in rec:
            print(f"{src:<30} {diff}  {moves:>4}  {'ERROR':<12}"); continue
        status = rec.get('status','?')
        mate_ply = rec.get('matePly', -1)
        perfect = rec.get('perfect', False)
        aesthetic = rec.get('scores', {}).get('aestheticScore', 0)
        intend = rec.get('intendedMismatch', False)
        flag = ' [SHORTER!]' if status == 'mate' and mate_ply > 0 and mate_ply < moves else ''
        flag += ' [MISMATCH]' if intend else ''
        print(f"{src:<30} {diff}  {moves:>4}  {status:<12} {mate_ply:>5} {str(perfect):>7} {aesthetic:>6.1f}{flag}")
