/*
模块名: frame
功能概述: 实现本机桥的有界长度帧，MCP stdio 不使用此编码。
对外接口: FrameDecoder、encodeFrame、parseBoundedJson。
依赖关系: Node Buffer/TextDecoder；限额仅取 methods.json。
输入输出: 分片字节流 -> 严格 UTF-8 JSON 值；JSON -> uint32 LE 帧。
异常与错误: 零长度、超帧、无效 UTF-8、深度/节点超限立即拒绝。
维护说明: 在 JSON.parse 分配对象树前执行结构预算检查。
*/
import { limits } from './schema.js';

/** 先计结构预算，再交由 JSON.parse 检查语法，不构造无界对象树。 */
export function parseBoundedJson(bytes: Uint8Array): unknown {
  const text = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes);
  let depth = 0;
  let nodes = 0;
  for (let index = 0; index < text.length; index++) {
    const char = text[index]!;
    if (char === '"') {
      for (index++; index < text.length; index++) {
        if (text[index] === '\\') index++;
        else if (text[index] === '"') break;
      }
      let next = index + 1;
      while (next < text.length && /\s/.test(text[next]!)) next++;
      if (text[next] !== ':') nodes++;
    } else if (char === '{' || char === '[') {
      nodes++;
      depth++;
    } else if (char === '}' || char === ']') depth--;
    else if (!/[\s,:]/.test(char)) {
      nodes++;
      while (index + 1 < text.length && !/[\s,\]}:]/.test(text[index + 1]!)) index++;
    }
    if (depth > limits.jsonDepth || nodes > limits.jsonNodes) throw new Error('JSON budget exceeded');
  }
  return JSON.parse(text) as unknown;
}

/** 一个解码器对应一个连接；仅在验证长度后为该帧分配 payload。 */
export class FrameDecoder {
  private header = Buffer.alloc(4);
  private headerUsed = 0;
  private body: Buffer | undefined;
  private bodyUsed = 0;

  constructor(private readonly maximumBytes = limits.responseBytes) {}

  push(chunk: Buffer): unknown[] {
    const messages: unknown[] = [];
    let offset = 0;
    while (offset < chunk.length) {
      if (!this.body) {
        const size = Math.min(4 - this.headerUsed, chunk.length - offset);
        chunk.copy(this.header, this.headerUsed, offset, offset + size);
        offset += size;
        this.headerUsed += size;
        if (this.headerUsed < 4) continue;
        const length = this.header.readUInt32LE();
        if (!length || length > this.maximumBytes) throw new Error('Frame length is invalid');
        this.body = Buffer.allocUnsafe(length);
        this.bodyUsed = 0;
        this.headerUsed = 0;
      }
      const size = Math.min(this.body.length - this.bodyUsed, chunk.length - offset);
      chunk.copy(this.body, this.bodyUsed, offset, offset + size);
      offset += size;
      this.bodyUsed += size;
      if (this.bodyUsed === this.body.length) {
        messages.push(parseBoundedJson(this.body));
        this.body = undefined;
      }
      if (messages.length > limits.queuedRequests) throw new Error('Too many responses in one chunk');
    }
    return messages;
  }

  /** 断开时检查半包，不把残帧当作可恢复的完整响应。 */
  finish(): void {
    if (this.headerUsed || this.body) throw new Error('Connection ended inside a frame');
  }
}

/** 对出站数据执行相同结构预算与请求字节预算。 */
export function encodeFrame(value: unknown, maximumBytes = limits.requestBytes): Buffer {
  const bytes = Buffer.from(JSON.stringify(value), 'utf8');
  if (!bytes.length || bytes.length > maximumBytes) throw new Error('Frame length is invalid');
  parseBoundedJson(bytes);
  const header = Buffer.allocUnsafe(4);
  header.writeUInt32LE(bytes.length);
  return Buffer.concat([header, bytes]);
}
