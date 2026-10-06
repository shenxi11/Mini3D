/*
模块名: tools
功能概述: 按方法目录注册明确的 MCP 工具，传递业务错误和真实图片字节。
对外接口: toolName、createServer。
依赖关系: 官方 MCP server、共享 Schema、BridgeClient。
输入输出: MCP 调用 -> 对应单个 RPC；成功结构化领域结果或 isError。
异常与错误: 保留 code/recovery/revision；图片哈希或尺寸不匹配拒绝呈现。
维护说明: adapter 恢复/账本信息独立 text content，不冒充领域响应字段。
*/
import { createHash } from 'node:crypto';
import { McpServer, fromJsonSchema, type CallToolResult, type JsonSchemaType } from '@modelcontextprotocol/server';
import { ApiFailure, BridgeClient, type BridgeResult } from './bridge.js';
import { apiVersion, limits, methods, toolInput, toolOutput, validator, type Method, type ObjectValue } from './schema.js';

/** 名称只由稳定 RPC 名称机械映射；每个工具对应一个明确定义的方法。 */
export function toolName(name: string): string {
  return `mini3d_${name.replaceAll('.', '_').replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase()}`;
}

function toolResult(method: Method, response: BridgeResult): CallToolResult {
  const structuredContent = { ...response.result };
  const summaries: Record<string, string> = { committed: '已提交', no_change: '无变化', saved: '已保存', opened: '已打开', captured: '已截图' };
  const content: CallToolResult['content'] = [{ type: 'text', text: `${method.name}：${summaries[String(structuredContent.status)] ?? '查询完成'}` }];
  if (method.name === 'viewport.capture') {
    const encoded = String(structuredContent.pngBase64);
    if (encoded.length > Math.ceil(limits.capturePngBytes / 3) * 4 || encoded.length % 4 !== 0 ||
        !/^[A-Za-z0-9+/]+={0,2}$/.test(encoded)) throw new ApiFailure({ code: 'INVALID_IMAGE', message: '截图编码无效', recovery: 'refetch' });
    const bytes = Buffer.from(encoded, 'base64');
    const size = structuredContent.outputPixelSize as ObjectValue;
    if (bytes.length < 24 || bytes.toString('base64') !== encoded || bytes.length !== structuredContent.byteLength ||
        bytes.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a' ||
        createHash('sha256').update(bytes).digest('hex') !== structuredContent.sha256 ||
        bytes.readUInt32BE(16) !== size.width || bytes.readUInt32BE(20) !== size.height ||
        Math.max(Number(size.width), Number(size.height)) > limits.captureLongestEdge! ||
        Number(size.width) * Number(size.height) > limits.capturePixels!) {
      throw new ApiFailure({ code: 'INVALID_IMAGE', message: '截图字节与元数据不匹配', recovery: 'refetch' });
    }
    delete structuredContent.pngBase64;
    content.unshift({ type: 'image', data: encoded, mimeType: 'image/png' });
  }
  if (response.bridge) content.push({ type: 'text', text: JSON.stringify({ source: 'adapter', bridge: response.bridge }) });
  if (response.adapterRecovery) content.push({ type: 'text', text: JSON.stringify({ adapterRecovery: response.adapterRecovery }) });
  return { content, structuredContent };
}

/** 注册静态工具合同，SDK 负责协议生命周期、输入验证与调用取消。 */
export function createServer(bridge: BridgeClient): McpServer {
  const server = new McpServer({ name: 'mini3d-mcp', version: apiVersion }, {
    capabilities: { tools: {} }, maxToolInputElements: limits.jsonNodes
  });
  for (const method of methods) {
    const inputSchema = toolInput(method);
    const description = `${method.name}；${String(inputSchema.description ?? '显式目标和版本由参数指定。')}`;
    server.registerTool(toolName(method.name), {
      description,
      inputSchema: fromJsonSchema<ObjectValue>(inputSchema as JsonSchemaType, validator),
      outputSchema: fromJsonSchema<ObjectValue>(toolOutput(method) as JsonSchemaType, validator),
      annotations: { readOnlyHint: method.kind === 'query', destructiveHint: method.kind !== 'query',
        idempotentHint: method.kind === 'query', openWorldHint: false }
    }, async (input, context) => {
      try { return toolResult(method, await bridge.call(method.name, input, context.mcpReq.signal)); }
      catch (error) {
        if (!(error instanceof ApiFailure)) throw error;
        return { isError: true, content: [{ type: 'text', text: JSON.stringify({ error: error.details,
          ...(error.bridge ? { bridge: error.bridge } : {}), ...(error.rpcCode !== undefined ? { rpcCode: error.rpcCode } : {}) }) }],
          structuredContent: { error: error.details, ...(error.bridge ? { bridge: error.bridge } : {}) } };
      }
    });
  }
  server.server.onclose = () => bridge.close();
  return server;
}
