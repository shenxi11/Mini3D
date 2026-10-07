/*
模块名: bridge.test
功能概述: 验证显式实例、单个写入与断线/取消恢复语义。
对外接口: Node test runner。
依赖关系: 真实 Node 本机管道、mock bridge 与 BridgeClient。
输入输出: 故障注入 RPC -> 是否执行、原会话/序号与结构化错误断言。
异常与错误: 每例清理自身连接；未确认结果必须阻断新写入。
维护说明: 不连接或修改真实 Mini3D，mock 不代替本体 E2E。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { BridgeClient, readDescriptor } from '../dist/bridge.js';
import { MockBridge, createInput, captureInput, commandResult, state } from './mock-bridge.mjs';

async function fixture(t) {
  const mock = await new MockBridge().start();
  const client = new BridgeClient(mock.descriptor);
  t.after(async () => { client.close(); await mock.close(); assert.equal(mock.failures.length, 0); });
  return { mock, client };
}
const code = expected => error => error.details?.code === expected;
const mutations = mock => mock.requests.filter(request => request.method === 'entity.create');

test('M5-01 十一方法仅映射RPC并保留复制/集合结构化结果', async t => {
  const { mock, client } = await fixture(t);
  const examples = JSON.parse(await readFile(new URL('../../api/schema/m5-examples.json', import.meta.url), 'utf8'));
  mock.onRequest = request => {
    if (!examples.valid.some(example => example.method === request.method)) return null;
    let result = commandResult;
    if (request.method === 'entity.duplicate') result = { ...commandResult, entityIdMap: [{ sourceEntityId: '1', entityId: '9007199254740993' }] };
    if (request.method.startsWith('collection.')) result = { ...commandResult, collectionId: request.params.collectionId ?? '9007199254740993' };
    return mock.commit(request, { result });
  };
  for (const [index, example] of examples.valid.entries()) {
    const result = await client.call(example.method, example.value);
    assert.equal(result.result.status, 'committed');
    const request = mock.requests.at(-1);
    assert.equal(request.method, example.method);
    assert.equal(request.params.mutationSequence, String(index + 1));
    assert.equal(request.params.clientSessionId, mock.session);
    for (const [key, value] of Object.entries(example.value)) assert.deepEqual(request.params[key], value);
    if (example.method === 'entity.duplicate') assert.equal(result.result.entityIdMap[0].entityId, '9007199254740993');
    if (example.method === 'collection.assign') assert.equal(result.result.collectionId, '0');
  }
  assert.equal(mock.requests.filter(request => request.method !== 'bridge.hello').length, 11);
});

test('close 在挂起 hello 时立即关闭自有管道，放行合法握手也不能写入', { timeout: 1500 }, async t => {
  const { mock, client } = await fixture(t);
  let receivedHello;
  const helloReceived = new Promise(resolve => { receivedHello = resolve; });
  let releaseHello;
  const helloGate = new Promise(resolve => { releaseHello = resolve; });
  let socketClosed;
  t.after(() => releaseHello());
  mock.onRequest = async (request, socket) => {
    if (request.method !== 'bridge.hello') return null;
    // 放行关闭后的握手可能产生 EPIPE；断言管道已关闭，而非等待新的响应。
    socketClosed = new Promise(resolve => socket.once('close', resolve));
    receivedHello();
    await helloGate;
    return { result: { instanceId: mock.descriptor.instanceId, bridgeId: mock.descriptor.bridgeId, clientSessionId: mock.session,
      highWater: '0', nextMutationSequence: '1', permissions: mock.descriptor.permissions } };
  };
  const outcome = client.call('entity.create', createInput).then(result => ({ result }), error => ({ error }));
  await helloReceived;
  client.close();
  releaseHello();
  const settled = await outcome;
  assert.equal(mutations(mock).length, 0, 'a released hello must never send the original mutation after close');
  assert.equal(settled.error?.details?.code, 'ADAPTER_CLOSED');
  await socketClosed;
  assert.equal(mock.sockets.size, 0);
  await assert.rejects(client.call('document.current', {}), code('ADAPTER_CLOSED'));
});

test('close 在 native connect 尚未完成时取消连接，不等私有 deadline 或发送 hello', { timeout: 1500 }, async t => {
  const { mock, client } = await fixture(t);
  const outcome = client.call('entity.create', createInput).then(result => ({ result }), error => ({ error }));
  client.close();
  const settled = await outcome;
  assert.equal(mock.requests.length, 0, 'no hello or mutation may follow close during native connect');
  assert.equal(settled.error?.details?.code, 'ADAPTER_CLOSED');
  await new Promise(resolve => setImmediate(resolve));
  await Promise.all([...mock.sockets].map(socket => new Promise(resolve => socket.once('close', resolve))));
  assert.equal(mock.sockets.size, 0);
});

test('仅显式绝对 descriptor，未知字段、版本与远程管道名称拒绝', async t => {
  const { mock } = await fixture(t);
  const filename = await mock.descriptorFile();
  assert.deepEqual(await readDescriptor(filename), mock.descriptor);
  await assert.rejects(readDescriptor('descriptor.json'), code('INVALID_DESCRIPTOR'));
  const original = await readFile(filename, 'utf8');
  for (const changed of [{ ...mock.descriptor, apiVersion: '0.1.0' }, { ...mock.descriptor, wireVersion: 2 }, { ...mock.descriptor, pipe: '\\\\host\\pipe\\mini3d-other' },
    { ...mock.descriptor, unexpected: true }, { ...mock.descriptor, instanceId: 'not-a-uuid' }]) {
    await writeFile(filename, JSON.stringify(changed), 'utf8');
    await assert.rejects(readDescriptor(filename));
  }
  await writeFile(filename, original, 'utf8');
  assert.equal(mock.requests.length, 0);
});

test('会话与序号归 adapter，两个终态写入只用服务 highWater 的 1/2', async t => {
  const { mock, client } = await fixture(t);
  const input = structuredClone(createInput);
  const first = await client.call('entity.create', input);
  assert.equal(first.result.created.entityIds[0], '9007199254740993');
  assert.equal(first.bridge.highWater, '1');
  await client.call('entity.create', { ...input, name: '第二块' });
  assert.deepEqual(input, createInput);
  assert.deepEqual(mutations(mock).map(request => request.params.mutationSequence), ['1', '2']);
  assert.ok(mutations(mock).every(request => request.params.clientSessionId === mock.session));
  assert.deepEqual(mock.requests.filter(request => request.method === 'bridge.hello').map(request => request.params.mode), ['new']);
  await assert.rejects(client.call('entity.create', { ...input, clientSessionId: mock.session }));
  await assert.rejects(client.call('entity.create', { ...input, mutationSequence: '99' }));
  assert.equal(mutations(mock).length, 2);
});

test('hello 返回不同实例或 bridge 时拒绝，任何领域命令都不发送', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = request => request.method === 'bridge.hello' ? { result: {
    instanceId: mock.descriptor.instanceId, bridgeId: randomUUID(), clientSessionId: mock.session,
    highWater: '0', nextMutationSequence: '1', permissions: mock.descriptor.permissions
  } } : null;
  await assert.rejects(client.call('entity.create', createInput), code('INSTANCE_MISMATCH'));
  assert.equal(mutations(mock).length, 0);
});

test('领域失败有终态 bridge 才消费序号，保留 code/recovery/current revision', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = request => request.method === 'entity.create' && request.params.mutationSequence === '1'
    ? mock.commit(request, mock.error('BUSY', 'wait')) : null;
  await assert.rejects(client.call('entity.create', createInput), error => {
    assert.equal(error.details.code, 'BUSY'); assert.equal(error.details.recovery, 'wait');
    assert.equal(error.details.documentRevision, '1'); assert.equal(error.bridge.highWater, '1');
    return true;
  });
  await client.call('entity.create', createInput);
  assert.deepEqual(mutations(mock).map(request => request.params.mutationSequence), ['1', '2']);
});

test('响应丢失仅 resume 原会话并查 status，已提交请求只执行一次', async t => {
  const { mock, client } = await fixture(t);
  let dropped = false;
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create' && !dropped) {
      dropped = true; mock.commit(request); socket.destroy(); return undefined;
    }
    return null;
  };
  const recovered = await client.call('entity.create', createInput);
  assert.equal(recovered.bridge.replayed, true);
  assert.deepEqual(recovered.result, commandResult);
  assert.equal(mutations(mock).length, 1);
  assert.deepEqual(mock.requests.filter(request => request.method === 'bridge.hello').map(request => request.params.mode), ['new', 'resume']);
  assert.equal(mock.requests.find(request => request.params.mode === 'resume').params.clientSessionId, mock.session);
  await client.call('entity.create', { ...createInput, name: '后续明确请求' });
  assert.deepEqual(mutations(mock).map(request => request.params.mutationSequence), ['1', '2']);
});

test('not_seen 不自动重放；新写阻断，显式 current 重查独立返回 adapterRecovery', async t => {
  const { mock, client } = await fixture(t);
  let dropped = false;
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create' && !dropped) { dropped = true; socket.destroy(); return undefined; }
    return null;
  };
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  await assert.rejects(client.call('entity.create', { ...createInput, name: '不可发送' }), code('OUTCOME_UNKNOWN'));
  assert.equal(mutations(mock).length, 1);
  const current = await client.call('document.current', {});
  assert.equal(current.adapterRecovery.source, 'adapter');
  assert.equal(current.adapterRecovery.state, 'not_seen');
  assert.equal(Object.hasOwn(current.result, 'adapterRecovery'), false);
  await client.call('entity.create', { ...createInput, name: '确认未接收后的新意图' });
  assert.deepEqual(mutations(mock).map(request => request.params.mutationSequence), ['1', '1']);
  assert.notEqual(mutations(mock)[1].params.name, mutations(mock)[0].params.name);
});

test('pending 写入无成功结果；状态明确完成后 current 重查解除阻断', async t => {
  const { mock, client } = await fixture(t);
  let pendingRequest;
  mock.onRequest = request => {
    if (request.method === 'entity.create') {
      pendingRequest = request;
      return { result: { status: 'pending' }, bridge: { ...mock.bridge('1'), executionState: 'pending' } };
    }
    if (request.method === 'bridge.requestStatus' && !mock.ledger.size) {
      return { result: { state: 'pending', highWater: '0', nextMutationSequence: '1', bridge: { ...mock.bridge('1'), executionState: 'pending' } } };
    }
    return null;
  };
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  await assert.rejects(client.call('history.undo', { document: state.document, expectedDocumentRevision: '1', expectedHistoryRevision: '1' }), code('OUTCOME_UNKNOWN'));
  mock.commit(pendingRequest);
  const current = await client.call('document.current', {});
  assert.equal(current.adapterRecovery.state, 'completed');
  assert.deepEqual(current.adapterRecovery.response, commandResult);
  assert.equal(mutations(mock).length, 1);
});

test('缺失 bridge 的领域错误保持 outcome_unknown，不凭错误推测消费', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = request => request.method === 'entity.create' ? mock.error('REVISION_CONFLICT') : null;
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  assert.equal(mutations(mock).length, 1);
  const current = await client.call('document.current', {});
  assert.equal(current.adapterRecovery.state, 'not_seen');
});

test('非法 UTF8 响应不能当作提交结果；只有原账本确认后才恢复', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create') {
      mock.commit(request);
      const header = Buffer.alloc(4); header.writeUInt32LE(2);
      socket.write(Buffer.concat([header, Buffer.from([0xc0, 0xaf])]));
      return undefined;
    }
    return null;
  };
  const recovered = await client.call('entity.create', createInput);
  assert.equal(recovered.bridge.replayed, true);
  assert.equal(mutations(mock).length, 1);
  assert.equal(mock.requests.filter(request => request.method === 'bridge.requestStatus').length, 1);
});

test('领域结果无法按 Schema 解码时保持 unknown，即使收到完成 watermark', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = request => request.method === 'entity.create' ? mock.commit(request, { result: { status: 'committed' } }) : null;
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  assert.equal(mutations(mock).length, 1);
  mock.ledger.set('1', { result: commandResult });
  const current = await client.call('document.current', {});
  assert.equal(current.adapterRecovery.state, 'completed');
  assert.deepEqual(current.adapterRecovery.response, commandResult);
});

test('session 过期保持原会话且不降级 new，不接管其他实例', async t => {
  const { mock, client } = await fixture(t);
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create') { socket.destroy(); return undefined; }
    if (request.method === 'bridge.hello' && request.params.mode === 'resume') return mock.error('SESSION_EXPIRED', 'query_result');
    return null;
  };
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  await assert.rejects(client.call('document.current', {}), code('SESSION_EXPIRED'));
  await assert.rejects(client.call('entity.create', createInput), code('OUTCOME_UNKNOWN'));
  const modes = mock.requests.filter(request => request.method === 'bridge.hello').map(request => request.params.mode);
  assert.equal(modes.filter(mode => mode === 'new').length, 1);
  assert.ok(modes.slice(1).every(mode => mode === 'resume'));
  assert.equal(mutations(mock).length, 1);
});

test('result_expired 用服务 highWater 消费原序号，绝不再次执行原请求', async t => {
  const { mock, client } = await fixture(t);
  let first = true;
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create' && first) { first = false; mock.highWater = '1'; socket.destroy(); return undefined; }
    if (request.method === 'bridge.requestStatus') return { result: { state: 'result_expired', highWater: '1', nextMutationSequence: '2' } };
    return null;
  };
  await assert.rejects(client.call('entity.create', createInput), code('RESULT_EXPIRED'));
  await client.call('entity.create', { ...createInput, name: '明确后续请求' });
  assert.deepEqual(mutations(mock).map(request => request.params.mutationSequence), ['1', '2']);
});

test('一个写请求 in-flight；SDK 取消只 best-effort，已开始写入仍确认终态', async t => {
  const { mock, client } = await fixture(t);
  const controller = new AbortController();
  let startedResolve;
  const started = new Promise(resolve => { startedResolve = resolve; });
  let pendingRequest;
  let pendingSocket;
  mock.onRequest = (request, socket) => {
    if (request.method === 'entity.create') { pendingRequest = request; pendingSocket = socket; startedResolve(); return undefined; }
    if (request.method === 'bridge.cancel') {
      assert.equal(request.params.target.kind, 'mutation'); assert.equal(request.params.target.mutationSequence, '1');
      mock.send(pendingSocket, pendingRequest, mock.commit(pendingRequest));
      return { result: { status: 'cannot_cancel_started' } };
    }
    return null;
  };
  const writing = client.call('entity.create', createInput, controller.signal);
  await started;
  await assert.rejects(client.call('entity.create', createInput), code('WRITE_IN_FLIGHT'));
  controller.abort();
  assert.equal((await writing).result.status, 'committed');
  assert.equal(mutations(mock).length, 1);
  assert.equal(mock.requests.filter(request => request.method === 'bridge.cancel').length, 1);
});

test('capture 本地期限映射 request cancel，未确认取消不会生成图像', async t => {
  const { mock, client } = await fixture(t);
  let capture;
  let captureSocket;
  mock.onRequest = (request, socket) => {
    if (request.method === 'viewport.capture') { capture = request; captureSocket = socket; return undefined; }
    if (request.method === 'bridge.cancel') {
      assert.equal(request.params.target.kind, 'request'); assert.equal(request.params.target.requestId, capture.id);
      mock.send(captureSocket, capture, mock.error('CANCELLED', 'none'));
      return { result: { status: 'cancelled' } };
    }
    return null;
  };
  await assert.rejects(client.call('viewport.capture', { ...captureInput, timeoutMs: 1 }), code('CANCELLED'));
  assert.equal(mock.requests.filter(request => request.method === 'bridge.cancel').length, 1);
  assert.equal(mutations(mock).length, 0);
});
