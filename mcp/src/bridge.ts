/*
模块名: bridge
功能概述: 连接明确指定的本机实例，管理单个写请求和原会话恢复。
对外接口: readDescriptor、BridgeClient、ApiFailure。
依赖关系: Node net/fs、冻结 Schema、私有 wire v1 帧。
输入输出: 严格业务参数 -> 本机 RPC 结果；写入未知结果 -> 保留的恢复状态。
异常与错误: 失联只 resume/requestStatus；任何未确认写结果阻断后续写入。
维护说明: 不扫描实例、不创建新会话重放、不凭领域错误推测序号已消费。
*/
import { open } from 'node:fs/promises';
import { win32 as path } from 'node:path';
import { createConnection, type Socket } from 'node:net';
import { randomUUID } from 'node:crypto';
import { FrameDecoder, encodeFrame, parseBoundedJson } from './frame.js';
import { apiVersion, wireVersion, limits, methods, requireValid, resolveSchema, toolInput,
  type Method, type ObjectValue } from './schema.js';

export interface Descriptor extends ObjectValue {
  instanceId: string; bridgeId: string; pipe: string; secret: string;
}
interface RpcError { code: number; message: string; data?: ObjectValue }
interface RpcResponse { jsonrpc: '2.0'; id: string; result?: ObjectValue; error?: RpcError; bridge?: ObjectValue }
export interface BridgeResult { result: ObjectValue; bridge?: ObjectValue; adapterRecovery?: ObjectValue }
interface PendingWrite { method: Method; params: ObjectValue; sequence: string; id: string }

/** 领域错误与 adapter 错误分开于 MCP 协议错误，业务详情供工具结果保留。 */
export class ApiFailure extends Error {
  constructor(readonly details: ObjectValue, readonly bridge?: ObjectValue, readonly rpcCode?: number) {
    super(String(details.message));
  }
}
function failure(code: string, recovery = 'none'): ApiFailure {
  return new ApiFailure({ code, message: code, recovery });
}
function object(value: unknown): value is ObjectValue {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}
const bridgeSchema = (name: string) => resolveSchema(`bridge.schema.json#/$defs/${name}`);

/** 只读显式绝对路径，以同一文件句柄做大小检查和读取；不猜目标窗口。 */
export async function readDescriptor(filename: string): Promise<Descriptor> {
  if (!path.isAbsolute(filename) || !/^[a-z]:[\\/]/i.test(filename)) throw failure('INVALID_DESCRIPTOR');
  const file = await open(filename, 'r');
  try {
    const stat = await file.stat();
    if (!stat.isFile() || stat.size > limits.requestBytes) throw failure('INVALID_DESCRIPTOR');
    const value = parseBoundedJson(await file.readFile());
    requireValid(bridgeSchema('descriptor'), value);
    const descriptor = value as Descriptor;
    if (!/^mini3d-[0-9a-f-]+$/.test(descriptor.pipe)) throw failure('INVALID_DESCRIPTOR');
    return descriptor;
  } finally {
    await file.close();
  }
}

class PipeRpc {
  private readonly decoder = new FrameDecoder();
  private readonly waiters = new Map<string, { resolve: (response: RpcResponse) => void;
    reject: (error: Error) => void; timer: NodeJS.Timeout }>();
  private open = true;
  private constructor(private readonly socket: Socket) {
    socket.on('data', chunk => {
      try {
        for (const value of this.decoder.push(chunk)) {
          if (!object(value) || value.jsonrpc !== '2.0' || typeof value.id !== 'string' ||
              (Object.hasOwn(value, 'result') === Object.hasOwn(value, 'error')) ||
              (Object.hasOwn(value, 'result') && !object(value.result)) ||
              (Object.hasOwn(value, 'error') && (!object(value.error) ||
                !Number.isInteger(value.error.code) || typeof value.error.message !== 'string'))) {
            throw new Error('Invalid private RPC response');
          }
          const waiter = this.waiters.get(value.id);
          if (!waiter) throw new Error('Unexpected private RPC response id');
          this.waiters.delete(value.id);
          clearTimeout(waiter.timer);
          waiter.resolve(value as unknown as RpcResponse);
        }
      } catch {
        this.fail(new Error('Invalid private RPC frame'));
      }
    });
    socket.on('error', () => this.fail(new Error('Instance connection failed')));
    socket.on('close', () => this.fail(new Error('Instance connection closed')));
  }

  static async connect(pipe: string, signal: AbortSignal): Promise<PipeRpc> {
    const socket = createConnection({ path: `\\\\.\\pipe\\${pipe}`, signal });
    await new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => { socket.destroy(); reject(new Error('Instance connection timed out')); }, limits.mutationTimeoutMs);
      socket.once('connect', () => { clearTimeout(timer); resolve(); });
      socket.once('error', () => { clearTimeout(timer); reject(new Error('Instance unavailable')); });
    });
    if (signal.aborted) { socket.destroy(); throw failure('ADAPTER_CLOSED'); }
    return new PipeRpc(socket);
  }

  get isOpen(): boolean { return this.open; }

  request(method: string, params: ObjectValue, timeoutMs: number, id = randomUUID()): Promise<RpcResponse> {
    const bytes = encodeFrame({ jsonrpc: '2.0', id, method, params });
    if (!this.open) return Promise.reject(new Error('Instance unavailable'));
    if (this.waiters.size >= limits.queuedRequests || this.socket.writableLength + bytes.length > limits.requestBytes + 4) {
      return Promise.reject(new Error('Local transport budget exceeded'));
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => this.fail(new Error('Private RPC deadline elapsed')), timeoutMs);
      this.waiters.set(id, { resolve, reject, timer });
      this.socket.write(bytes, error => { if (error) this.fail(new Error('Private RPC write failed')); });
    });
  }

  private fail(error: Error): void {
    this.open = false;
    this.socket.destroy();
    for (const waiter of this.waiters.values()) { clearTimeout(waiter.timer); waiter.reject(error); }
    this.waiters.clear();
  }
  close(): void { this.fail(new Error('Adapter closed')); }
}

/** 会话与写序号由桥返回；仅 document.current 是显式恢复未决请求的入口。 */
export class BridgeClient {
  private rpc: PipeRpc | undefined;
  private connecting: Promise<PipeRpc> | undefined;
  private connectingAbort: AbortController | undefined;
  private session: string | undefined;
  private nextSequence = '1';
  private highWater = '0';
  private newAttempted = false;
  private closed = false;
  private writing = false;
  private pending: PendingWrite | undefined;

  constructor(private readonly descriptor: Descriptor) { requireValid(bridgeSchema('descriptor'), descriptor); }

  /** 同时取消 native connect/hello 和已建立连接；不回滚已确认命令。 */
  close(): void { this.closed = true; this.connectingAbort?.abort(); this.rpc?.close(); }

  private async connect(): Promise<PipeRpc> {
    if (this.closed) throw failure('ADAPTER_CLOSED');
    if (this.rpc?.isOpen) return this.rpc;
    if (this.connecting) return this.connecting;
    const controller = new AbortController();
    this.connectingAbort = controller;
    this.connecting = (async () => {
      if (!this.session && this.newAttempted) throw failure('SESSION_EXPIRED', 'refetch');
      let rpc: PipeRpc | undefined;
      try {
        rpc = await PipeRpc.connect(this.descriptor.pipe, controller.signal);
        if (this.closed) throw failure('ADAPTER_CLOSED');
        const resume = !!this.session;
        this.newAttempted = true;
        const response = await rpc.request('bridge.hello', {
          mode: resume ? 'resume' : 'new', instanceId: this.descriptor.instanceId,
          bridgeId: this.descriptor.bridgeId, secret: this.descriptor.secret, apiVersion, wireVersion,
          ...(resume ? { clientSessionId: this.session } : {})
        }, limits.mutationTimeoutMs);
        if (this.closed) throw failure('ADAPTER_CLOSED');
        const result = this.domain(response);
        requireValid(bridgeSchema('helloResult'), result);
        if (String(result.instanceId).toLowerCase() !== this.descriptor.instanceId.toLowerCase() ||
            String(result.bridgeId).toLowerCase() !== this.descriptor.bridgeId.toLowerCase() ||
            (resume && String(result.clientSessionId).toLowerCase() !== this.session)) throw failure('INSTANCE_MISMATCH');
        this.acceptWatermark(result);
        this.session = String(result.clientSessionId).toLowerCase();
        this.rpc = rpc;
        return rpc;
      } catch (error) {
        rpc?.close();
        if (this.closed) throw failure('ADAPTER_CLOSED');
        throw error;
      }
    })();
    try { return await this.connecting; }
    finally { this.connecting = undefined; this.connectingAbort = undefined; }
  }

  private acceptWatermark(value: ObjectValue): void {
    requireValid(resolveSchema('common.schema.json#/$defs/uint64'), value.highWater);
    requireValid(resolveSchema('common.schema.json#/$defs/id'), value.nextMutationSequence);
    const highWater = BigInt(String(value.highWater));
    const next = highWater === 18446744073709551615n ? highWater : highWater + 1n;
    if (highWater < BigInt(this.highWater) || String(next) !== value.nextMutationSequence) throw failure('INVALID_BRIDGE_STATE');
    this.highWater = String(value.highWater);
    this.nextSequence = String(value.nextMutationSequence);
  }

  private domain(response: RpcResponse): ObjectValue {
    if (response.error) {
      if (response.error.data) {
        requireValid(resolveSchema('common.schema.json#/$defs/error'), response.error.data);
        throw new ApiFailure(response.error.data, response.bridge, response.error.code);
      }
      throw new ApiFailure({ code: 'PRIVATE_RPC_ERROR', message: response.error.message, recovery: 'none' }, undefined, response.error.code);
    }
    if (!response.result) throw failure('INVALID_BRIDGE_RESPONSE');
    return response.result;
  }

  private completed(pending: PendingWrite, response: RpcResponse): BridgeResult {
    const bridge = response.bridge;
    if (!bridge) throw failure('INVALID_BRIDGE_STATE');
    requireValid(bridgeSchema('bridgeMetadata'), bridge);
    if (bridge.clientSessionId !== this.session || bridge.mutationSequence !== pending.sequence ||
        bridge.executionState !== 'completed' || BigInt(String(bridge.highWater)) < BigInt(pending.sequence)) {
      throw failure('INVALID_BRIDGE_STATE');
    }
    if (response.error?.data) requireValid(resolveSchema('common.schema.json#/$defs/error'), response.error.data);
    else if (response.error) throw failure('INVALID_BRIDGE_RESPONSE');
    else requireValid(resolveSchema(pending.method.resultSchema), response.result);
    this.acceptWatermark(bridge);
    this.pending = undefined;
    return { result: this.domain(response), bridge };
  }

  private unknown(pending: PendingWrite): ApiFailure {
    return new ApiFailure({ code: 'OUTCOME_UNKNOWN', message: 'outcome_unknown：请重查 document.current 确认原请求；后续写入已暂停。',
      recovery: 'query_result', mutationSequence: pending.sequence, method: pending.method.name });
  }

  private async recover(pending: PendingWrite, explicit: boolean): Promise<{ outcome?: BridgeResult; adapterRecovery: ObjectValue; error?: ApiFailure }> {
    const rpc = await this.connect();
    const response = await rpc.request('bridge.requestStatus', { clientSessionId: this.session, mutationSequence: pending.sequence }, limits.mutationTimeoutMs);
    const status = this.domain(response);
    requireValid(bridgeSchema('requestStatusResult'), status);
    const adapterRecovery: ObjectValue = { source: 'adapter', method: pending.method.name, mutationSequence: pending.sequence, state: status.state };
    if (status.state === 'completed') {
      if (!object(status.response) || !object(status.bridge)) throw this.unknown(pending);
      if (status.highWater !== status.bridge.highWater || status.nextMutationSequence !== status.bridge.nextMutationSequence) throw this.unknown(pending);
      const original = { jsonrpc: '2.0', id: pending.id, ...status.response, bridge: status.bridge } as RpcResponse;
      try {
        const outcome = this.completed(pending, original);
        adapterRecovery.response = outcome.result;
        adapterRecovery.bridge = outcome.bridge;
        return { outcome, adapterRecovery };
      } catch (error) {
        if (!this.pending && error instanceof ApiFailure) {
          adapterRecovery.error = error.details;
          adapterRecovery.bridge = error.bridge;
          return { error, adapterRecovery };
        }
        throw error;
      }
    }
    if (status.state === 'result_expired' && BigInt(String(status.highWater)) >= BigInt(pending.sequence)) {
      this.acceptWatermark(status);
      this.pending = undefined;
      const error = failure('RESULT_EXPIRED', 'refetch');
      adapterRecovery.highWater = status.highWater;
      return { error, adapterRecovery };
    }
    if (explicit && status.state === 'not_seen' && String(status.highWater) === this.highWater && status.nextMutationSequence === pending.sequence) {
      this.acceptWatermark(status);
      this.pending = undefined;
      return { adapterRecovery };
    }
    throw this.unknown(pending);
  }

  private cancel(target: ObjectValue): void {
    // 仅在原连接仍可用时取消；未确认的取消不改变本地账本状态。
    if (this.rpc?.isOpen && this.session) {
      void this.rpc.request('bridge.cancel', { clientSessionId: this.session, target }, limits.mutationTimeoutMs).catch(() => {});
    }
  }

  /** 输入取自同一方法合同；写入保留原参数，SDK 取消仅触发 best-effort cancel。 */
  async call(name: string, params: ObjectValue, signal?: AbortSignal): Promise<BridgeResult> {
    const method = methods.find(entry => entry.name === name);
    if (!method) throw failure('METHOD_NOT_FOUND');
    requireValid(toolInput(method), params);
    if (signal?.aborted) throw failure('CANCELLED');
    if (method.kind !== 'query' && this.writing) throw failure('WRITE_IN_FLIGHT', 'wait');
    let adapterRecovery: ObjectValue | undefined;
    if (this.pending) {
      if (name === 'document.current' && !this.writing) {
        try { adapterRecovery = (await this.recover(this.pending, true)).adapterRecovery; }
        catch { adapterRecovery = { source: 'adapter', state: 'outcome_unknown', method: this.pending?.method.name, mutationSequence: this.pending?.sequence }; }
      } else if (method.kind !== 'query') throw this.unknown(this.pending);
    }
    if (method.kind !== 'query') {
      this.writing = true;
      let pending: PendingWrite | undefined;
      let abort: (() => void) | undefined;
      let cancelTimer: NodeJS.Timeout | undefined;
      try {
        const rpc = await this.connect();
        if (this.closed) throw failure('ADAPTER_CLOSED');
        if (signal?.aborted) throw failure('CANCELLED');
        if (this.highWater === '18446744073709551615') throw failure('SEQUENCE_EXHAUSTED');
        const id = randomUUID();
        const ownedParams = { ...structuredClone(params), clientSessionId: this.session, mutationSequence: this.nextSequence };
        encodeFrame({ jsonrpc: '2.0', id, method: name, params: ownedParams });
        pending = { method, params: ownedParams, sequence: this.nextSequence, id };
        this.pending = pending;
        const request = rpc.request(name, pending.params, Number(params.timeoutMs ?? limits.mutationTimeoutMs) + 1000, id);
        abort = () => this.cancel({ kind: 'mutation', mutationSequence: pending!.sequence });
        cancelTimer = setTimeout(abort, Number(params.timeoutMs ?? limits.mutationTimeoutMs));
        signal?.addEventListener('abort', abort, { once: true });
        return this.completed(pending, await request);
      } catch (error) {
        if (!pending || !this.pending) throw error;
        try {
          const recovered = await this.recover(pending, false);
          if (recovered.error) throw recovered.error;
          if (recovered.outcome) return recovered.outcome;
        } catch (recoveryError) {
          if (!this.pending) throw recoveryError;
        }
        throw this.unknown(pending);
      } finally { this.writing = false; clearTimeout(cancelTimer); if (abort) signal?.removeEventListener('abort', abort); }
    }
    const rpc = await this.connect();
    if (this.closed) throw failure('ADAPTER_CLOSED');
    const id = randomUUID();
    const abort = () => this.cancel({ kind: 'request', requestId: id });
    const timeoutMs = Number(params.timeoutMs ?? (name === 'viewport.capture' ? limits.captureTimeoutMs : limits.mutationTimeoutMs));
    let cancelTimer: NodeJS.Timeout | undefined;
    if (name === 'viewport.capture') signal?.addEventListener('abort', abort, { once: true });
    try {
      if (signal?.aborted) throw failure('CANCELLED');
      const request = rpc.request(name, params, timeoutMs + 1000, id);
      if (name === 'viewport.capture') cancelTimer = setTimeout(abort, timeoutMs);
      const response = await request;
      const result = this.domain(response);
      requireValid(resolveSchema(method.resultSchema), result);
      return { result, ...(adapterRecovery ? { adapterRecovery } : {}) };
    } finally { clearTimeout(cancelTimer); signal?.removeEventListener('abort', abort); }
  }
}
