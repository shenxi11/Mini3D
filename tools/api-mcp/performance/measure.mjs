/*
 * 模块名: 正式 pipe 外部性能测量
 * 功能概述: 启动自有可见探针，测完整请求往返、阶段客户端成本和持续资源窗口。
 * 对外接口: node measure.mjs --probe --output --cases --samples --warmup。
 * 依赖关系: Node 内置 fs/net/child_process/crypto，冻结 wire v1。
 * 输入输出: typed 夹具与正式实例描述文件到可复跑成对性能 JSON。
 * 异常与错误: 接口/采样失败保存短日志并仅结束自身 PID。
 * 维护说明: 不输出 secret/参数/base64；同一时刻只拥有一个 probe/pipe。
 */
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { once } from 'node:events';

const options = Object.fromEntries(Array.from({ length: (process.argv.length - 2) / 2 }, (_, i) =>
  [process.argv[2 + i * 2].replace(/^--/, ''), process.argv[3 + i * 2]]));
const sampleCount = Number(options.samples ?? 20);
const warmup = Number(options.warmup ?? 3);
const captureCount = Number(options['capture-samples'] ?? 10);
const captureEdge = Number(options['capture-longest-edge'] ?? 960);
const windowSeconds = Number(options['window-seconds'] ?? 5);
const windowCount = Number(options['window-count'] ?? 5);
const cases = (options.cases ?? 'mesh:1000,mesh:10000,mesh:100000,entities:1000,entities:10000')
  .split(',').map(value => { const [scenario, size] = value.split(':'); return { scenario, size: Number(size) }; });
const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const now = () => process.hrtime.bigint();
const elapsedMs = started => Number(now() - started) / 1e6;
const rank = (values, fraction) => [...values].sort((a, b) => a - b)[Math.max(1, Math.ceil(values.length * fraction)) - 1];
const summary = (method, records, warmups) => ({ method, timingScope: 'external Node encode + named pipe round trip + Node decode',
  sampleCount: records.length, medianMs: rank(records.map(item => item.ms), .5),
  p95Ms: rank(records.map(item => item.ms), .95), requestFrameBytesMedian: rank(records.map(item => item.requestBytes), .5),
  responseFrameBytesMedian: rank(records.map(item => item.responseBytes), .5),
  clientRequestJsonEncodeMsMedian: rank(records.map(item => item.jsonEncodeMs), .5),
  clientResponseJsonDecodeMsMedian: rank(records.map(item => item.jsonDecodeMs), .5),
  samples: records, warmupsMs: warmups.map(item => item.ms) });

/** @brief 事件驱动正式帧客户端；每条超时只清理自己的等待记录。 */
class Client {
  constructor(socket) {
    this.socket = socket;
    this.nextId = 0;
    this.pending = new Map();
    this.bytes = Buffer.alloc(0);
    socket.on('data', chunk => {
      this.bytes = Buffer.concat([this.bytes, chunk]);
      while (this.bytes.length >= 4) {
        const size = this.bytes.readUInt32LE(0);
        if (size > 16 * 1024 * 1024) throw new Error('Response exceeded formal limit.');
        if (this.bytes.length < size + 4) break;
        const started = now();
        const response = JSON.parse(this.bytes.subarray(4, size + 4).toString('utf8'));
        const jsonDecodeMs = elapsedMs(started);
        this.bytes = this.bytes.subarray(size + 4);
        const pending = this.pending.get(response.id);
        if (!pending) throw new Error('Unexpected response id.');
        this.pending.delete(response.id);
        clearTimeout(pending.timeout);
        pending.resolve({ response, ms: elapsedMs(pending.started), id: response.id,
          requestBytes: pending.requestBytes, responseBytes: size + 4,
          jsonEncodeMs: pending.jsonEncodeMs, jsonDecodeMs });
      }
    });
    const fail = error => {
      for (const item of this.pending.values()) { clearTimeout(item.timeout); item.reject(error); }
      this.pending.clear();
    };
    socket.on('error', fail);
    socket.on('close', () => fail(new Error('Owned pipe closed with unfinished requests.')));
  }
  call(method, params = {}) {
    const id = `pipe-${++this.nextId}`;
    const started = now();
    const payload = Buffer.from(JSON.stringify({ jsonrpc: '2.0', id, method, params }), 'utf8');
    const frame = Buffer.allocUnsafe(payload.length + 4);
    frame.writeUInt32LE(payload.length, 0);
    payload.copy(frame, 4);
    const jsonEncodeMs = elapsedMs(started);
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { this.pending.delete(id); reject(new Error(`${method} exceeded owned 35s wait`)); }, 35000);
      this.pending.set(id, { resolve, reject, timeout, started, requestBytes: frame.length, jsonEncodeMs });
      this.socket.write(frame);
    });
  }
}
function requireResponse(item, method) {
  if (!item.response.result) {
    const error = item.response.error;
    throw new Error(`${method}: ${error?.data?.code ?? error?.code ?? 'missing_result'} ${error?.data?.fieldPath ?? ''}`);
  }
  return item.response.result;
}
function createGrid(size) {
  const [columns, rows] = size === 1000 ? [40, 25] : [100, 100];
  const positions = [];
  const faces = [];
  for (let y = 0; y < rows; y++) for (let x = 0; x < columns; x++)
    positions.push([Math.fround(-5 + Math.fround(Math.fround(10 * x) / (columns - 1))),
      Math.fround(-5 + Math.fround(Math.fround(10 * y) / (rows - 1))), 0]);
  for (let y = 0; y < rows - 1; y++) for (let x = 0; x < columns - 1; x++) {
    const first = y * columns + x;
    faces.push({ indices: [first, first + 1, first + columns + 1, first + columns] });
  }
  return { name: 'performance-grid', parentId: '0',
    transform: { space: 'local', translation: [0, 0, 0], rotationQuaternion: [0, 0, 0, 1], scale: [1, 1, 1] },
    surface: { tint: [1, 1, 1], useVertexColor: true, useTexture: true }, positions, faces };
}
async function launch(entry, directory) {
  fs.mkdirSync(directory, { recursive: true });
  const report = path.join(directory, 'typed.json');
  const trace = path.join(directory, 'trace.ndjson');
  const descriptor = path.join(directory, 'instance.json');
  const environment = { ...process.env, QT_QPA_PLATFORM: 'windows', QT_FORCE_STDERR_LOGGING: '1' };
  const child = spawn(options.probe, ['--scenario', entry.scenario, '--size', String(entry.size),
    '--samples', String(sampleCount), '--warmup', String(warmup), '--capture-samples', String(captureCount),
    '--capture-longest-edge', String(captureEdge),
    '--report', report, '--trace', trace, '--descriptor', descriptor], {
    windowsHide: false, shell: false, stdio: ['ignore', 'pipe', 'pipe'], env: environment });
  const log = fs.createWriteStream(path.join(directory, 'probe-stderr.log'), { flags: 'wx' });
  child.stderr.pipe(log);
  const exit = new Promise(resolve => child.once('exit', (code, signal) => resolve({ code, signal })));
  let text = '';
  const ready = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Owned fixture did not become ready in 90s.')), 90000);
    child.on('error', reject);
    child.once('exit', code => { clearTimeout(timeout); reject(new Error(`Owned fixture exited before ready: ${code}`)); });
    child.stdout.on('data', chunk => {
      text += chunk.toString('utf8');
      for (;;) {
        const end = text.indexOf('\n');
        if (end < 0) break;
        const line = text.slice(0, end); text = text.slice(end + 1);
        const message = JSON.parse(line);
        if (message.ready) { clearTimeout(timeout); resolve(message); }
      }
    });
  });
  try { await ready; }
  catch (error) { child.kill(); await exit; throw error; }
  const instance = JSON.parse(fs.readFileSync(descriptor, 'utf8'));
  const socket = net.connect(`\\\\.\\pipe\\${instance.pipe}`);
  await once(socket, 'connect');
  const client = new Client(socket);
  const hello = requireResponse(await client.call('bridge.hello', { mode: 'new', instanceId: instance.instanceId,
    bridgeId: instance.bridgeId, secret: instance.secret, apiVersion: '0.1.0', wireVersion: 1 }), 'bridge.hello');
  return { child, exit, client, report, trace, sessionId: hello.clientSessionId,
    sequence: BigInt(hello.nextMutationSequence), directory };
}
async function runCase(entry) {
  const directory = path.join(options.output, `${entry.scenario}-${entry.size}`);
  const owned = await launch(entry, directory);
  const typed = JSON.parse(fs.readFileSync(owned.report, 'utf8'));
  let state = typed.state;
  let entityId = typed.entityId;
  let meshId = typed.meshId;
  const measured = [];
  const observe = { scenario: entry.scenario, size: entry.size, node: process.version,
    pid: owned.child.pid, directReport: owned.report, tracePath: owned.trace, pipe: measured,
    instrumentation: typed.instrumentation,
    bridgePermissions: typed.bridgePermissions,
    limitations: ['Actual queue and server stages are available only for the instrumented executable.',
      'JSON phase excludes socket transfer and frame reassembly; GPU grab includes any paint triggered by grabFramebuffer.',
      'Instrumentation output and one-second resource telemetry add observer overhead; official results are authoritative.',
      'Memory windows use no-change mutations to exercise ledger without growing undo history.'] };
  const call = async (method, params) => {
    const item = await owned.client.call(method, params);
    requireResponse(item, method);
    return item;
  };
  const mutate = async (method, params) => {
    const item = await call(method, { ...params, document: state.document,
      expectedDocumentRevision: state.documentRevision, clientSessionId: owned.sessionId,
      mutationSequence: String(owned.sequence) });
    owned.sequence = BigInt(item.response.bridge.nextMutationSequence);
    state = item.response.result;
    return item;
  };
  const measure = async (name, invoke, count = sampleCount, prepare = async () => {}) => {
    const records = [], warmups = [];
    for (let index = 0; index < warmup + count; index++) {
      await prepare(index);
      const item = await invoke(index);
      const scalar = { id: item.id, ms: item.ms, requestBytes: item.requestBytes, responseBytes: item.responseBytes,
        jsonEncodeMs: item.jsonEncodeMs, jsonDecodeMs: item.jsonDecodeMs };
      (index < warmup ? warmups : records).push(scalar);
    }
    measured.push(summary(name, records, warmups));
  };
  const target = () => ({ document: state.document, entityId, meshId });
  try {
    observe.limits = requireResponse(await call('system.describe', {}), 'system.describe').limits;
    if (entry.scenario === 'mesh') {
      if (entry.size <= 10000) {
        const grid = createGrid(entry.size);
        await measure('mesh.create', async () => {
          const item = await mutate('mesh.create', grid);
          entityId = item.response.result.entityId;
          meshId = item.response.result.meshId;
          return item;
        }, sampleCount, async () => { await mutate('document.new', { ifDirty: 'discard' }); });
      }
      const mesh = requireResponse(await call('mesh.getSummary', target()), 'mesh.getSummary');
      if (mesh.source.vertexCount !== entry.size) throw new Error('Pipe fixture vertex count differs from typed fixture.');
      observe.pipeFixture = { source: mesh.source, evaluated: mesh.evaluated };
      for (const domain of ['vertices', 'faces'])
        await measure(`mesh.readSourcePage.${domain}256`, () => call('mesh.readSourcePage', { ...target(), domain, limit: 256 }));
      await measure('mesh.getSummary', () => call('mesh.getSummary', target()));
      const scanStarted = now();
      let cursor, total = 0, pages = 0, scanBytes = 0;
      do {
        const item = await call('mesh.readSourcePage', { ...target(), domain: 'vertices', limit: 256, ...(cursor ? { cursor } : {}) });
        total += item.response.result.vertices.length;
        pages++; scanBytes += item.responseBytes; cursor = item.response.result.nextCursor;
      } while (cursor);
      if (total !== entry.size) throw new Error('Complete source pagination count mismatch.');
      observe.fullVertexScan = { pageCount: pages, vertexCount: total, elapsedMs: elapsedMs(scanStarted), responseFrameBytes: scanBytes };
      if (entry.size <= 10000) {
        let current = requireResponse(await call('mesh.getSummary', target()), 'mesh.getSummary');
        await measure('mesh.transformComponents.vertex1', async index => {
          const item = await mutate('mesh.transformComponents', { entityId, meshId,
            expectedTopologyRevision: current.topologyRevision, expectedGeometryRevision: current.geometryRevision,
            domain: 'vertices', vertexIds: ['1'],
            transform: { space: 'local', pivot: [0, 0, 0], translation: [index % 2 ? -.001 : .001, 0, 0],
              rotationQuaternion: [0, 0, 0, 1], scale: [1, 1, 1] } });
          current = item.response.result;
          return item;
        });
      }
      const view = requireResponse(await call('viewport.getState', { document: state.document }), 'viewport.getState');
      const focused = await call('viewport.focus', { document: state.document,
        expectedDocumentRevision: state.documentRevision, expectedViewportRevision: view.viewportRevision,
        entityIds: [entityId], clientSessionId: owned.sessionId, mutationSequence: String(owned.sequence) });
      owned.sequence = BigInt(focused.response.bridge.nextMutationSequence);
      const focusedView = focused.response.result.view;
      observe.captureFixture = { framing: 'focused-current-mesh', focusMethod: 'viewport.focus -> focusEntities',
        entityIds: [entityId], view: focusedView, bridgePermissions: typed.bridgePermissions };
      await measure('viewport.capture', async index => {
        const item = await call('viewport.capture', { document: state.document,
          expectedDocumentRevision: state.documentRevision, expectedViewportRevision: focusedView.viewportRevision,
          longestEdge: captureEdge, timeoutMs: 10000 });
        const result = item.response.result;
        if (Math.max(result.outputPixelSize.width, result.outputPixelSize.height) !== captureEdge)
          throw new Error('Capture fixture did not exercise the requested actual output edge.');
        const png = Buffer.from(result.pngBase64, 'base64');
        if (crypto.createHash('sha256').update(png).digest('hex') !== result.sha256 || png.length !== result.byteLength)
          throw new Error('Actual pipe PNG hash or byte length mismatch.');
        if (index === warmup) {
          const pngPath = path.join(directory, 'pipe-capture.png');
          fs.writeFileSync(pngPath, png, { flag: 'wx' });
          observe.captureExample = { pngPath, sha256: result.sha256, byteLength: result.byteLength,
            originalPixelSize: result.originalPixelSize, outputPixelSize: result.outputPixelSize,
            view: result.view, overlayIncluded: result.overlayIncluded, capturedRegion: result.capturedRegion,
            documentRevision: result.view.documentRevision, viewportRevision: result.view.viewportRevision };
        }
        return item;
      }, captureCount);
    } else {
      for (const [pageName, afterEntityId] of [['first256', '0'], ['middle256', String(entry.size / 2)]])
        await measure(`scene.listEntities.${pageName}`, () => call('scene.listEntities', { document: state.document,
          afterEntityId, limit: 256, ...(afterEntityId !== '0' ? { expectedDocumentRevision: state.documentRevision } : {}) }));
    }
    // 实际队列压力仅查询自己的场景；不更改协议或引入业务并发线程。
    const burst = await Promise.all(Array.from({ length: 64 }, () => owned.client.call('document.current', {})));
    observe.queueBurst = { sent: burst.length, succeeded: burst.filter(item => item.response.result).length,
      rejected: burst.filter(item => item.response.error).map(item => item.response.error.data?.code ?? item.response.error.code) };
    if (observe.queueBurst.rejected.some(code => code !== 'QUEUE_FULL')) throw new Error('Unexpected burst error category.');
    observe.idleWindows = [];
    observe.continuousWindows = [];
    const idle = async phase => {
      const startEpochMs = Date.now();
      await pause(3000);
      observe.idleWindows.push({ phase, startEpochMs, endEpochMs: Date.now(), durationMs: 3000, calls: 0 });
    };
    await idle('before-continuous');
    const firstMutationSequence = owned.sequence;
    for (let window = 0; window < windowCount; window++) {
      const started = now(), startEpochMs = Date.now();
      let calls = 0, noChangeMutations = 0;
      while (elapsedMs(started) < windowSeconds * 1000) {
        if (entry.scenario === 'mesh') {
          await call('mesh.readSourcePage', { ...target(), domain: 'vertices', limit: 256 });
          calls++;
          if (entry.size <= 10000) {
            const item = await mutate('entity.update', { entityId, name: 'performance-grid' });
            if (item.response.result.status !== 'no_change') throw new Error('Sustained mutation grew history.');
            calls++; noChangeMutations++;
          }
        } else {
          await call('scene.listEntities', { document: state.document, limit: 256 });
          calls++;
        }
      }
      observe.continuousWindows.push({ window, startEpochMs, endEpochMs: Date.now(),
        requestedDurationMs: windowSeconds * 1000, measuredDurationMs: elapsedMs(started), calls, noChangeMutations });
    }
    if (entry.scenario === 'mesh' && entry.size <= 10000) {
      const status = requireResponse(await call('bridge.requestStatus', { clientSessionId: owned.sessionId,
        mutationSequence: String(firstMutationSequence) }), 'bridge.requestStatus');
      observe.ledgerAfterContinuous = { firstMutationSequence: String(firstMutationSequence),
        highWater: status.highWater, earliestResultState: status.state };
      if (owned.sequence - firstMutationSequence > 128n && status.state !== 'result_expired')
        throw new Error('Old result remained beyond configured ledger window.');
    }
    await idle('after-continuous');
    owned.client.socket.end();
    const finished = await Promise.race([owned.exit, pause(4000).then(() => ({ timeout: true }))]);
    if (finished.timeout) throw new Error('Owned probe did not exit after disconnect.');
    if (finished.code !== 0) throw new Error(`Owned probe failed on exit: ${finished.code}`);
    const traces = fs.readFileSync(owned.trace, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
    const requestTraces = new Map(traces.filter(item => item.kind === 'request').map(item => [item.id, item]));
    const phases = ['utf8BudgetJsonMs', 'mutationCanonicalMs', 'canonicalDigestMs', 'ledgerAdmissionMs',
      'ledgerCompleteMs', 'queueWaitMs', 'typedDecodeMs', 'typedDomainMs',
      'resultDtoEncodeMs', 'wireEncodeMs', 'captureFrameWaitMs', 'framebufferGrabIncludingPossiblePaintMs',
      'imageScaleMs', 'pngEncodeMs', 'captureDtoBase64EncodeMs'];
    for (const metric of measured) {
      metric.serverStages = {};
      for (const phase of phases) {
        const values = metric.samples.map(item => requestTraces.get(item.id)?.[phase]).filter(value => value !== undefined);
        if (values.length) metric.serverStages[phase] = { sampleCount: values.length, medianMs: rank(values, .5), p95Ms: rank(values, .95) };
      }
    }
    observe.memorySamples = traces.filter(item => item.kind === 'memory');
    for (const window of [...observe.idleWindows, ...observe.continuousWindows]) {
      const points = observe.memorySamples.filter(point => point.epochMs >= window.startEpochMs && point.epochMs <= window.endEpochMs);
      window.memorySampleCount = points.length;
      if (points.length >= 2) {
        const first = points[0], last = points.at(-1), durationMs = last.epochMs - first.epochMs;
        Object.assign(window, { privateBytesFirst: first.privateBytes, privateBytesLast: last.privateBytes,
          privateBytesMax: Math.max(...points.map(point => point.privateBytes)),
          workingSetBytesFirst: first.workingSetBytes, workingSetBytesLast: last.workingSetBytes,
          oneCoreCpuPercent: (last.processCpuMs - first.processCpuMs) / durationMs * 100,
          wakeupsPerSecond: (last.eventDispatcherWakeCount - first.eventDispatcherWakeCount) / durationMs * 1000,
          historyCountFirst: first.historyCount, historyCountLast: last.historyCount });
      }
    }
    const serverRequests = traces.filter(item => item.kind === 'request');
    const instrumented = typed.instrumentation === 'm7-phase-trace-v2';
    observe.resourceBounds = { telemetryMaximumQueued: Math.max(0, ...observe.memorySamples.map(item => item.queuedCount)),
      serverPrivateCountersAvailable: instrumented,
      observedMaximumQueuedAfterSend: instrumented ? Math.max(0, ...serverRequests.map(item => item.queuedCount)) : null,
      observedMaximumLedgerCachedBytes: instrumented ? Math.max(0, ...serverRequests.map(item => item.ledgerCachedBytes)) : null,
      observedMaximumSessions: instrumented ? Math.max(0, ...serverRequests.map(item => item.sessionCount)) : null };
    if (observe.resourceBounds.observedMaximumQueuedAfterSend > 16 ||
      observe.resourceBounds.observedMaximumLedgerCachedBytes > 16 * 1024 * 1024 || observe.resourceBounds.observedMaximumSessions > 4)
      throw new Error('Actual instrumented resource bounds exceeded configured limits.');
    fs.writeFileSync(path.join(directory, 'pipe.json'), JSON.stringify(observe, null, 2), { flag: 'wx' });
    console.log(JSON.stringify({ complete: `${entry.scenario}-${entry.size}`, sampleCount,
      metrics: measured.map(item => ({ method: item.method, medianMs: item.medianMs, p95Ms: item.p95Ms })) }));
    return observe;
  } finally {
    if (owned.child.exitCode === null) {
      owned.client.socket.destroy();
      owned.child.kill();
      await owned.exit;
    }
  }
}
if (!options.probe || !options.output || !Number.isInteger(sampleCount) || sampleCount < 1 || warmup < 0)
  throw new Error('Specify probe/output and valid sample counts.');
fs.mkdirSync(options.output, { recursive: true });
const results = [];
for (const entry of cases) {
  const pauseFile = path.join(options.output, 'pause.flag');
  if (fs.existsSync(pauseFile)) {
    console.log(JSON.stringify({ paused: true, pauseFile, nextCase: `${entry.scenario}-${entry.size}` }));
    await new Promise(resolve => {
      const watcher = fs.watch(options.output, () => {
        if (!fs.existsSync(pauseFile)) { watcher.close(); resolve(); }
      });
      if (!fs.existsSync(pauseFile)) { watcher.close(); resolve(); }
    });
  }
  results.push(await runCase(entry));
}
fs.writeFileSync(path.join(options.output, 'paired-results.json'), JSON.stringify({ schema: 'mini3d-api-pipe-performance-v1',
  node: process.version, cases: results }, null, 2), { flag: 'wx' });
