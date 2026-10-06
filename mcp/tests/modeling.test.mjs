/*
模块名: modeling.test
功能概述: 验证 M5-02 六方法的共享合同、单次 RPC 映射和 MCP 结果传递。
对外接口: Node test runner。
依赖关系: 官方 MCP client、已编译 adapter、共享建模样例与本机 mock bridge。
输入输出: 合同样例和业务响应夹具 -> 参数、身份、序号及错误断言。
异常与错误: 合同漂移、字段丢失或非法输入到达业务管道时使测试失败。
维护说明: 夹具不执行建模算法，不替代真实 Mini3D 几何与历史验收。
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

const examples = JSON.parse(readFileSync(new URL('../../api/schema/m5-modeling-examples.json', import.meta.url), 'utf8'));
const modelingMethods = methods.filter(method => method.schema.startsWith('m5-modeling.schema.json#'));
const names = ['mesh.makeEditable', 'mesh.transformComponents', 'mesh.bevelEdge', 'mesh.loopCut', 'mesh.deleteComponents', 'mesh.fillFace'];
const identity = { entityId: '9007199254740993', meshId: '9007199254740994',
  topologyRevision: '4', geometryRevision: '5', evaluationRevision: '6' };
const resultBase = { ...commandResult, document: examples.valid[0].value.document,
  documentRevision: '2', historyRevision: '2', created: { entityIds: [] }, ...identity };
const results = new Map([
  ['mesh.makeEditable', { ...resultBase }],
  ['mesh.transformComponents', { ...resultBase, affectedVertexIds: ['1', '2'] }],
  ['mesh.bevelEdge', { ...resultBase, bevelFaceId: '9007199254740995' }],
  ['mesh.loopCut', { ...resultBase, cutEdges: [['1', '2'], ['3', '4']] }],
  ['mesh.deleteComponents', { ...resultBase, deletedVertexIds: ['1'], deletedFaceIds: ['3'],
    deletedCornerIds: ['5', '6', '7'], deletedEdges: [['1', '2'], ['1', '3']] }],
  ['mesh.fillFace', { ...resultBase, faceId: '9007199254740995' }]
]);
const main = fileURLToPath(new URL('../dist/main.js', import.meta.url));
const cwd = fileURLToPath(new URL('..', import.meta.url));

/** 只返回合同夹具；每个调用的提交与会话元数据沿用现有 mock。 */
function modelingReply(mock) {
  return request => results.has(request.method)
    ? mock.commit(request, { result: results.get(request.method) }) : null;
}

test('M5-02 六方法共用 6 合法/24 非法样例，会话字段由 adapter 独占', () => {
  assert.deepEqual(modelingMethods.map(method => method.name), names);
  assert.deepEqual(examples.valid.map(example => example.method), names);
  assert.equal(examples.invalid.length, 24);
  for (const example of examples.valid) {
    const method = modelingMethods.find(entry => entry.name === example.method);
    assert.equal(method.kind, 'mutation');
    assert.equal(method.permission, 'scene.write');
    requireValid(resolveSchema(method.schema), example.value);
    requireValid(toolInput(method), example.value);
    assert.equal(method.schema, `m5-modeling.schema.json#/$defs/${example.definition}`);
    for (const [field, value] of [['clientSessionId', '11111111-1111-4111-8111-111111111111'], ['mutationSequence', '1']]) {
      assert.throws(() => requireValid(toolInput(method), { ...example.value, [field]: value }));
    }
  }
  for (const example of examples.invalid) {
    const method = modelingMethods.find(entry => entry.schema.endsWith(`/$defs/${example.definition}`));
    assert(method);
    assert.throws(() => requireValid(resolveSchema(method.schema), example.value));
    assert.throws(() => requireValid(toolInput(method), example.value));
  }
});

test('M5-02 flat command/mesh 输出保留源身份、算子结果与 no_change', () => {
  const businessFields = ['affectedVertexIds', 'bevelFaceId', 'cutEdges', 'deletedVertexIds',
    'deletedFaceIds', 'deletedCornerIds', 'deletedEdges', 'faceId'];
  for (const method of modelingMethods) {
    const result = results.get(method.name);
    const schema = toolOutput(method);
    requireValid(schema, result);
    for (const field of [...Object.keys(identity), ...businessFields.filter(name => Object.hasOwn(result, name))]) {
      const missing = structuredClone(result);
      delete missing[field];
      assert.throws(() => requireValid(schema, missing), `${method.name} must require ${field}`);
    }
    assert.throws(() => requireValid(schema, { command: resultBase, meshIdentity: identity }));
    assert.throws(() => requireValid(schema, { ...result, meshId: 9007199254740994 }));
    assert.equal(Object.hasOwn(result, 'command'), false);
    assert.equal(Object.hasOwn(result, 'meshIdentity'), false);
  }
  requireValid(toolOutput(modelingMethods[0]), { ...results.get('mesh.makeEditable'), status: 'no_change', undoable: false });
  requireValid(toolOutput(modelingMethods[1]), { ...results.get('mesh.transformComponents'),
    status: 'no_change', undoable: false, affectedVertexIds: [] });
});

test('M5-02 六方法只发送一条 RPC，完整保留参数、会话和单调序号', async t => {
  const mock = await new MockBridge().start();
  const client = new BridgeClient(mock.descriptor);
  mock.onRequest = modelingReply(mock);
  t.after(async () => { client.close(); await mock.close(); assert.equal(mock.failures.length, 0); });
  for (const example of examples.invalid) {
    await assert.rejects(client.call(`mesh.${example.definition}`, example.value));
  }
  assert.equal(mock.requests.length, 0, 'invalid samples do not connect or reach the business pipe');
  for (const [index, example] of examples.valid.entries()) {
    const response = await client.call(example.method, example.value);
    assert.deepEqual(response.result, results.get(example.method));
    const request = mock.requests.at(-1);
    assert.equal(request.method, example.method);
    assert.deepEqual(request.params, { ...example.value, clientSessionId: mock.session, mutationSequence: String(index + 1) });
    assert.equal(response.bridge.mutationSequence, String(index + 1));
    assert.equal(response.bridge.clientSessionId, mock.session);
  }
  assert.deepEqual(mock.requests.filter(request => request.method !== 'bridge.hello').map(request => request.method), names);
});

for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
  test(`M5-02 官方 ${era} stdio：六方法、非法输入隔离与业务错误原样传递`, async t => {
    const mock = await new MockBridge().start();
    mock.onRequest = modelingReply(mock);
    const filename = await mock.descriptorFile();
    const transport = new StdioClientTransport({ command: process.execPath, args: [main, '--descriptor', filename], cwd,
      env: { ...process.env }, stderr: 'pipe' });
    const client = new Client({ name: 'mini3d-modeling-mcp-test', version: '0.1.0' }, {
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
    const listed = await client.listTools();
    for (const method of modelingMethods) {
      const tool = listed.tools.find(entry => entry.name === toolName(method.name));
      assert(tool);
      assert.deepEqual(tool.inputSchema, toolInput(method));
      assert.deepEqual(tool.outputSchema, toolOutput(method));
      assert.equal(tool.annotations.readOnlyHint, false);
    }
    assert.equal(mock.requests.length, 0, 'static contracts do not imply a connected runtime capability');
    for (const example of examples.invalid) {
      const response = await client.callTool({ name: toolName(`mesh.${example.definition}`), arguments: example.value });
      assert.equal(response.isError, true);
    }
    for (const [field, value] of [['clientSessionId', '11111111-1111-4111-8111-111111111111'], ['mutationSequence', '1']]) {
      const response = await client.callTool({ name: toolName('mesh.makeEditable'), arguments: { ...examples.valid[0].value, [field]: value } });
      assert.equal(response.isError, true);
    }
    assert.equal(mock.requests.length, 0, 'SDK rejects invalid input before the bridge');
    for (const [index, example] of examples.valid.entries()) {
      const response = await client.callTool({ name: toolName(example.method), arguments: example.value });
      assert.notEqual(response.isError, true);
      assert.deepEqual(response.structuredContent, results.get(example.method));
      const request = mock.requests.at(-1);
      assert.equal(request.method, example.method);
      assert.deepEqual(request.params, { ...example.value, clientSessionId: mock.session, mutationSequence: String(index + 1) });
      const adapter = response.content.filter(item => item.type === 'text').map(item => {
        try { return JSON.parse(item.text); } catch { return null; }
      }).find(item => item?.source === 'adapter');
      assert.equal(adapter.bridge.mutationSequence, String(index + 1));
      assert.equal(Object.hasOwn(response.structuredContent, 'bridge'), false);
    }
    const duplicateEdges = { ...examples.valid[1].value, domain: 'edges', edges: [['2', '1'], ['1', '2']] };
    delete duplicateEdges.vertexIds;
    requireValid(toolInput(modelingMethods[1]), duplicateEdges);
    const error = { code: 'INVALID_ARGUMENT', message: '源边目标重复', fieldPath: 'edges', recovery: 'correct_input',
      document: resultBase.document, documentRevision: '2', historyRevision: '2' };
    mock.onRequest = request => request.method === 'mesh.transformComponents'
      ? mock.commit(request, { error: { code: -32010, message: error.message, data: error } }) : null;
    const response = await client.callTool({ name: toolName('mesh.transformComponents'), arguments: duplicateEdges });
    assert.equal(response.isError, true);
    assert.deepEqual(response.structuredContent.error, error);
    assert.equal(response.structuredContent.bridge.highWater, String(examples.valid.length + 1));
    const contentError = JSON.parse(response.content.find(item => item.type === 'text').text);
    assert.deepEqual(contentError.error, error);
    assert.equal(contentError.rpcCode, -32010);
    assert.deepEqual(mock.requests.at(-1).params.edges, duplicateEdges.edges, 'adapter neither normalizes nor deduplicates source edges');
    assert.equal(mock.requests.at(-1).params.transform.rotationQuaternion[3], 2, 'quaternion normalization belongs to the application');
    assert.deepEqual(mock.requests.filter(request => request.method !== 'bridge.hello').map(request => request.method),
      [...names, 'mesh.transformComponents']);
  });
}
