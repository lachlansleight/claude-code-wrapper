#!/usr/bin/env node
// Bundles the bridge and its dependencies into one ES module that runs on a
// bare Node 20+ with no node_modules: this is what the Sultana app embeds.
//
//   node scripts/bundle.mjs [outdir]     (default: dist/bundle)
//
// Writes <outdir>/bridge.mjs and <outdir>/licenses/ (notices for the bundled
// packages, which their licences require to travel with the code).

import { build } from 'esbuild'
import { copyFileSync, mkdirSync, rmSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const outdir = resolve(process.argv[2] ?? join(root, 'dist/bundle'))

/** Bundled packages whose licence files ship beside the bundle. */
const BUNDLED_PACKAGES = ['ws']

await build({
  entryPoints: [join(root, 'src/index.ts')],
  outfile: join(outdir, 'bridge.mjs'),
  bundle: true,
  platform: 'node',
  target: 'node20',
  format: 'esm',
  // ws require()s these optional native speed-ups inside try/catch.
  external: ['bufferutil', 'utf-8-validate'],
  // ws is CommonJS and require()s Node built-ins, which an ES module bundle
  // can't do without a real require.
  banner: { js: "import { createRequire } from 'node:module'; const require = createRequire(import.meta.url);" },
  legalComments: 'eof',
  logLevel: 'warning',
})

const licenses = join(outdir, 'licenses')
rmSync(licenses, { recursive: true, force: true })
mkdirSync(licenses, { recursive: true })
for (const name of BUNDLED_PACKAGES) {
  copyFileSync(join(root, 'node_modules', name, 'LICENSE'), join(licenses, `${name}.txt`))
}
console.log(`bundled ${join(outdir, 'bridge.mjs')}`)
