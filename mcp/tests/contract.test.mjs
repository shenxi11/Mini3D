/*
模块名: contract.test
功能概述: 验证共享合同派生、uint64、严格字段和有界私有帧。
对外接口: Node test runner。
依赖关系: 已编译 Schema/帧模块、冻结合法和非法样例。
输入输出: 合同样例与恶意帧 -> 断言结果。
异常与错误: 任一合同漂移或预算漏检使测试失败。
维护说明: 不连接真实应用，不创建用户场景文件。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { methods, limits, resolveSchema, toolInput, toolOutput, requireValid } from '../dist/schema.js';
import { FrameDecoder, encodeFrame, parseBoundedJson } from '../dist/frame.js';
import { toolName } from '../dist/tools.js';
import { createInput, document } from './mock-bridge.mjs';

test('所有方法 1:1 映射，同一 Schema 只移除 adapter 会话字段与截图文本字节', () => {
  const catalog = JSON.parse(readFileSync(new URL('../../api/schema/methods.json', import.meta.url), 'utf8'));
  assert.deepEqual(methods, catalog.methods);
  assert.equal(new Set(methods.map(method => toolName(method.name))).size, methods.length);
  for (const method of methods) {
    const expectedInput = structuredClone(resolveSchema(method.schema));
    expectedInput.properties ??= {};
    delete expectedInput.properties.clientSessionId;
    delete expectedInput.properties.mutationSequence;
    if (expectedInput.required) expectedInput.required = expectedInput.required.filter(name => !['clientSessionId', 'mutationSequence'].includes(name));
    assert.deepEqual(toolInput(method), expectedInput);
    assert.equal(toolInput(method).additionalProperties, false);
    const output = structuredClone(resolveSchema(method.resultSchema));
    if (method.name === 'viewport.capture') {
      delete output.properties.pngBase64;
      output.required = output.required.filter(name => name !== 'pngBase64');
    }
    assert.deepEqual(toolOutput(method), output);
    assert.equal(JSON.stringify(toolInput(method)).includes('"$ref"'), false);
  }
});

test('共享合法/非法样例无损验证，包括 >2^53、溢出、负缩放和条件字段', () => {
  const examples = JSON.parse(readFileSync(new URL('../../api/schema/examples.json', import.meta.url), 'utf8'));
  for (const example of examples.valid) requireValid(resolveSchema(`common.schema.json#/$defs/${example.kind}`), example.value);
  for (const example of examples.invalid) assert.throws(() => requireValid(resolveSchema(`common.schema.json#/$defs/${example.kind}`), example.value));
  for (const example of examples.m2.valid) requireValid(resolveSchema(`m2.schema.json#/$defs/${example.definition}`), example.value);
  for (const example of examples.m2.invalid) assert.throws(() => requireValid(resolveSchema(`m2.schema.json#/$defs/${example.definition}`), example.value));
  const schema = toolInput(methods.find(method => method.name === 'entity.create'));
  for (const field of ['extra', 'clientSessionId', 'mutationSequence']) assert.throws(() => requireValid(schema, { ...createInput, [field]: '1' }));
  assert.throws(() => requireValid(schema, { ...createInput, transform: { ...createInput.transform, extra: true } }));
  for (const value of [NaN, Infinity, -Infinity]) assert.throws(() => requireValid(resolveSchema('common.schema.json#/$defs/vec3'), [value, 0, 0]));
  requireValid(resolveSchema('m1.schema.json#/$defs/entityGet'), { document, entityId: '18446744073709551615' });
  assert.throws(() => requireValid(resolveSchema('m1.schema.json#/$defs/entityGet'), { document, entityId: '18446744073709551616' }));
});

test('M5-01 共用对象、集合和设备样例严格验证并保护会话字段', () => {
  const examples = JSON.parse(readFileSync(new URL('../../api/schema/m5-examples.json', import.meta.url), 'utf8'));
  for (const example of examples.valid) {
    const method = methods.find(method => method.name === example.method);
    assert(method);
    requireValid(toolInput(method), example.value);
    for (const hidden of ['clientSessionId', 'mutationSequence']) {
      assert.throws(() => requireValid(toolInput(method), { ...example.value, [hidden]: '1' }));
    }
  }
  for (const example of examples.invalid) {
    assert.throws(() => requireValid(resolveSchema(`m5.schema.json#/$defs/${example.definition}`), example.value));
  }
});

test('LE 帧处理分片头、中文分片、粘包和半包断线', () => {
  const first = { text: '中文装甲块', entityId: '9007199254740993' };
  const encoded = encodeFrame(first);
  assert.equal(encoded.readUInt32LE(), encoded.length - 4);
  const decoder = new FrameDecoder();
  const actual = [];
  for (const byte of encoded) actual.push(...decoder.push(Buffer.of(byte)));
  actual.push(...decoder.push(Buffer.concat([encodeFrame({ second: true }), encodeFrame({ third: true })])));
  assert.deepEqual(actual, [first, { second: true }, { third: true }]);
  decoder.finish();
  for (const bytes of [encoded.subarray(0, 2), encoded.subarray(0, 5)]) {
    const partial = new FrameDecoder();
    partial.push(bytes);
    assert.throws(() => partial.finish());
  }
});

test('零长度、超帧、非法 UTF8、深度和节点预算在构树前拒绝', () => {
  for (const length of [0, limits.responseBytes + 1]) {
    const header = Buffer.alloc(4); header.writeUInt32LE(length);
    assert.throws(() => new FrameDecoder().push(header));
  }
  for (const bytes of [Buffer.from([0xc0, 0xaf]), Buffer.from([0xed, 0xa0, 0x80]), Buffer.from([0xf0, 0x80, 0x80, 0x80])]) {
    assert.throws(() => parseBoundedJson(bytes));
  }
  const nested = '['.repeat(limits.jsonDepth + 1) + '0' + ']'.repeat(limits.jsonDepth + 1);
  assert.throws(() => parseBoundedJson(Buffer.from(nested)), /budget/);
  const tooMany = '[' + '0,'.repeat(limits.jsonNodes) + '0]';
  assert.throws(() => parseBoundedJson(Buffer.from(tooMany)), /budget/);
  assert.throws(() => parseBoundedJson(Buffer.from('{')));
  assert.throws(() => encodeFrame({ large: 'x'.repeat(limits.requestBytes) }));
});
