/*
模块名: stdio.test
功能概述: 用官方客户端验收真实 stdio、静态工具合同和图像 content。
对外接口: Node test runner。
依赖关系: 官方 client 2.3.1、adapter 子进程、本机 mock bridge。
输入输出: legacy/modern MCP 调用 -> Schema、错误、图像及生命周期断言。
异常与错误: 协议解析错误或 stdout 污染使测试失败；始终关闭自有子进程。
维护说明: 图像是合同夹具，不声称真实视口或 Codex 显示验收通过。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { methods, toolInput, toolOutput, validator } from '../dist/schema.js';
import { toolName } from '../dist/tools.js';
import { MockBridge, createInput, captureInput, document, png } from './mock-bridge.mjs';

const main = fileURLToPath(new URL('../dist/main.js', import.meta.url));
const cwd = fileURLToPath(new URL('..', import.meta.url));

for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
  test(`官方 client ${era} stdio：${methods.length} 个工具、uint64、领域错误、PNG 与退出`, async t => {
    const mock = await new MockBridge().start();
    const filename = await mock.descriptorFile();
    const transport = new StdioClientTransport({ command: process.execPath, args: [main, '--descriptor', filename], cwd,
      env: { ...process.env }, stderr: 'pipe' });
    const client = new Client({ name: 'mini3d-controlled-mcp-test', version: '0.1.0' }, {
      jsonSchemaValidator: validator,
      versionNegotiation: { mode, probe: { timeoutMs: 5000, maxRetries: 0 } }
    });
    const protocolErrors = [];
    let stderr = '';
    transport.stderr?.on('data', chunk => { stderr += chunk.toString('utf8'); });
    client.onerror = error => protocolErrors.push(error);
    t.after(async () => {
      const pid = transport.pid;
      await client.close(); await transport.close(); await mock.close();
      assert.equal(mock.failures.length, 0);
      assert.equal(protocolErrors.length, 0);
      assert.equal(stderr.includes(mock.descriptor.secret), false, 'secret never enters stderr');
      assert.equal(stderr.includes(filename), false, 'private descriptor path never enters stderr');
      if (pid) assert.throws(() => process.kill(pid, 0), error => error.code === 'ESRCH');
    });
    await client.connect(transport, { timeout: 5000 });
    assert.equal(client.getProtocolEra(), era);
    if (era === 'modern') assert.equal(client.getNegotiatedProtocolVersion(), '2026-07-28');
    const listed = await client.listTools();
    assert.deepEqual(listed.tools.map(tool => tool.name).sort(), methods.map(method => toolName(method.name)).sort());
    for (const method of methods) {
      const tool = listed.tools.find(entry => entry.name === toolName(method.name));
      assert.deepEqual(tool.inputSchema, toolInput(method));
      assert.deepEqual(tool.outputSchema, toolOutput(method));
      assert.equal(tool.annotations.readOnlyHint, method.kind === 'query');
    }
    assert.equal(mock.requests.length, 0, 'listing static tools does not choose or connect an instance');
    const describe = await client.callTool({ name: toolName('system.describe'), arguments: {} });
    assert.equal(describe.structuredContent.apiVersion, '0.2.0');
    assert.equal(describe.structuredContent.methods.length, methods.length);
    const entity = await client.callTool({ name: toolName('entity.get'), arguments: { document, entityId: '9007199254740993' } });
    assert.equal(entity.structuredContent.entity.entityId, '9007199254740993');
    assert.equal(entity.structuredContent.entity.name, '中文装甲块');
    for (const invalid of [
      { document, entityId: '18446744073709551616' }, { document, entityId: '01' }, { document, entityId: 42 },
      { document, entityId: '1', unknown: true }, { document: { ...document, extra: true }, entityId: '1' }
    ]) {
      const rejected = await client.callTool({ name: toolName('entity.get'), arguments: invalid });
      assert.equal(rejected.isError, true);
    }
    assert.equal(mock.requests.filter(request => request.method === 'entity.get').length, 1, 'invalid inputs do not reach bridge');
    const created = await client.callTool({ name: toolName('entity.create'), arguments: createInput });
    assert.equal(created.structuredContent.created.entityIds[0], '9007199254740993');
    assert.equal(created.structuredContent.status, 'committed');
    assert.ok(created.content.some(item => item.type === 'text' && item.text.includes('"source":"adapter"')));
    for (const hidden of ['clientSessionId', 'mutationSequence']) {
      const rejected = await client.callTool({ name: toolName('entity.create'), arguments: { ...createInput, [hidden]: '1' } });
      assert.equal(rejected.isError, true);
    }
    mock.onRequest = request => request.method === 'entity.update' ? mock.commit(request, mock.error('REVISION_CONFLICT')) : null;
    const failed = await client.callTool({ name: toolName('entity.update'), arguments: { document, expectedDocumentRevision: '1', entityId: '9007199254740993', name: '旧版本' } });
    assert.equal(failed.isError, true);
    assert.equal(failed.structuredContent.error.code, 'REVISION_CONFLICT');
    assert.equal(failed.structuredContent.error.recovery, 'refetch');
    assert.equal(failed.structuredContent.error.documentRevision, '1');
    assert.equal(failed.structuredContent.bridge.highWater, '2');
    const capture = await client.callTool({ name: toolName('viewport.capture'), arguments: captureInput });
    const image = capture.content.find(item => item.type === 'image');
    assert.ok(image);
    assert.equal(image.mimeType, 'image/png');
    const bytes = Buffer.from(image.data, 'base64');
    assert.deepEqual(bytes, png);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), capture.structuredContent.sha256);
    assert.equal(bytes.length, capture.structuredContent.byteLength);
    assert.equal(Object.hasOwn(capture.structuredContent, 'pngBase64'), false);
    assert.equal(capture.content.some(item => item.type === 'text' && item.text.includes(image.data)), false);
    mock.onRequest = request => request.method === 'viewport.capture' ? { result: { ...capture.structuredContent, pngBase64: png.toString('base64'), sha256: '0'.repeat(64) } } : null;
    const mismatched = await client.callTool({ name: toolName('viewport.capture'), arguments: captureInput });
    assert.equal(mismatched.isError, true);
    assert.equal(mismatched.structuredContent.error.code, 'INVALID_IMAGE');
    assert.equal(mismatched.content.some(item => item.type === 'image'), false);
    const matching = { ...capture.structuredContent, pngBase64: png.toString('base64') };
    for (const changed of [
      { document: { ...document, instanceId: '11111111-1111-4111-8111-111111111111' } },
      { document: { ...document, documentId: '22222222-2222-4222-8222-222222222222' } },
      { documentRevision: '2' }, { viewportRevision: '2' }, { evaluationId: '1' },
      { mode: 'playing' }, { mode: 'pose_draft' }
    ]) {
      mock.onRequest = request => request.method === 'viewport.capture'
        ? { result: { ...matching, view: { ...matching.view, ...changed } } } : null;
      const wrongIdentity = await client.callTool({ name: toolName('viewport.capture'), arguments: captureInput });
      assert.equal(wrongIdentity.isError, true);
      assert.equal(wrongIdentity.structuredContent.error.code, 'STALE_EVALUATION');
      assert.equal(wrongIdentity.content.some(item => item.type === 'image'), false);
    }
  });
}

test('缺少显式 descriptor 的启动失败仅写 stderr，stdout 无污染', async () => {
  const child = spawn(process.execPath, [main], { cwd, env: { ...process.env }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', bytes => { stdout += bytes.toString('utf8'); });
  child.stderr.on('data', bytes => { stderr += bytes.toString('utf8'); });
  const exitCode = await new Promise(resolve => child.once('exit', resolve));
  assert.equal(exitCode, 1);
  assert.equal(stdout, '');
  assert.ok(stderr.includes('启动失败'));
});
