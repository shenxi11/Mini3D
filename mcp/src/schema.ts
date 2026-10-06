/*
模块名: schema
功能概述: 从冻结的公共 Schema 派生 MCP 合同；不另建业务字段定义。
对外接口: methods、limits、resolveSchema、toolInput、toolOutput、validator。
依赖关系: api/schema、Ajv2020、ajv-formats、官方 SDK validator 接口。
输入输出: 方法目录和本地 $ref -> 解引用 Schema 与严格验证器。
异常与错误: 未知引用或无效合同在启动时失败；输入错误保留验证失败。
维护说明: 只移除 adapter 拥有的顶层会话字段，uint64 始终是规范数字串。
*/
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { Ajv2020 } from 'ajv/dist/2020.js';
import type { FormatsPlugin } from 'ajv-formats';
import type { JsonSchemaType, JsonSchemaValidator, jsonSchemaValidator } from '@modelcontextprotocol/server';

export type ObjectValue = Record<string, unknown>;
export type Schema = Record<string, unknown>;
export interface Method {
  name: string;
  kind: 'query' | 'mutation' | 'file';
  permission: string;
  schema: string;
  resultSchema: string;
}

const schemaRoot = new URL('../../api/schema/', import.meta.url);
const catalog = JSON.parse(readFileSync(new URL('methods.json', schemaRoot), 'utf8')) as {
  apiVersion: string; wireVersion: number; limits: Record<string, number>; methods: Method[];
};
export const methods = catalog.methods;
export const limits = catalog.limits as Record<string, number> & {
  requestBytes: number; responseBytes: number; jsonDepth: number; jsonNodes: number;
  queuedRequests: number; mutationTimeoutMs: number; captureTimeoutMs: number; capturePngBytes: number;
};
export const apiVersion = catalog.apiVersion;
export const wireVersion = catalog.wireVersion;
const files = new Map<string, Schema>();
const resolvedSchemas = new Map<string, Schema>();
const inputSchemas = new Map<string, Schema>();
const outputSchemas = new Map<string, Schema>();
for (const name of ['common', 'm1', 'm2', 'm3', 'm5', 'm5-modeling', 'm5-modifiers', 'm5-files', 'm6', 'results', 'bridge']) {
  const filename = `${name}.schema.json`;
  files.set(filename, JSON.parse(readFileSync(new URL(filename, schemaRoot), 'utf8')) as Schema);
}

/** 仅解析公共目录的本地引用，保留 $ref 邻接约束的合取语义。 */
export function resolveSchema(reference: string): Schema {
  const cached = resolvedSchemas.get(reference);
  if (cached) return cached;
  function walk(value: unknown, file: string, stack: string[]): unknown {
    if (Array.isArray(value)) return value.map(item => walk(item, file, stack));
    if (!value || typeof value !== 'object') return value;
    const object = value as Schema;
    if (typeof object.$ref === 'string') {
      const [filename, pointer = ''] = object.$ref.split('#');
      const targetFile = filename || file;
      const key = `${targetFile}#${pointer}`;
      if (stack.includes(key)) throw new Error('Recursive contract reference is unsupported');
      let target: unknown = files.get(targetFile);
      for (const segment of pointer.split('/').slice(1)) {
        target = (target as Schema | undefined)?.[segment.replaceAll('~1', '/').replaceAll('~0', '~')];
      }
      if (!target) throw new Error('Unknown contract reference');
      const resolved = walk(target, targetFile, [...stack, key]);
      const { $ref: _reference, ...siblings } = object;
      return Object.keys(siblings).length ? { allOf: [resolved, walk(siblings, file, stack)] } : resolved;
    }
    return Object.fromEntries(Object.entries(object).map(([key, item]) => [key, walk(item, file, stack)]));
  }
  const resolved = walk({ $ref: reference }, '', []) as Schema;
  resolvedSchemas.set(reference, resolved);
  return resolved;
}

function omitTopLevel(schema: Schema, names: string[]): Schema {
  const copy = structuredClone(schema);
  const properties = (copy.properties ?? {}) as Schema;
  for (const name of names) delete properties[name];
  copy.properties = properties;
  if (Array.isArray(copy.required)) copy.required = copy.required.filter(name => !names.includes(String(name)));
  return copy;
}

/** MCP 调用者不能提供或覆盖 adapter 的会话和序号。 */
export function toolInput(method: Method): Schema {
  let schema = inputSchemas.get(method.name);
  if (!schema) {
    schema = omitTopLevel(resolveSchema(method.schema), ['clientSessionId', 'mutationSequence']);
    inputSchemas.set(method.name, schema);
  }
  return schema;
}

/** 图片字节只进入 image content；公开输出仍来自私有结果合同。 */
export function toolOutput(method: Method): Schema {
  const cached = outputSchemas.get(method.name);
  if (cached) return cached;
  const schema = resolveSchema(method.resultSchema);
  const result = method.name === 'viewport.capture' ? omitTopLevel(schema, ['pngBase64']) : schema;
  outputSchemas.set(method.name, result);
  return result;
}

const ajv = new Ajv2020({ strict: false, strictNumbers: true, allErrors: false, coerceTypes: false, useDefaults: false });
const addFormats = createRequire(import.meta.url)('ajv-formats') as FormatsPlugin;
addFormats(ajv);
ajv.addFormat('uint64-decimal', {
  type: 'string',
  validate: value => /^(0|[1-9][0-9]*)$/.test(value) && value.length <= 20 && BigInt(value) <= 18446744073709551615n
});

/** 为 SDK 与私有响应共用验证器，保留原始数据而不删字段或改数值。 */
export const validator: jsonSchemaValidator = {
  getValidator<T>(schema: JsonSchemaType): JsonSchemaValidator<T> {
    const validate = ajv.compile(schema);
    return input => validate(input)
      ? { valid: true, data: input as T, errorMessage: undefined }
      : { valid: false, data: undefined, errorMessage: ajv.errorsText(validate.errors, { dataVar: 'input' }) };
  }
};

/** 非 MCP 调用入口使用相同合同校验；失败只报告合同问题。 */
export function requireValid(schema: Schema, value: unknown): void {
  const result = validator.getValidator(schema as JsonSchemaType)(value);
  if (!result.valid) throw new Error(result.errorMessage);
}
