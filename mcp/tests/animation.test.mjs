/*
模块名: animation.test
功能概述: 使用共享动画合同和官方SDK验证14工具、CAS信封及结构化诊断。
对外接口: Node test runner。
依赖关系: 已编译适配器、官方MCP客户端、本机独有mock管道。
输入输出: 显式动画请求到无损参数、领域结果与账本元数据断言。
异常与错误: 无效输入不得到达桥；每例关闭自有进程和管道。
维护说明: mock证明协议合同，不代替本体数学、状态和真实PNG验收。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { apiVersion, limits, methods, toolInput, toolOutput, requireValid, validator } from '../dist/schema.js';
import { toolName } from '../dist/tools.js';
import { MockBridge, document, state, transform, commandResult } from './mock-bridge.mjs';

const query = { document, expectedDocumentRevision: '1' };
const write = { ...query };
const control = { ...write, expectedSessionRevision: '1' };
const settings = { fps: 24, startFrame: 1, endFrame: 49 };
const target = { entityId: '9007199254740993', channel: 'rotationEulerXYZDegrees' };
const key = { frame: 49, value: [0, 720.000000001, 0], interpolation: 'linear' };
const animationState = { ...state, settings, mode: 'preview_paused', frame: 25, loop: false,
  sessionRevision: '1', evaluationId: '9007199254740993' };
const controlResult = { ...state, status: 'committed', mode: 'preview_paused', frame: 25, loop: false,
  sessionRevision: '2', evaluationId: '9007199254740994' };
const matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const inputs = new Map([
  ['animation.getState', query],
  ['animation.listTracks', { ...query, entityId: target.entityId, limit: 1 }],
  ['animation.readKeyframes', { ...query, ...target, limit: 1 }],
  ['animation.sample', { ...query, frame: 25.5, entityIds: [target.entityId] }],
  ['animation.setSettings', { ...write, settings }],
  ['animation.upsertKeyframes', { ...write, items: [{ ...target, ...key }], onConflict: 'replace' }],
  ['animation.deleteKeyframes', { ...write, items: [{ ...target, frame: 49 }] }],
  ['animation.removeTrack', { ...write, ...target }],
  ['animation.moveKeyframe', { ...write, ...target, fromFrame: 49, toFrame: 48, onConflict: 'reject' }],
  ['animation.setPreview', { ...control, enabled: true }],
  ['animation.setFrame', { ...control, frame: 25.5 }],
  ['animation.play', control],
  ['animation.pause', control],
  ['animation.setLoop', { ...control, enabled: true }]
]);
const selected = methods.filter(method => method.name.startsWith('animation.'));

function resultFor(name) {
  if (name === 'animation.getState') return animationState;
  if (name === 'animation.listTracks') return { ...state, tracks: [{ ...target, keyframeCount: 2 }], nextAfterTrack: null };
  if (name === 'animation.readKeyframes') return { ...state, ...target, keyframes: [key], nextAfterFrame: null };
  if (name === 'animation.sample') return { ...state, frame: 25.5, entities: [{ entityId: target.entityId,
    localTransform: transform, worldMatrix: matrix, rotationSource: 'animationTrack', rotationEulerXYZDegrees: [0, 367.5, 0] }] };
  if (name === 'animation.pause') return { ...controlResult, mode: 'base', diagnostic: {
    ...state, code: 'UNSUPPORTED_TRANSFORM', message: '停止后包装失败，已退出预览', recovery: 'none' } };
  if (selected.find(method => method.name === name)?.permission === 'viewport.control') return controlResult;
  return commandResult;
}

test('动画14方法/预算与严格CAS合同；raw double和成功诊断无损', () => {
  assert.equal(apiVersion, '0.2.0');
  assert.equal(methods.length, 61);
  assert.equal(selected.length, 14);
  assert.equal(limits.animationTracks, 3000);
  assert.equal(limits.animationBatchItems, 1024);
  assert.equal(limits.animationSampleEntities, 256);
  for (const method of selected) {
    const input = inputs.get(method.name);
    requireValid(toolInput(method), input);
    requireValid(toolOutput(method), resultFor(method.name));
    assert.throws(() => requireValid(toolInput(method), { ...input, unknown: true }));
    for (const field of ['clientSessionId', 'mutationSequence'])
      assert.throws(() => requireValid(toolInput(method), { ...input, [field]: '1' }));
    if (method.kind !== 'query') {
      const missingContentCas = { ...input };
      delete missingContentCas.expectedDocumentRevision;
      assert.throws(() => requireValid(toolInput(method), missingContentCas));
    }
    if (method.permission === 'viewport.control') {
      const missingSessionCas = { ...input };
      delete missingSessionCas.expectedSessionRevision;
      assert.throws(() => requireValid(toolInput(method), missingSessionCas));
    }
  }
  const upsert = toolInput(selected.find(method => method.name === 'animation.upsertKeyframes'));
  const original = inputs.get('animation.upsertKeyframes');
  requireValid(upsert, original);
  assert.equal(original.items[0].value[1], 720.000000001);
  assert.throws(() => requireValid(upsert, { ...original, items: Array.from({ length: 1025 }, () => original.items[0]) }));
  const sample = toolInput(selected.find(method => method.name === 'animation.sample'));
  for (const frame of [NaN, Infinity, 0, 100001])
    assert.throws(() => requireValid(sample, { ...inputs.get('animation.sample'), frame }));
});

const main = fileURLToPath(new URL('../dist/main.js', import.meta.url));
const cwd = fileURLToPath(new URL('..', import.meta.url));
for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
  test(`动画官方SDK ${era}：14请求逐项映射，双CAS和暂停诊断保留`, async t => {
    const mock = await new MockBridge().start();
    mock.onRequest = request => {
      const method = selected.find(item => item.name === request.method);
      if (!method) return null;
      const fragment = { result: resultFor(method.name) };
      return method.kind === 'query' ? fragment : mock.commit(request, fragment);
    };
    const descriptor = await mock.descriptorFile();
    const transport = new StdioClientTransport({ command: process.execPath, args: [main, '--descriptor', descriptor],
      cwd, env: { ...process.env }, stderr: 'pipe' });
    const client = new Client({ name: 'mini3d-animation-contract-test', version: '0.2.0' }, {
      jsonSchemaValidator: validator, versionNegotiation: { mode, probe: { timeoutMs: 5000, maxRetries: 0 } }
    });
    const errors = [];
    client.onerror = error => errors.push(String(error));
    t.after(async () => {
      await client.close(); await transport.close(); await mock.close();
      assert.equal(mock.failures.length, 0); assert.equal(errors.length, 0);
    });
    await client.connect(transport, { timeout: 5000 });
    assert.equal(client.getProtocolEra(), era);
    const listed = await client.listTools();
    assert.equal(listed.tools.length, 61);
    for (const method of selected) {
      const invalid = { ...inputs.get(method.name), unknown: true };
      assert.equal((await client.callTool({ name: toolName(method.name), arguments: invalid })).isError, true);
    }
    assert.equal(mock.requests.length, 0);
    let sequence = 0;
    for (const method of selected) {
      const input = inputs.get(method.name);
      const before = structuredClone(input);
      const response = await client.callTool({ name: toolName(method.name), arguments: input });
      assert.notEqual(response.isError, true);
      assert.deepEqual(response.structuredContent, resultFor(method.name));
      assert.deepEqual(input, before);
      const expected = method.kind === 'query' ? input : {
        ...input, clientSessionId: mock.session, mutationSequence: String(++sequence)
      };
      assert.deepEqual(mock.requests.at(-1).params, expected);
      if (method.name === 'animation.pause') {
        assert.equal(response.structuredContent.mode, 'base');
        assert.equal(response.structuredContent.diagnostic.code, 'UNSUPPORTED_TRANSFORM');
      }
    }
    assert.equal(sequence, 10);
    assert.equal(mock.highWater, '10');
    assert.equal(mock.requests[0].params.apiVersion, '0.2.0');
  });
}
