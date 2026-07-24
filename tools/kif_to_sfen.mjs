/**
 * Batch KIF → SFEN converter using tsshogi.
 * Reads all KIF from monthly folders + ZIP, outputs all_problems.tsv.
 * Usage: node kif_to_sfen.mjs
 */
import { importKIF } from './dist/esm/index.mjs';
import { readFileSync, writeFileSync, readdirSync, statSync } from 'fs';
import { join, basename } from 'path';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const AdmZip = (() => { try { return require('adm-zip'); } catch { return null; } })();

const BASE   = 'G:/My Drive/Shogi/T_Shogi';
const OUT    = BASE + '/all_problems.tsv';
const MONTH_DIFF = { s: 1, c: 2, k: 3, t: 4 };

const RANK_TO_USI = ['a','b','c','d','e','f','g','h','i'];
const PIECE_TO_USI = {
  pawn: 'P', lance: 'L', knight: 'N', silver: 'S', gold: 'G',
  bishop: 'B', rook: 'R', king: 'K',
  'promoted_pawn': '+P', 'promoted_lance': '+L', 'promoted_knight': '+N',
  'promoted_silver': '+S', 'promoted_bishop': '+B', 'promoted_rook': '+R',
  dragon: '+R', horse: '+B', tokin: '+P',
};

function squareToUSI(sq) {
  return `${sq.file}${RANK_TO_USI[sq.rank - 1]}`;
}

function moveToUSI(move) {
  if (!move || move.type === 'start' || move.type === 'resign' || move.type === 'draw') return null;
  if (!move.to) return null;
  const to = squareToUSI(move.to);
  if (typeof move.from === 'string') {
    // Drop move: from is piece type string
    const pt = PIECE_TO_USI[move.from] || PIECE_TO_USI[move.pieceType];
    return pt ? `${pt.replace('+','')}*${to}` : null;
  }
  const from = squareToUSI(move.from);
  return `${from}${to}${move.promote ? '+' : ''}`;
}

function parseSFEN_attackerOnly(fullSFEN) {
  // fullSFEN includes both attacker and defender hands.
  // Return only with attacker (uppercase) hand pieces — engine fills defender via 39-piece rule.
  const parts = fullSFEN.split(' ');
  if (parts.length < 4) return fullSFEN;
  const [board, turn, hand, moveNo] = parts;
  let attackerHand = '';
  if (hand !== '-') {
    let count = '';
    for (const c of hand) {
      if (c >= '0' && c <= '9') { count += c; continue; }
      const n = count ? count : '1'; count = '';
      if (c === c.toUpperCase()) {
        attackerHand += (n !== '1' ? n : '') + c;
      }
    }
  }
  return `${board} ${turn} ${attackerHand || '-'} ${moveNo}`;
}

function decodeKIF(buf) {
  try { return new TextDecoder('shift_jis').decode(buf); } catch {}
  try { return new TextDecoder('cp932').decode(buf); } catch {}
  return buf.toString('binary');
}

function processKIF(content, src, diff) {
  let record;
  try { record = importKIF(content); } catch { return null; }
  if (!record || record instanceof Error || !record.initialPosition) return null;

  const fullSFEN = record.initialPosition.sfen;
  const sparseSFEN = parseSFEN_attackerOnly(fullSFEN);

  const moves = [];
  let node = record.first;
  while (node && node.next) {
    const m = moveToUSI(node.next.move);
    if (m) moves.push(m);
    node = node.next;
    // Stop at variation branches (follow main line only via .next)
  }

  return { src, diff, moves: moves.length, sfen: sparseSFEN, usi: moves.join(' ') };
}

// ---- Collect all KIFs ----
const problems = [];
const seenSFENs = new Set();

// Monthly folders
const folderRE = /^\d{6}$/;
for (const folder of readdirSync(BASE).sort()) {
  if (!folderRE.test(folder)) continue;
  const folderPath = join(BASE, folder);
  if (!statSync(folderPath).isDirectory()) continue;
  for (const fn of readdirSync(folderPath).sort()) {
    if (!fn.toLowerCase().endsWith('.kif')) continue;
    const diff = MONTH_DIFF[fn[0].toLowerCase()] ?? 0;
    const src = `${folder}/${fn}`;
    const buf = readFileSync(join(folderPath, fn));
    const text = decodeKIF(buf);
    const rec = processKIF(text, src, diff);
    if (rec && !seenSFENs.has(rec.sfen)) {
      seenSFENs.add(rec.sfen);
      problems.push(rec);
    } else if (!rec) {
      problems.push({ src, diff, moves: 0, sfen: '', usi: '' });
    }
  }
}

// ZIP file
const zipPath = join(BASE, '9784839975418.zip');
if (AdmZip) {
  const zip = new AdmZip(zipPath);
  for (const entry of zip.getEntries().sort((a, b) => a.entryName.localeCompare(b.entryName))) {
    if (!entry.entryName.toLowerCase().endsWith('.kif')) continue;
    const src = `zip/${basename(entry.entryName)}`;
    const buf = entry.getData();
    const text = decodeKIF(buf);
    const rec = processKIF(text, src, 0);
    if (rec && !seenSFENs.has(rec.sfen)) {
      seenSFENs.add(rec.sfen);
      problems.push(rec);
    } else if (!rec) {
      problems.push({ src, diff: 0, moves: 0, sfen: '', usi: '' });
    }
  }
} else {
  process.stderr.write('adm-zip not available; skipping ZIP\n');
}

// Write output
let tsv = 'src\tdiff\tmoves\tsfen\tusi\n';
for (const { src, diff, moves, sfen, usi } of problems) {
  tsv += `${src}\t${diff}\t${moves}\t${sfen}\t${usi}\n`;
}
writeFileSync(OUT, tsv, 'utf8');

// Stats
const byMoves = {};
for (const { moves } of problems) { byMoves[moves] = (byMoves[moves] ?? 0) + 1; }
console.log(`Total: ${problems.length} problems`);
for (const k of Object.keys(byMoves).map(Number).sort((a,b)=>a-b)) {
  console.log(`  ${k}手: ${byMoves[k]}`);
}

// Sample
const sample = problems.find(p => p.src.includes('202510/s4.kif'));
if (sample) {
  console.log('\ns4.kif:', sample.sfen);
  console.log('  USI:', sample.usi);
}
