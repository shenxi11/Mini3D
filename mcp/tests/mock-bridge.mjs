/*
模块名: mock-bridge
功能概述: 在独有本机管道中提供可注入断线的合同夹具，不操作真实 Mini3D。
对外接口: MockBridge、document、createInput、captureInput、png。
依赖关系: Node net/fs、已编译 adapter 与共享方法目录。
输入输出: 合同 RPC -> 可控响应、账本和合成 PNG 夹具。
异常与错误: 测试负责关闭自己的管道；未处理的夹具异常记录后断开连接。
维护说明: descriptor secret 是测试随机值，不记录 secret 或完整网格。
*/
import { createServer } from 'node:net';
import { randomUUID, randomBytes, createHash } from 'node:crypto';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { join, win32 } from 'node:path';
import { FrameDecoder, encodeFrame } from '../dist/frame.js';
import { apiVersion, limits, methods } from '../dist/schema.js';

export const document = { instanceId: 'cdf82e3f-49f2-461b-a24a-7b5f99c0c701', documentId: '1597846b-bc6c-4566-867c-b963fbe42ace' };
export const state = { document, documentRevision: '1', historyRevision: '1' };
export const transform = { space: 'local', translation: [0, 0, 0], rotationQuaternion: [0, 0, 0, 1], scale: [-2.4, 1.5, 1] };
export const surface = { tint: [0.8, 0.8, 0.8], useVertexColor: false, useTexture: false };
export const createInput = { document, expectedDocumentRevision: '1', primitive: 'cube', name: '中文装甲块', parentId: '0', transform, surface };
export const captureInput = { document, expectedDocumentRevision: '1', expectedViewportRevision: '1', expectedEvaluationId: '0' };
export const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a8WQAAAAASUVORK5CYII=', 'base64');
const matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
export const view = { ...state, viewportRevision: '1', evaluationId: '0', frame: 1, mode: 'base', sessionRevision: '1', preset: 'orbit', projectionMode: 'perspective',
  position: [0, 0, 3], target: [0, 0, 0], forward: [0, 0, -1], up: [0, 1, 0],
  viewMatrix: matrix, projectionMatrix: matrix, previewCameraId: '0', shading: 'material',
  overlays: true, xRay: false, logicalSize: { width: 1, height: 1 }, pixelSize: { width: 1, height: 1 }, devicePixelRatio: 1,
  visibility: { hiddenEntityIds: [], isolatedEntityIds: [], editedEntityId: '0', hiddenVertexCount: 0, hiddenEdgeCount: 0, hiddenFaceCount: 0 } };
export const captureResult = { status: 'captured', captureId: randomUUID(), frameId: '1', contextGeneration: '1', view,
  originalPixelSize: { width: 1, height: 1 }, outputPixelSize: { width: 1, height: 1 }, mimeType: 'image/png', byteLength: png.length,
  sha256: createHash('sha256').update(png).digest('hex'), overlayIncluded: true, capturedRegion: 'gl_viewport', pngBase64: png.toString('base64') };
export const commandResult = { ...state, status: 'committed', created: { entityIds: ['9007199254740993'] },
  affectedEntityIds: ['9007199254740993'], undoable: true, selectionChanged: false };

export class MockBridge {
  requests = [];
  failures = [];
  sockets = new Set();
  session = randomUUID();
  highWater = '0';
  ledger = new Map();
  onRequest;

  constructor() {
    this.descriptor = { instanceId: document.instanceId, bridgeId: randomUUID(), pid: process.pid,
      startedAt: new Date().toISOString(), pipe: `mini3d-${randomUUID()}`, apiVersion, wireVersion: 1,
      secret: randomBytes(32).toString('hex'), permissions: ['scene.read', 'scene.write', 'viewport.observe', 'viewport.control', 'file.read', 'file.write'],
      readRoots: ['E:/CodexTemp'], writeRoots: ['E:/CodexTemp'] };
    this.server = createServer(socket => {
      this.sockets.add(socket);
      socket.on('close', () => this.sockets.delete(socket));
      socket.on('error', () => {});
      const decoder = new FrameDecoder(limits.requestBytes);
      socket.on('data', bytes => {
        try {
          for (const request of decoder.push(bytes)) {
            this.requests.push(request);
            void this.handle(request, socket).catch(error => { this.failures.push(error); socket.destroy(); });
          }
        } catch (error) { this.failures.push(error); socket.destroy(); }
      });
    });
  }

  async start() {
    await new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(`\\\\.\\pipe\\${this.descriptor.pipe}`, resolve);
    });
    return this;
  }

  async descriptorFile() {
    const root = process.env.MINI3D_MCP_TEST_ROOT;
    if (!root || !/^[ed]:[\\/]/i.test(root) || !win32.isAbsolute(root)) throw new Error('Verified E:/D: test root is required');
    this.directory = await mkdtemp(join(root, 'mini3d-mcp-'));
    this.filename = join(this.directory, 'descriptor.json');
    await writeFile(this.filename, JSON.stringify(this.descriptor), { encoding: 'utf8', flag: 'wx' });
    return this.filename;
  }

  bridge(sequence, replayed = false) {
    return { clientSessionId: this.session, mutationSequence: sequence, executionState: 'completed', highWater: this.highWater,
      nextMutationSequence: String(BigInt(this.highWater) + 1n), replayed, committedDocumentRevision: '1', currentState: state };
  }

  commit(request, fragment = { result: commandResult }) {
    this.highWater = request.params.mutationSequence;
    this.ledger.set(this.highWater, fragment);
    return { ...fragment, bridge: this.bridge(this.highWater) };
  }

  error(code, recovery = 'refetch') {
    return { error: { code: -32010, message: code, data: { code, message: code, recovery, ...state } } };
  }

  send(socket, request, fragment) {
    socket.write(encodeFrame({ jsonrpc: '2.0', id: request.id, ...fragment }, limits.responseBytes));
  }

  async handle(request, socket) {
    if (this.onRequest) {
      const result = await this.onRequest(request, socket, this);
      if (result !== null) { if (result !== undefined) this.send(socket, request, result); return; }
    }
    const { method, params } = request;
    let fragment;
    if (method === 'bridge.hello') {
      if (params.secret !== this.descriptor.secret || params.instanceId !== this.descriptor.instanceId || params.bridgeId !== this.descriptor.bridgeId) {
        fragment = this.error('AUTH_FAILED', 'none');
      } else if (params.mode === 'resume' && params.clientSessionId !== this.session) fragment = this.error('SESSION_EXPIRED', 'query_result');
      else fragment = { result: { instanceId: this.descriptor.instanceId, bridgeId: this.descriptor.bridgeId, clientSessionId: this.session,
        highWater: this.highWater, nextMutationSequence: String(BigInt(this.highWater) + 1n), permissions: this.descriptor.permissions } };
    } else if (method === 'bridge.requestStatus') {
      const saved = this.ledger.get(params.mutationSequence);
      fragment = { result: { state: saved ? 'completed' : 'not_seen', highWater: this.highWater,
        nextMutationSequence: String(BigInt(this.highWater) + 1n),
        ...(saved ? { response: saved, bridge: this.bridge(params.mutationSequence, true) } : {}) } };
    } else if (method === 'document.current') fragment = { result: { ...state, path: '', isModified: false, requiresSaveAs: false, busy: false, busyReasons: [] } };
    else if (method === 'scene.getSummary') fragment = { result: { ...state, entityCount: 0, collectionCount: 0, rootIds: [], collections: [] } };
    else if (method === 'history.getState') fragment = { result: { ...state, count: 0, index: 0, cleanIndex: 0, isClean: true, canUndo: false, canRedo: false, undoText: '', redoText: '' } };
    else if (method === 'entity.get') fragment = { result: { ...state, entity: { entityId: params.entityId, parentId: '0', childIds: [],
      name: '中文装甲块', primitive: 'cube', meshId: '9007199254740994', meshKind: 'primitive', transform, worldMatrix: matrix,
      surface, collectionId: '0', camera: null, light: null, visible: true, effectiveVisible: true, viewportVisible: true, bounds: { kind: 'evaluated', local: null, world: null } } } };
    else if (method === 'viewport.getState') fragment = { result: view };
    else if (method === 'viewport.capture') fragment = { result: captureResult };
    else if (method === 'system.describe') fragment = { result: { ...state, apiVersion, wireVersion: 1,
      coordinates: 'right-handed Y-up, scene units', matrixLayout: 'column-major', entityPageDefault: 256, entityPageMaximum: 2048,
      limits, methods: methods.map(entry => ({ name: entry.name, kind: entry.kind, permission: entry.permission, externalEnabled: true })) } };
    else if (method === 'bridge.cancel') fragment = { result: { status: 'cannot_cancel_started' } };
    else fragment = this.commit(request);
    this.send(socket, request, fragment);
  }

  async close() {
    for (const socket of this.sockets) socket.destroy();
    await new Promise(resolve => this.server.close(resolve));
    if (this.directory) await rm(this.directory, { recursive: true });
  }
}
