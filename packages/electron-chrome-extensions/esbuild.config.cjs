const esbuild = require('esbuild')

const external = ['electron', 'electron-chrome-extensions/preload']

const configs = [
  {
    entryPoints: ['src/index.ts'],
    outfile: 'dist/cjs/index.js',
    bundle: true,
    platform: 'node',
    format: 'cjs',
    external,
  },
  {
    entryPoints: ['src/index.ts'],
    outfile: 'dist/esm/index.mjs',
    bundle: true,
    platform: 'node',
    format: 'esm',
    external,
  },
  {
    entryPoints: ['src/preload.ts'],
    outfile: 'dist/chrome-extension-api.preload.js',
    bundle: true,
    platform: 'browser',
    format: 'iife',
    external,
    sourcemap: false,
  },
  {
    entryPoints: ['src/browser-action.ts'],
    outfile: 'dist/cjs/browser-action.js',
    bundle: true,
    platform: 'browser',
    format: 'cjs',
    external,
    sourcemap: false,
  },
  {
    entryPoints: ['src/browser-action.ts'],
    outfile: 'dist/esm/browser-action.mjs',
    bundle: true,
    platform: 'browser',
    format: 'esm',
    external,
    sourcemap: false,
  },
]

Promise.all(configs.map(c => esbuild.build(c)))
  .then(() => console.log('electron-chrome-extensions built successfully'))
  .catch(e => { console.error(e); process.exit(1) })
