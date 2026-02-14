import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

const checks = {
  'darwin-arm64': ['@rollup/rollup-darwin-arm64', '@esbuild/darwin-arm64'],
  'darwin-x64': ['@rollup/rollup-darwin-x64', '@esbuild/darwin-x64'],
}

const key = `${process.platform}-${process.arch}`
const requiredPkgs = checks[key]

if (!requiredPkgs) {
  process.exit(0)
}

const missing = []
for (const pkg of requiredPkgs) {
  try {
    require.resolve(`${pkg}/package.json`)
  } catch {
    try {
      require.resolve(pkg)
    } catch {
      missing.push(pkg)
    }
  }
}

if (missing.length === 0) {
  process.exit(0)
}

console.error('[web precheck] Missing platform optional deps:', missing.join(', '))
console.error('[web precheck] This usually means node_modules came from a different OS/arch.')
console.error('[web precheck] Fix:')
console.error('  1) Ensure npm can access registry (proxy/network)')
console.error('  2) Run: npm --prefix server/web install --platform=' + process.platform + ' --arch=' + process.arch + ' --no-audit')
process.exit(1)
