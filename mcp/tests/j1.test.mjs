/*
模块名: j1.test
功能概述: 验证11个J1工具的公共合同、binary64、规范身份和两代官方SDK桥映射。
对外接口: Node test runner。
依赖关系: 已编译适配器、共享schema、官方MCP客户端、独占本机mock管道。
输入输出: 材料/Sun/typed请求到无损RPC、领域结果、Unsupported及CAS诊断。
异常与错误: 非法合同输入不得连接桥；每例关闭自己的客户端、进程和管道。
维护说明: mock只验证边界和传输，不代替Core取样、真实时钟或作者历史验收。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { BridgeClient } from '../dist/bridge.js';
import { apiVersion, wireVersion, limits, methods, resolveSchema, toolInput, toolOutput, requireValid, validator } from '../dist/schema.js';
import { toolName } from '../dist/tools.js';
import { MockBridge, document, documentUuid, state, transform, surface, commandResult } from './mock-bridge.mjs';

const names = [
  'material.create', 'material.get', 'material.update', 'entity.assignMaterial', 'light.createSun', 'light.updateSun',
  'animation.upsertPropertyKeyframe', 'animation.deletePropertyKeyframe', 'animation.readPropertyKeyframes',
  'animation.sampleProperties', 'animation.setRationalSettings'
];
const selected = methods.filter(method => names.includes(method.name));
const entityId = '9007199254740993', materialId = '9007199254740993', materialSlotId = '9007199254740994';
const curveIdentity = { actionId: '9007199254741993', actionSlotId: '9007199254741994', curveId: '9007199254741995' };
const keyIdentity = { ...curveIdentity, keyId: '9007199254741996' };
const parameters = { baseColor: [0.1234567890123456, 0.8765432109876543, 0.5],
  roughness: 0.23456789012345678, metallic: 0.3456789012345678, coverage: 0.9999999999999999 };
const sun = { irradiance: 1.2345678901234567, color: [0.1234567890123456, 0.5, 0.8765432109876543] };
const address = { documentUuid, owner: { kind: 'entity', entityId }, property: 'transform.position', component: 'x' };
const roughnessAddress = { documentUuid, owner: { kind: 'material', materialId }, property: 'material.pbr.roughness', component: 'scalar' };
const sunAddress = { documentUuid, owner: { kind: 'entity', entityId }, property: 'light.sun.irradiance', component: 'scalar' };
const addresses = [address, roughnessAddress, sunAddress];
const time = 12.500000000000002, value = 1.0000000000000002;
const query = { document, expectedDocumentRevision: '1' }, write = { ...query };
const settings = { rationalFps: { numerator: 4294967295, denominator: 4294967294 },
  startFrame: 1, endFrame: 100000, loopPolicy: 'inclusiveFrameStep' };
const authorCommand = { ...state, status: 'committed', created: { entityIds: [] }, affectedEntityIds: [],
  undoable: true, selectionChanged: false };
const noChange = { ...authorCommand, status: 'no_change', undoable: false };
const material = { id: materialId, definitionVersion: '18446744073709551615',
  model: 'pbrMetallicRoughness', parameters, textures: [] };
const scalarKey = { id: keyIdentity.keyId, time, value, outgoing: 'linear' };
const page = { ...state, address, curveIdentity, keys: [scalarKey], nextAfterTime: time };
const inputs = new Map([
  ['material.create', { ...write, parameters }],
  ['material.get', { document, materialId }],
  ['material.update', { ...write, materialId, parameters }],
  ['entity.assignMaterial', { ...write, entityId, materialId }],
  ['light.createSun', { ...write, name: '作者Sun', parentId: '0', transform, visible: true, sun }],
  ['light.updateSun', { ...write, entityId, sun }],
  ['animation.upsertPropertyKeyframe', { ...write, address, time, value, outgoing: 'linear' }],
  ['animation.deletePropertyKeyframe', { ...write, address, time, linkedEdit: 'rejectShared' }],
  ['animation.readPropertyKeyframes', { ...query, address, limit: 1 }],
  ['animation.sampleProperties', { ...query, frame: 12.5, addresses }],
  ['animation.setRationalSettings', { ...write, settings }]
]);
const outputs = new Map([
  ['material.create', { command: authorCommand, materialId }],
  ['material.get', { ...state, material }],
  ['material.update', { command: authorCommand, materialId }],
  ['entity.assignMaterial', { command: { ...authorCommand, affectedEntityIds: [entityId] }, entityId, materialSlotId, materialId }],
  ['light.createSun', commandResult],
  ['light.updateSun', { ...authorCommand, affectedEntityIds: [entityId] }],
  ['animation.upsertPropertyKeyframe', { command: authorCommand, keyIdentity }],
  ['animation.deletePropertyKeyframe', { command: authorCommand, keyIdentity }],
  ['animation.readPropertyKeyframes', page],
  ['animation.sampleProperties', sampleAt(12.5, [1, 0.5, 1.5])],
  ['animation.setRationalSettings', authorCommand]
]);

/** 按输入顺序构造合同取样夹具；不执行数学求值或安装状态。 */
function sampleAt(frame, values) {
  return { ...state, frame, samples: addresses.map((item, index) => ({ address: item, value: values[index] })) };
}
/** 返回实际权威目录的方法，缺失方法使合同测试明确失败。 */
function methodFor(name) {
  const method = methods.find(item => item.name === name);
  assert(method, name);
  return method;
}
/** 复制后删除字段，用于验证必需项而不修改共享夹具。 */
function without(object, field) {
  const copy = structuredClone(object);
  delete copy[field];
  return copy;
}
const upsert = inputs.get('animation.upsertPropertyKeyframe');
const read = inputs.get('animation.readPropertyKeyframes');
const sample = inputs.get('animation.sampleProperties');
const rational = inputs.get('animation.setRationalSettings');
const invalid = selected.flatMap(method => {
  const input = inputs.get(method.name);
  return [
    { method: method.name, input: { ...input, unknown: true } },
    { method: method.name, input: { ...input, clientSessionId: document.instanceId } },
    { method: method.name, input: { ...input, mutationSequence: '1' } },
    ...(method.kind === 'query' ? [] : [{ method: method.name, input: without(input, 'expectedDocumentRevision') }])
  ];
});
invalid.push(
  { method: 'material.create', input: { ...inputs.get('material.create'), parameters: { ...parameters, textures: [] } } },
  { method: 'material.create', input: { ...inputs.get('material.create'), parameters: without(parameters, 'coverage') } },
  { method: 'light.updateSun', input: { ...inputs.get('light.updateSun'), sun: { ...sun, intensity: 1 } } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, keyId: keyIdentity.keyId } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, address: { ...address, owner: { ...address.owner, materialId } } } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, address: { ...address, property: 'transform.unknown' } } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, address: { ...roughnessAddress, component: 'x' } } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, address: { ...address, component: 'scalar' } } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, outgoing: 'cubic' } },
  { method: 'animation.deletePropertyKeyframe', input: { ...inputs.get('animation.deletePropertyKeyframe'), linkedEdit: 'replace' } },
  { method: 'animation.readPropertyKeyframes', input: { ...without(read, 'expectedDocumentRevision'), afterTime: time } },
  { method: 'animation.sampleProperties', input: { ...sample, addresses: [address, address] } },
  { method: 'animation.setRationalSettings', input: { ...rational, settings: { ...settings, rationalFps: null } } }
);

test('J1目录精确增加11方法，API0.3/wire1；输入CAS/会话与nested command/flat state保持', () => {
  const catalog = JSON.parse(readFileSync(new URL('../../api/schema/methods.json', import.meta.url), 'utf8'));
  assert.equal(apiVersion, '0.3.0'); assert.equal(wireVersion, 1); assert.equal(methods.length, 72);
  assert.deepEqual(methods, catalog.methods);
  assert.deepEqual(selected.map(method => method.name), names);
  assert.deepEqual(catalog.methods.filter(method => method.stage === 'J1'), selected);
  assert.equal(new Set(methods.map(method => toolName(method.name))).size, 72);
  assert.deepEqual(selected.map(method => method.kind), [
    'mutation', 'query', 'mutation', 'mutation', 'mutation', 'mutation', 'mutation', 'mutation', 'query', 'query', 'mutation'
  ]);
  for (const method of selected) {
    assert.equal(method.permission, method.kind === 'query' ? 'scene.read' : 'scene.write');
    const input = inputs.get(method.name), output = outputs.get(method.name);
    const copy = structuredClone(input);
    requireValid(toolInput(method), input); requireValid(toolOutput(method), output);
    for (const field of toolInput(method).required) assert.throws(() => requireValid(toolInput(method), without(input, field)));
    for (const field of ['document', 'documentRevision', 'historyRevision']) {
      if (output.command) assert.throws(() => requireValid(toolOutput(method), { ...output, command: without(output.command, field) }));
      else assert.throws(() => requireValid(toolOutput(method), without(output, field)));
    }
    if (output.command) assert.throws(() => requireValid(toolOutput(method), { ...without(output, 'command'), ...output.command }));
    else assert.throws(() => requireValid(toolOutput(method), { ...without(without(without(output, 'document'), 'documentRevision'), 'historyRevision'), state }));
    assert.deepEqual(input, copy);
  }
  for (const item of invalid) assert.throws(() => requireValid(toolInput(methodFor(item.method)), item.input), item.method);
  assert.equal(Object.hasOwn(toolOutput(methodFor('animation.sampleProperties')).properties, 'source'), false);
});

test('作者binary64和规范uint64无损；UUID、材料/Sun值域与正式新增查询字段严格', () => {
  const materialInput = toolInput(methodFor('material.get'));
  for (const id of [materialId, '18446744073709551615']) requireValid(materialInput, { document, materialId: id });
  for (const id of ['0', '01', '-1', '18446744073709551616', 9007199254740992])
    assert.throws(() => requireValid(materialInput, { document, materialId: id }));
  const identitySchema = resolveSchema('j1.schema.json#/$defs/keyIdentity');
  requireValid(identitySchema, keyIdentity);
  for (const field of Object.keys(keyIdentity)) {
    assert.throws(() => requireValid(identitySchema, without(keyIdentity, field)));
    assert.throws(() => requireValid(identitySchema, { ...keyIdentity, [field]: Number(keyIdentity[field]) }));
  }
  for (const value of [NaN, Infinity, -Infinity, -0.001, 1.001])
    assert.throws(() => requireValid(resolveSchema('j1.schema.json#/$defs/parameters'), { ...parameters, roughness: value }));
  for (const value of [NaN, Infinity, -Infinity, -1, 3.402823466385289e38])
    assert.throws(() => requireValid(resolveSchema('j1.schema.json#/$defs/sun'), { ...sun, irradiance: value }));
  assert.notEqual(parameters.baseColor[0], Math.fround(parameters.baseColor[0]));
  assert.notEqual(parameters.roughness, Math.fround(parameters.roughness));
  assert.notEqual(sun.irradiance, Math.fround(sun.irradiance));
  assert.equal(JSON.parse(JSON.stringify(upsert)).time, time);
  assert.equal(JSON.parse(JSON.stringify(upsert)).value, value);
  const current = { ...state, documentUuid, path: '', isModified: false, requiresSaveAs: false, busy: false, busyReasons: [] };
  const currentSchema = toolOutput(methodFor('document.current'));
  requireValid(currentSchema, current);
  assert.throws(() => requireValid(currentSchema, without(current, 'documentUuid')));
  for (const uuid of ['00000000-0000-0000-0000-000000000000', documentUuid.toUpperCase(), documentUuid.replaceAll('-', ''), '{' + documentUuid + '}']) {
    assert.throws(() => requireValid(currentSchema, { ...current, documentUuid: uuid }));
    assert.throws(() => requireValid(toolInput(methodFor('animation.upsertPropertyKeyframe')), { ...upsert, address: { ...address, documentUuid: uuid } }));
  }
  const entity = { entityId, parentId: '0', childIds: [], name: '作者对象', primitive: 'cube', meshId: null,
    meshKind: 'primitive', transform, worldMatrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
    surface, visible: true, effectiveVisible: true, viewportVisible: true, collectionId: '0', camera: null, light: null,
    bounds: { kind: 'evaluated', local: null, world: null }, materialSlots: [{ id: materialSlotId, material: materialId }], sun };
  const entitySchema = toolOutput(methodFor('entity.get'));
  requireValid(entitySchema, { ...state, entity });
  for (const field of ['materialSlots', 'sun']) assert.throws(() => requireValid(entitySchema, { ...state, entity: without(entity, field) }));
  assert.throws(() => requireValid(entitySchema, { ...state, entity: { ...entity, materialSlots: [{ id: materialSlotId, materialId }] } }));
  requireValid(entitySchema, { ...state, entity: { ...entity, materialSlots: [], sun: null } });
});

test('typed子帧分页/2048地址预算；旧sample256不变，已登记Unsupported请求不改写', () => {
  assert.equal(limits.sourcePageDefault, 256); assert.equal(limits.sourcePageMaximum, 2048);
  assert.equal(limits.animationSampleEntities, 256);
  const readSchema = toolInput(methodFor('animation.readPropertyKeyframes'));
  assert.equal(readSchema.properties.limit.default, 256);
  for (const limit of [1, 256, 2048]) requireValid(readSchema, { ...read, limit, afterTime: time });
  for (const limit of [0, 2049, 1.5]) assert.throws(() => requireValid(readSchema, { ...read, limit }));
  for (const time of [0, 100001, NaN, Infinity]) {
    assert.throws(() => requireValid(toolInput(methodFor('animation.upsertPropertyKeyframe')), { ...upsert, time }));
    assert.throws(() => requireValid(readSchema, { ...read, afterTime: time }));
  }
  const maximum = Array.from({ length: 2048 }, (_, index) => ({ ...address, owner: { kind: 'entity', entityId: String(BigInt(entityId) + BigInt(index)) } }));
  const sampleSchema = toolInput(methodFor('animation.sampleProperties'));
  requireValid(sampleSchema, { ...sample, addresses: maximum });
  for (const items of [[], [...maximum, roughnessAddress]])
    assert.throws(() => requireValid(sampleSchema, { ...sample, addresses: items }));
  const sampleOutput = toolOutput(methodFor('animation.sampleProperties'));
  requireValid(sampleOutput, { ...state, frame: 12.5, samples: maximum.map(item => ({ address: item, value })) });
  assert.throws(() => requireValid(sampleOutput, { ...state, frame: 12.5, samples: [...maximum, roughnessAddress].map(item => ({ address: item, value })) }));
  const legacy = toolInput(methodFor('animation.sample'));
  requireValid(legacy, { ...query, frame: 12.5, entityIds: maximum.slice(0, 256).map(item => item.owner.entityId) });
  assert.throws(() => requireValid(legacy, { ...query, frame: 12.5, entityIds: maximum.slice(0, 257).map(item => item.owner.entityId) }));
  const upsertSchema = toolInput(methodFor('animation.upsertPropertyKeyframe'));
  for (const property of ['transform.rotation_euler_xyz_degrees', 'transform.scale'])
    requireValid(upsertSchema, { ...upsert, address: { ...address, property } });
  requireValid(upsertSchema, { ...upsert, outgoing: 'bezier' });
  for (const item of [roughnessAddress, sunAddress]) requireValid(upsertSchema, { ...upsert, address: item, value: 0.5 });
  requireValid(toolOutput(methodFor('animation.deletePropertyKeyframe')), { command: noChange, keyIdentity: null });
  requireValid(toolOutput(methodFor('animation.readPropertyKeyframes')), { ...state, address, curveIdentity: null, keys: [], nextAfterTime: null });
});

test('有理fps覆盖全部uint32字段；有效结果非null，速率/约分和范围交服务明确校验', () => {
  const schema = toolInput(methodFor('animation.setRationalSettings'));
  for (const rationalFps of [{ numerator: 1, denominator: 1 }, { numerator: 120, denominator: 1 },
    { numerator: 30000, denominator: 1001 }, settings.rationalFps]) {
    requireValid(schema, { ...rational, settings: { ...settings, rationalFps } });
  }
  for (const field of ['numerator', 'denominator']) {
    for (const value of [0, -1, 1.5, 4294967296, '30000', NaN, Infinity]) {
      const rationalFps = { ...settings.rationalFps, [field]: value };
      assert.throws(() => requireValid(schema, { ...rational, settings: { ...settings, rationalFps } }));
    }
  }
  for (const rationalFps of [{ numerator: 1, denominator: 2 }, { numerator: 121, denominator: 1 }, { numerator: 60, denominator: 2 }])
    requireValid(schema, { ...rational, settings: { ...settings, rationalFps } });
  const animationState = { ...state, settings: { fps: 24, startFrame: 1, endFrame: 24 },
    rationalFps: { numerator: 30000, denominator: 1001 }, loopPolicy: 'legacyHalfOpen',
    mode: 'base', frame: 24, loop: true, sessionRevision: '1', evaluationId: '0' };
  const stateSchema = toolOutput(methodFor('animation.getState'));
  requireValid(stateSchema, animationState);
  assert.throws(() => requireValid(stateSchema, { ...animationState, rationalFps: null }));
  assert.throws(() => requireValid(schema, { ...rational, settings: { ...settings, loopPolicy: 'halfOpen' } }));
});

const intents = names.map(method => ({ method, input: inputs.get(method), result: outputs.get(method) }));
intents.push(
  { method: 'material.update', input: inputs.get('material.update'), result: { command: noChange, materialId } },
  { method: 'animation.upsertPropertyKeyframe', input: upsert, result: { command: noChange, keyIdentity } },
  { method: 'animation.upsertPropertyKeyframe', input: { ...upsert, linkedEdit: 'editLinked' }, result: { command: noChange, keyIdentity } },
  { method: 'animation.deletePropertyKeyframe', input: { ...inputs.get('animation.deletePropertyKeyframe'), time: 99 },
    result: { command: noChange, keyIdentity: null } },
  { method: 'animation.readPropertyKeyframes', input: { ...read, afterTime: page.nextAfterTime },
    result: { ...page, keys: [{ ...scalarKey, id: '9007199254741997', time: 24, value: 2 }], nextAfterTime: null } },
  { method: 'animation.readPropertyKeyframes', input: { ...read, address: sunAddress },
    result: { ...state, address: sunAddress, curveIdentity: null, keys: [], nextAfterTime: null } }
);
for (const [frame, values] of [[24, [2, 0.8, 2]], [1, [0, 0.2, 1]], [12.5, [1, 0.5, 1.5]], [1, [0, 0.2, 1]]])
  intents.push({ method: 'animation.sampleProperties', input: { ...sample, frame }, result: sampleAt(frame, values) });
for (const [input, fieldPath] of [
  [{ ...upsert, outgoing: 'bezier' }, 'outgoing'],
  [{ ...upsert, address: { ...address, property: 'transform.rotation_euler_xyz_degrees' } }, 'address.property'],
  [{ ...upsert, address: { ...address, property: 'transform.scale' } }, 'address.property']
]) intents.push({ method: 'animation.upsertPropertyKeyframe', input, error: {
  ...state, code: 'UNSUPPORTED_OPERATION', message: '已登记但当前切片未开放', recovery: 'none', fieldPath
} });
for (const rationalFps of [{ numerator: 1, denominator: 2 }, { numerator: 121, denominator: 1 }, { numerator: 60, denominator: 2 }])
  intents.push({ method: 'animation.setRationalSettings', input: { ...rational, settings: { ...settings, rationalFps } },
    error: { ...state, code: 'INVALID_ARGUMENT', message: '有理fps不满足Core规范', recovery: 'correct_input', fieldPath: 'settings.rationalFps' } });
for (const code of ['REVISION_CONFLICT', 'CANCELLED', 'DEADLINE_EXCEEDED', 'PERMISSION_DENIED', 'BUSY'])
  intents.push({ method: 'material.update', input: inputs.get('material.update'),
    error: { ...state, code, message: '预置正式守卫拒绝', recovery: code === 'BUSY' ? 'wait' : 'refetch' } });

/** 逐请求返回预登记结果，查询不消费序号，写入沿现有mock账本。 */
function reply(mock) {
  let offset = 0;
  return request => {
    if (!names.includes(request.method)) return null;
    const intent = intents[offset++];
    assert(intent); assert.equal(request.method, intent.method);
    const fragment = intent.error
      ? { error: { code: -32010, message: intent.error.message, data: intent.error } }
      : { result: intent.result };
    return methodFor(intent.method).kind === 'query' ? fragment : mock.commit(request, fragment);
  };
}

test('J1直接桥：非法输入零连接；11方法、精确分页、no_change和结构化守卫逐项无损', async t => {
  const mock = await new MockBridge().start(), client = new BridgeClient(mock.descriptor);
  mock.onRequest = reply(mock);
  t.after(async () => { client.close(); await mock.close(); assert.equal(mock.failures.length, 0); });
  for (const item of invalid) await assert.rejects(client.call(item.method, item.input));
  assert.equal(mock.requests.length, 0); assert.equal(mock.sockets.size, 0);
  let sequence = 0;
  for (const intent of intents) {
    const before = structuredClone(intent.input), isQuery = methodFor(intent.method).kind === 'query';
    const expected = isQuery ? intent.input : { ...intent.input, clientSessionId: mock.session, mutationSequence: String(++sequence) };
    if (intent.error) await assert.rejects(client.call(intent.method, intent.input), error => {
      assert.deepEqual(error.details, intent.error); assert.equal(error.rpcCode, -32010);
      assert.deepEqual(error.bridge, mock.bridge(String(sequence))); return true;
    });
    else assert.deepEqual(await client.call(intent.method, intent.input), isQuery
      ? { result: intent.result } : { result: intent.result, bridge: mock.bridge(String(sequence)) });
    assert.deepEqual(intent.input, before);
    assert.deepEqual(mock.requests.at(-1).params, expected);
  }
  assert.equal(mock.requests.length, intents.length + 1); assert.equal(mock.highWater, String(sequence));
  assert.deepEqual(mock.requests.slice(1).map(item => item.method), intents.map(item => item.method));
  assert.equal(mock.requests[0].params.apiVersion, '0.3.0'); assert.equal(mock.requests[0].params.wireVersion, 1);
});

const main = fileURLToPath(new URL('../dist/main.js', import.meta.url));
const cwd = fileURLToPath(new URL('..', import.meta.url));
for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
  test('J1官方SDK ' + era + '：72工具和11方法无损映射，已知Unsupported、no_change及守卫保留', async t => {
    const mock = await new MockBridge().start(); mock.onRequest = reply(mock);
    const descriptor = await mock.descriptorFile();
    const transport = new StdioClientTransport({ command: process.execPath, args: [main, '--descriptor', descriptor],
      cwd, env: { ...process.env }, stderr: 'pipe' });
    const client = new Client({ name: 'mini3d-j1-contract-test', version: '0.3.0' }, {
      jsonSchemaValidator: validator, versionNegotiation: { mode, probe: { timeoutMs: 5000, maxRetries: 0 } }
    });
    const errors = []; client.onerror = error => errors.push(String(error));
    t.after(async () => {
      await client.close(); await transport.close(); await mock.close();
      assert.equal(mock.failures.length, 0); assert.equal(errors.length, 0);
    });
    await client.connect(transport, { timeout: 5000 }); assert.equal(client.getProtocolEra(), era);
    if (era === 'modern') assert.equal(client.getNegotiatedProtocolVersion(), '2026-07-28');
    const listed = await client.listTools(); assert.equal(listed.tools.length, 72);
    assert.deepEqual(listed.tools.map(tool => tool.name).sort(), methods.map(method => toolName(method.name)).sort());
    for (const method of selected) {
      const tool = listed.tools.find(item => item.name === toolName(method.name)); assert(tool);
      assert.deepEqual(tool.inputSchema, toolInput(method)); assert.deepEqual(tool.outputSchema, toolOutput(method));
      assert.equal(tool.annotations.readOnlyHint, method.kind === 'query');
      assert.equal(tool.annotations.destructiveHint, method.kind !== 'query'); assert.equal(tool.annotations.openWorldHint, false);
    }
    for (const item of invalid)
      assert.equal((await client.callTool({ name: toolName(item.method), arguments: item.input })).isError, true);
    assert.equal(mock.requests.length, 0); assert.equal(mock.sockets.size, 0);
    let sequence = 0;
    for (const intent of intents) {
      const before = structuredClone(intent.input), isQuery = methodFor(intent.method).kind === 'query';
      const expected = isQuery ? intent.input : { ...intent.input, clientSessionId: mock.session, mutationSequence: String(++sequence) };
      const result = await client.callTool({ name: toolName(intent.method), arguments: intent.input });
      if (intent.error) {
        assert.equal(result.isError, true); assert.deepEqual(result.structuredContent.error, intent.error);
        assert.deepEqual(result.structuredContent.bridge, mock.bridge(String(sequence)));
      } else {
        assert.notEqual(result.isError, true); assert.deepEqual(result.structuredContent, intent.result);
        assert.equal(Object.hasOwn(result.structuredContent, 'source'), false);
      }
      assert.deepEqual(intent.input, before); assert.deepEqual(mock.requests.at(-1).params, expected);
    }
    assert.equal(mock.requests.length, intents.length + 1); assert.equal(mock.highWater, String(sequence));
    assert.deepEqual(mock.requests.slice(1).map(item => item.method), intents.map(item => item.method));
    assert.equal(mock.requests[0].params.apiVersion, '0.3.0'); assert.equal(mock.requests[0].params.wireVersion, 1);
  });
}
