import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REQUIRED_REQUEST_SCHEMA = 'blueprint-lens-layout-request.v1';
const RESPONSE_SCHEMA = 'blueprint-lens-layout-response.v1';
const REQUIRED_ELK_VERSION = '0.12.0';

function fail(code, error) {
  const message = error instanceof Error ? error.message : String(error ?? 'unknown error');
  process.stderr.write(`${code}:${message}\n`);
  process.exitCode = 1;
}

function argumentValue(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length
    ? process.argv[index + 1]
    : '';
}

async function main() {
  const elkRoot = argumentValue('--elk-root');
  if (!elkRoot) {
    fail('BLUEPRINT_LENS_ELK_ROOT_MISSING', 'missing --elk-root');
    return;
  }

  const requestChunks = [];
  for await (const chunk of process.stdin) {
    requestChunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }
  const requestText = Buffer.concat(requestChunks).toString('utf8');
  if (!requestText.trim()) {
    fail('BLUEPRINT_LENS_ELK_INPUT_EMPTY', 'stdin is empty');
    return;
  }

  let envelope;
  try {
    envelope = JSON.parse(requestText);
  } catch (error) {
    fail('BLUEPRINT_LENS_ELK_INPUT_MALFORMED', error);
    return;
  }
  if (!envelope || envelope.schema_version !== REQUIRED_REQUEST_SCHEMA ||
      !envelope.graph || typeof envelope.graph !== 'object') {
    fail('BLUEPRINT_LENS_ELK_REQUEST_SCHEMA_INVALID', 'unsupported request schema');
    return;
  }

  const packageJson = JSON.parse(
    await readFile(path.join(elkRoot, 'package.json'), 'utf8'));
  if (packageJson.version !== REQUIRED_ELK_VERSION) {
    fail(
      'BLUEPRINT_LENS_ELK_VERSION_UNSUPPORTED',
      `expected ${REQUIRED_ELK_VERSION}, got ${packageJson.version}`);
    return;
  }

  const require = createRequire(import.meta.url);
  const Elk = require(path.join(elkRoot, 'lib', 'elk.bundled.js'));
  const elk = new Elk();
  const graph = await elk.layout(envelope.graph);
  process.stdout.write(JSON.stringify({
    schema_version: 'blueprint-lens-layout-response.v1',
    backend: 'ELK.Layered',
    backend_version: `ELK.js ${packageJson.version}`,
    graph
  }));
}

main().catch((error) => fail('BLUEPRINT_LENS_ELK_RUNTIME_ERROR', error));
