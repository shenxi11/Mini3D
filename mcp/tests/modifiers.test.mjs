/*
模块名: modifiers.test
功能概述: 验证 M5-03 三工具共享合同、完整修改器状态与单次 RPC 映射。
对外接口: Node test runner。
依赖关系: 官方 MCP client、已编译 adapter、共享修改器样例和本机 mock bridge。
输入输出: 合同样例及完整结果夹具 -> 参数、身份、状态、会话和领域错误断言。
异常与错误: 合同漂移、非法输入连接业务管道或结果字段丢失时使测试失败。
维护说明: 夹具不执行修改器求值，不替代真实 Mini3D 几何、历史与 Apply 验收。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { BridgeClient } from '../dist/bridge.js';
import { methods, resolveSchema, toolInput, toolOutput, requireValid, validator } from '../dist/schema.js';
import { toolName } from '../dist/tools.js';
import { MockBridge, commandResult } from './mock-bridge.mjs';

const examples = JSON.parse(readFileSync(new URL('../../api/schema/m5-modifiers-examples.json', import.meta.url), 'utf8'));
const modifierMethods = methods.filter(method => method.schema.startsWith('m5-modifiers.schema.json#'));
const names = ['modifier.setMirror', 'modifier.setSubdivision', 'modifier.apply'];
const identity = { entityId: '9007199254740993', meshId: '9007199254740994',
  topologyRevision: '2', geometryRevision: '3', evaluationRevision: '4' };
const resultBase = { ...commandResult, document: examples.valid[0].value.document,
  documentRevision: '2', historyRevision: '2', created: { entityIds: [] }, ...identity };
const mirror = examples.valid[0].value.options;
const subdivision = examples.valid[1].value.options;
const states = [
  { mirror, subdivision: null }, { mirror, subdivision }, { mirror: null, subdivision },
  { mirror: null, subdivision }, { mirror: null, subdivision: null }, { mirror: null, subdivision: null },
  { mirror: examples.valid[6].value.options, subdivision: null },
  { mirror: null, subdivision: examples.valid[7].value.options },
  { mirror: examples.valid[8].value.options, subdivision: null }
].map(state => ({ order: ['mirror', 'subdivision'], ...state }));
const results = states.map((modifiers, index) => ({ ...resultBase, modifiers,
  requeryRequired: examples.valid[index].method === 'modifier.apply' }));
const futureResult = { ...results[1], status: 'no_change', undoable: false, futureResultField: '输出扩展',
  modifiers: { ...states[1], futureStateField: true, mirror: { ...mirror, futureMirrorField: '保留' },
    subdivision: { ...subdivision, futureSubdivisionField: true } } };
const successCases = [
  ...examples.valid.map((example, index) => ({ ...example, result: results[index] })),
  { ...examples.valid[0], result: futureResult },
  { ...examples.valid[1], result: { ...results[1], status: 'no_change', undoable: false } }
];
const errorCases = [
  { ...examples.valid[0], error: { code: 'REVISION_CONFLICT', message: '源网格版本已变化',
    fieldPath: 'expectedGeometryRevision', recovery: 'refetch' } },
  { ...examples.valid[1], error: { code: 'INVALID_ARGUMENT', message: '当前几何不满足求值要求',
    fieldPath: 'options', recovery: 'correct_input' } },
  { ...examples.valid[2], error: { code: 'UNSUPPORTED_OPERATION', message: '不存在待应用的修改器',
    fieldPath: 'modifier', recovery: 'correct_input' } }
].map(example => ({ ...example, error: { ...example.error, document: resultBase.document,
  documentRevision: resultBase.documentRevision, historyRevision: resultBase.historyRevision } }));
const invalidCases = [
  ...examples.invalid.map(example => ({ ...example, method: `modifier.${example.definition}` })),
  ...examples.valid.slice(0, 3).flatMap(example => [
    { ...example, value: { ...example.value, clientSessionId: '11111111-1111-4111-8111-111111111111' } },
    { ...example, value: { ...example.value, mutationSequence: '1' } },
    { ...example, value: { ...example.value, futureInputField: true } },
    { ...example, value: { ...example.value, document: { ...example.value.document, futureInputField: true } } }
  ])
];
const summary = { document: resultBase.document, documentRevision: '2', historyRevision: '2', ...identity,
  source: { vertexCount: 8, faceCount: 6, cornerCount: 24, edgeCount: 12, boundaryEdgeCount: 0, bounds: null },
  evaluated: { vertexCount: 8, faceCount: 6, triangleCount: 12, bounds: null }, modifiers: futureResult.modifiers };
const summaryInput = { document: resultBase.document, entityId: identity.entityId, meshId: identity.meshId };
const main = fileURLToPath(new URL('../dist/main.js', import.meta.url));
const cwd = fileURLToPath(new URL('..', import.meta.url));

/** 按测试预置顺序返回夹具；不按输入计算几何或模拟修改器状态机。 */
function modifierReply(mock) {
  const replies = [...successCases, ...errorCases];
  let index = 0;
  return request => {
    if (request.method === 'mesh.getSummary') return { result: summary };
    if (!names.includes(request.method)) return null;
    const example = replies[index++];
    assert(example, 'each expected modifier call has exactly one reply');
    assert.equal(request.method, example.method);
    return mock.commit(request, example.error
      ? { error: { code: -32010, message: example.error.message, data: example.error } }
      : { result: example.result });
  };
}

/** 成功结果的 adapter 元数据仅从独立文本读取，领域输出保持 flat。 */
function adapterMetadata(response) {
  return response.content.filter(item => item.type === 'text').map(item => {
    try { return JSON.parse(item.text); } catch { return null; }
  }).find(item => item?.source === 'adapter')?.bridge;
}

test('M5-03 三工具共用 9 合法/18 非法样例，输入严格且会话字段由 adapter 独占', () => {
  assert.deepEqual(modifierMethods.map(method => method.name), names);
  assert.deepEqual(names.map(toolName), ['mini3d_modifier_set_mirror', 'mini3d_modifier_set_subdivision', 'mini3d_modifier_apply']);
  assert.equal(examples.valid.length, 9);
  assert.equal(examples.invalid.length, 18);
  for (const method of modifierMethods) {
    assert.equal(method.kind, 'mutation');
    assert.equal(method.permission, 'scene.write');
    assert.equal(method.resultSchema, 'm5-modifiers.schema.json#/$defs/modifierCommand');
  }
  for (const example of examples.valid) {
    const method = modifierMethods.find(entry => entry.name === example.method);
    assert.equal(method.schema, `m5-modifiers.schema.json#/$defs/${example.definition}`);
    requireValid(resolveSchema(method.schema), example.value);
    requireValid(toolInput(method), example.value);
  }
  for (const example of examples.invalid) {
    const method = modifierMethods.find(entry => entry.name === `modifier.${example.definition}`);
    assert.throws(() => requireValid(resolveSchema(method.schema), example.value));
  }
  for (const example of invalidCases) {
    const method = modifierMethods.find(entry => entry.name === example.method);
    assert.throws(() => requireValid(toolInput(method), example.value));
  }
  assert.equal(examples.valid[3].value.options, null);
  assert.equal(examples.valid[4].value.options, null);
  assert.deepEqual(examples.valid[6].value.options, { ...mirror, enabled: false, threshold: 0 });
  assert.deepEqual(examples.valid[7].value.options, { enabled: false, levels: 1 });
  assert.equal(examples.valid[8].value.options.threshold, 1e300);
  for (const axis of ['x', 'y', 'z']) {
    requireValid(toolInput(modifierMethods[0]), { ...examples.valid[0].value,
      options: { ...mirror, axis, merge: false, clipping: true } });
  }
  for (const threshold of [NaN, Infinity, -Infinity]) {
    assert.throws(() => requireValid(toolInput(modifierMethods[0]), { ...examples.valid[0].value,
      options: { ...mirror, enabled: false, threshold } }));
  }
});

test('M5-03 flat 输出必须包含 command、源身份、完整固定状态和 requeryRequired', () => {
  const required = ['document', 'documentRevision', 'historyRevision', 'status', 'created',
    'affectedEntityIds', 'undoable', 'selectionChanged', ...Object.keys(identity), 'modifiers', 'requeryRequired'];
  for (const method of modifierMethods) {
    const schema = toolOutput(method);
    for (const result of results) requireValid(schema, result);
    requireValid(schema, futureResult);
    for (const field of required) {
      const missing = structuredClone(results[1]);
      delete missing[field];
      assert.throws(() => requireValid(schema, missing), `${method.name} requires ${field}`);
    }
    for (const field of ['order', 'mirror', 'subdivision']) {
      const missing = structuredClone(results[1]);
      delete missing.modifiers[field];
      assert.throws(() => requireValid(schema, missing));
    }
    for (const [kind, fields] of [['mirror', Object.keys(mirror)], ['subdivision', Object.keys(subdivision)]]) {
      for (const field of fields) {
        const missing = structuredClone(results[1]);
        delete missing.modifiers[kind][field];
        assert.throws(() => requireValid(schema, missing));
      }
    }
    assert.throws(() => requireValid(schema, { command: resultBase, meshIdentity: identity,
      modifiers: states[1], requeryRequired: false }));
    assert.throws(() => requireValid(schema, { ...results[1], meshId: 9007199254740994 }));
    assert.throws(() => requireValid(schema, { ...results[1], requeryRequired: 'true' }));
    assert.throws(() => requireValid(schema, { ...results[1], modifiers: { ...states[1], order: ['subdivision', 'mirror'] } }));
  }
  assert.equal(results[2].requeryRequired, true);
  assert.equal(results[5].requeryRequired, true);
  assert.deepEqual(results[2].modifiers, { order: ['mirror', 'subdivision'], mirror: null, subdivision });
  assert.deepEqual(results[5].modifiers, { order: ['mirror', 'subdivision'], mirror: null, subdivision: null });
  assert.equal(results[2].meshId, examples.valid[2].value.meshId, 'even coincident numeric IDs require an apply requery');
});

test('M5-03 mesh.getSummary 也必须返回完整状态，输出扩展字段不被删除', () => {
  const schema = toolOutput(methods.find(method => method.name === 'mesh.getSummary'));
  for (const modifiers of states) requireValid(schema, { ...summary, modifiers });
  const unchanged = structuredClone(summary);
  requireValid(schema, summary);
  assert.deepEqual(summary, unchanged);
  const missing = structuredClone(summary);
  delete missing.modifiers;
  assert.throws(() => requireValid(schema, missing));
  for (const field of ['order', 'mirror', 'subdivision']) {
    const partial = structuredClone(summary);
    delete partial.modifiers[field];
    assert.throws(() => requireValid(schema, partial));
  }
  assert.throws(() => requireValid(schema, { ...summary, modifiers: { ...summary.modifiers, order: ['mirror'] } }));
});

test('M5-03 非法输入不连接桥；成功/no_change/领域错误各一条 RPC、原会话与 1..n 序号', async t => {
  const mock = await new MockBridge().start();
  const client = new BridgeClient(mock.descriptor);
  mock.onRequest = modifierReply(mock);
  t.after(async () => { client.close(); await mock.close(); assert.equal(mock.failures.length, 0); });
  for (const example of invalidCases) await assert.rejects(client.call(example.method, example.value));
  assert.equal(mock.requests.length, 0);
  assert.equal(mock.sockets.size, 0, 'invalid input cannot open the private bridge connection');
  for (const [index, example] of successCases.entries()) {
    const original = structuredClone(example.value);
    const response = await client.call(example.method, example.value);
    assert.deepEqual(response.result, example.result);
    assert.deepEqual(example.value, original);
    assert.deepEqual(mock.requests.at(-1).params, { ...example.value,
      clientSessionId: mock.session, mutationSequence: String(index + 1) });
    assert.deepEqual(response.bridge, mock.bridge(String(index + 1)));
  }
  assert.deepEqual((await client.call('mesh.getSummary', summaryInput)).result, summary);
  assert.deepEqual(mock.requests.at(-1).params, summaryInput);
  for (const [index, example] of errorCases.entries()) {
    const sequence = String(successCases.length + index + 1);
    await assert.rejects(client.call(example.method, example.value), error => {
      assert.deepEqual(error.details, example.error);
      assert.equal(error.rpcCode, -32010);
      assert.deepEqual(error.bridge, mock.bridge(sequence));
      return true;
    });
    assert.deepEqual(mock.requests.at(-1).params, { ...example.value,
      clientSessionId: mock.session, mutationSequence: sequence });
  }
  assert.deepEqual(mock.requests.filter(request => request.method !== 'bridge.hello').map(request => request.method),
    [...successCases.map(example => example.method), 'mesh.getSummary', ...errorCases.map(example => example.method)]);
  assert.equal(mock.requests.filter(request => request.method === 'bridge.hello').length, 1);
});

for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
  test(`M5-03 官方 ${era} stdio：三工具、完整状态、非法输入隔离和业务结果原样传递`, async t => {
    const mock = await new MockBridge().start();
    mock.onRequest = modifierReply(mock);
    const filename = await mock.descriptorFile();
    const transport = new StdioClientTransport({ command: process.execPath, args: [main, '--descriptor', filename], cwd,
      env: { ...process.env }, stderr: 'pipe' });
    const client = new Client({ name: 'mini3d-modifiers-mcp-test', version: '0.1.0' }, {
      jsonSchemaValidator: validator, versionNegotiation: { mode, probe: { timeoutMs: 5000, maxRetries: 0 } }
    });
    const protocolErrors = [];
    client.onerror = error => protocolErrors.push(error);
    t.after(async () => {
      await client.close(); await transport.close(); await mock.close();
      assert.equal(mock.failures.length, 0);
      assert.equal(protocolErrors.length, 0);
    });
    await client.connect(transport, { timeout: 5000 });
    assert.equal(client.getProtocolEra(), era);
    if (era === 'modern') assert.equal(client.getNegotiatedProtocolVersion(), '2026-07-28');
    const listed = await client.listTools();
    for (const method of modifierMethods) {
      const tool = listed.tools.find(entry => entry.name === toolName(method.name));
      assert(tool);
      assert.deepEqual(tool.inputSchema, toolInput(method));
      assert.deepEqual(tool.outputSchema, toolOutput(method));
      assert.equal(tool.annotations.readOnlyHint, false);
    }
    assert.equal(mock.requests.length, 0, 'listing static tool contracts cannot connect the runtime');
    for (const example of invalidCases) {
      const rejected = await client.callTool({ name: toolName(example.method), arguments: example.value });
      assert.equal(rejected.isError, true);
    }
    assert.equal(mock.requests.length, 0);
    assert.equal(mock.sockets.size, 0, 'invalid tools cannot connect the private bridge');
    for (const [index, example] of successCases.entries()) {
      const response = await client.callTool({ name: toolName(example.method), arguments: example.value });
      const sequence = String(index + 1);
      assert.notEqual(response.isError, true);
      assert.deepEqual(response.structuredContent, example.result);
      assert.deepEqual(mock.requests.at(-1).params, { ...example.value,
        clientSessionId: mock.session, mutationSequence: sequence });
      assert.deepEqual(adapterMetadata(response), mock.bridge(sequence));
      for (const wrapper of ['command', 'meshIdentity', 'bridge']) assert.equal(Object.hasOwn(response.structuredContent, wrapper), false);
    }
    const queried = await client.callTool({ name: toolName('mesh.getSummary'), arguments: summaryInput });
    assert.notEqual(queried.isError, true);
    assert.deepEqual(queried.structuredContent, summary);
    assert.deepEqual(mock.requests.at(-1).params, summaryInput);
    for (const [index, example] of errorCases.entries()) {
      const response = await client.callTool({ name: toolName(example.method), arguments: example.value });
      const sequence = String(successCases.length + index + 1);
      assert.equal(response.isError, true);
      assert.deepEqual(response.structuredContent.error, example.error);
      assert.deepEqual(response.structuredContent.bridge, mock.bridge(sequence));
      const content = JSON.parse(response.content.find(item => item.type === 'text').text);
      assert.deepEqual(content.error, example.error);
      assert.deepEqual(content.bridge, mock.bridge(sequence));
      assert.equal(content.rpcCode, -32010);
      assert.deepEqual(mock.requests.at(-1).params, { ...example.value,
        clientSessionId: mock.session, mutationSequence: sequence });
    }
    assert.deepEqual(mock.requests.filter(request => request.method !== 'bridge.hello').map(request => request.method),
      [...successCases.map(example => example.method), 'mesh.getSummary', ...errorCases.map(example => example.method)]);
    assert.equal(mock.requests.filter(request => request.method === 'bridge.hello').length, 1);
  });
}
