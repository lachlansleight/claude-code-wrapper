// Repair common mojibake from UTF-8 decoded as Windows-1252 or Latin-1 (per-byte).
// See WHATWG index-windows-1252 for bytes 0x80–0xFF.
// https://encoding.spec.whatwg.org/index-windows-1252.txt

const WIN1252_CODEPOINTS: readonly number[] = [
  0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d,
  0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a,
  0x153, 0x9d, 0x17e, 0x178, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac,
  0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe,
  0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0,
  0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
  0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
]

function cp1252ByteToCodePoint(b: number): number {
  if (b < 0x80) return b
  return WIN1252_CODEPOINTS[b - 0x80]!
}

/** UTF-8 codepoint for valid 3-byte sequence 0xE2 0x80 0x80–0xBF. */
function utf8E280xxToCodePoint(xx: number): number {
  return ((0xe2 & 0x0f) << 12) | ((0x80 & 0x3f) << 6) | (xx & 0x3f)
}

function buildE280Replacements(): { bad: string; good: string }[] {
  const out: { bad: string; good: string }[] = []
  const seen = new Set<string>()
  const push = (bad: string, good: string) => {
    if (bad === good || seen.has(bad)) return
    seen.add(bad)
    out.push({ bad, good })
  }

  for (let xx = 0x80; xx <= 0xbf; xx++) {
    const good = String.fromCodePoint(utf8E280xxToCodePoint(xx))
    const badCp1252 = String.fromCodePoint(
      cp1252ByteToCodePoint(0xe2),
      cp1252ByteToCodePoint(0x80),
      cp1252ByteToCodePoint(xx),
    )
    const badLatin1 = String.fromCharCode(0xe2, 0x80, xx)
    push(badCp1252, good)
    push(badLatin1, good)
  }
  return out
}

const E280_REPLACEMENTS = buildE280Replacements()

/** NBSP: UTF-8 C2 A0 read as cp1252 / Latin-1 → U+00C2 U+00A0 */
const NBSP_MOJIBAKE = '\u00C2\u00A0'
const NBSP_CHAR = '\u00A0'

export function repairMojibakeInString(s: string): string {
  let out = s
  for (const { bad, good } of E280_REPLACEMENTS) {
    if (out.includes(bad)) out = out.split(bad).join(good)
  }
  if (out.includes(NBSP_MOJIBAKE)) out = out.split(NBSP_MOJIBAKE).join(NBSP_CHAR)
  return out
}

function isPlainObject(v: unknown): v is Record<string, unknown> {
  if (typeof v !== 'object' || v === null || Array.isArray(v)) return false
  const proto = Object.getPrototypeOf(v)
  return proto === Object.prototype || proto === null
}

/** Deep-repair all string values (hook JSON trees, parsed events). */
export function repairMojibakeDeep<T>(value: T): T {
  if (typeof value === 'string') return repairMojibakeInString(value) as T
  if (value === null || typeof value !== 'object') return value
  if (Array.isArray(value)) {
    const arr = value as unknown[]
    for (let i = 0; i < arr.length; i++) arr[i] = repairMojibakeDeep(arr[i])
    return value
  }
  if (!isPlainObject(value)) return value
  const o = value as Record<string, unknown>
  for (const k of Object.keys(o)) o[k] = repairMojibakeDeep(o[k])
  return value
}
