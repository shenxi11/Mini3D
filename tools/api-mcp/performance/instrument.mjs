/*
 * 模块名: 性能测试副本生成
 * 功能概述: 对冻结源码生成少量测试计时点，所有输出位于调用者验证的 scratch。
 * 对外接口: node instrument.mjs <repository> <new-output-directory>。
 * 依赖关系: Node 内置 fs/path/crypto。
 * 输入输出: 生产源码到编译专用副本和 SHA256 manifest。
 * 异常与错误: 替换锚点缺失或重复立即失败，不猜测源码变化。
 * 维护说明: 不改生产文件、不加协议字段或线程，计时不覆盖 socket 传输。
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const [root, destination] = process.argv.slice(2);
if (!root || !destination || fs.existsSync(destination))
  throw new Error('Specify repository and a new generated directory.');
fs.mkdirSync(destination, { recursive: true });
const manifest = { version: 'm7-phase-trace-v2', sources: [] };
const replaceOnce = (source, from, to) => {
  if (source.split(from).length !== 2) throw new Error(`Instrumentation anchor drift: ${from.slice(0, 90)}`);
  return source.replace(from, to);
};
function generate(relative, modify) {
  const bytes = fs.readFileSync(path.join(root, relative));
  let source = bytes.toString('utf8').replaceAll('\r\n', '\n');
  if (!source.includes('\n#include ')) throw new Error('Source has no include anchor.');
  source = source.replace('\n#include ', '\n#include "PerformanceTrace.h"\n#include ');
  source = modify(source);
  const name = path.basename(relative);
  fs.writeFileSync(path.join(destination, name), source, 'utf8');
  manifest.sources.push({ path: relative,
    productionSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    instrumentedSha256: crypto.createHash('sha256').update(source).digest('hex') });
}
generate('src/editor/automation/AutomationFrame.cpp', s => {
  s = replaceOnce(s, '        if (!isStrictUtf8(body_)) {',
    '        const auto perfJsonStarted = mini3d::performance::nowNs();\n        if (!isStrictUtf8(body_)) {');
  return replaceOnce(s, '        body_.clear();\n        bodyLength_ = 0;',
    '        mini3d::performance::incomingJsonMs = double(mini3d::performance::nowNs() - perfJsonStarted) / 1e6;\n        body_.clear();\n        bodyLength_ = 0;');
});
generate('src/editor/automation/LocalAutomationBridge.cpp', s => {
  s = replaceOnce(s, '        auto frame = encodeFrame(fragment);',
    '        mini3d::performance::activeId = id.toString();\n        const auto perfEncodeStarted = mini3d::performance::nowNs();\n        auto frame = encodeFrame(fragment);\n        mini3d::performance::record("wireEncodeMs", double(mini3d::performance::nowNs() - perfEncodeStarted) / 1e6);');
  s = replaceOnce(s, '        writeOutput(connection);\n    }\n    void writeOutput',
    '        writeOutput(connection);\n        mini3d::performance::complete(id.toString(), frame.size(), queue_.size(), ledger_.cachedBytes(), ledger_.sessionCount());\n    }\n    void writeOutput');
  s = replaceOnce(s, '        const auto method = object["method"].toString();\n        const auto id = object["id"];',
    '        const auto method = object["method"].toString();\n        const auto id = object["id"];\n        mini3d::performance::begin(id.toString(), method);');
  s = replaceOnce(s, '        queue_.push_back(std::move(request));',
    '        mini3d::performance::queued(request->id.toString());\n        queue_.push_back(std::move(request));');
  s = replaceOnce(s, '        const auto normalized = request->method.startsWith("viewport.")',
    '        const auto perfCanonicalStarted = mini3d::performance::nowNs();\n        const auto normalized = request->method.startsWith("viewport.")');
  s = replaceOnce(s, '        // 信封合法但领域解码失败仍须终结；无法形成 DTO 时保留确定性失败摘要。',
    '        mini3d::performance::record("mutationCanonicalMs", double(mini3d::performance::nowNs() - perfCanonicalStarted) / 1e6);\n        // 信封合法但领域解码失败仍须终结；无法形成 DTO 时保留确定性失败摘要。');
  s = replaceOnce(s, '        const auto admission = ledger_.accept(request->sessionId, request->sequence,',
    '        const auto perfDigestStarted = mini3d::performance::nowNs();\n        const auto perfDigest = canonicalDigest(request->method, digestParams);\n        mini3d::performance::record("canonicalDigestMs", double(mini3d::performance::nowNs() - perfDigestStarted) / 1e6);\n        const auto perfLedgerStarted = mini3d::performance::nowNs();\n        const auto admission = ledger_.accept(request->sessionId, request->sequence,');
  s = replaceOnce(s, 'canonicalDigest(request->method, digestParams),\n                                                int(timeout), acceptedAt);',
    'perfDigest,\n                                                int(timeout), acceptedAt);\n        mini3d::performance::record("ledgerAdmissionMs", double(mini3d::performance::nowNs() - perfLedgerStarted) / 1e6);');
  s = replaceOnce(s, '        queue_.push_back(request);',
    '        mini3d::performance::queued(request->id.toString());\n        queue_.push_back(request);');
  s = replaceOnce(s, '        queue_.erase(eligible);',
    '        queue_.erase(eligible);\n        mini3d::performance::start(request->id.toString());');
  s = replaceOnce(s, '        ledger_.complete(request->sessionId, request->sequence, response, committed, now());',
    '        const auto perfLedgerCompleteStarted = mini3d::performance::nowNs();\n        ledger_.complete(request->sessionId, request->sequence, response, committed, now());\n        mini3d::performance::record("ledgerCompleteMs", double(mini3d::performance::nowNs() - perfLedgerCompleteStarted) / 1e6);');
  return replaceOnce(s, '        const auto callback = [this, alive, request](QJsonObject response) {',
    '        const auto callback = [this, alive, request](QJsonObject response) {\n            mini3d::performance::activeId = request->id.toString();');
});
generate('src/editor/api/ApiJsonCodec.cpp', s => {
  s = replaceOnce(s, '    auto decoded = decodeRequest(method, params);',
    '    const auto perfDecodeStarted = mini3d::performance::nowNs();\n    auto decoded = decodeRequest(method, params);\n    mini3d::performance::record("typedDecodeMs", double(mini3d::performance::nowNs() - perfDecodeStarted) / 1e6);');
  for (const call of ['service.currentDocument()', 'service.listEntities(std::get<EntityListRequest>(request))',
    'service.createMesh(std::get<MeshCreateRequest>(request))',
    'service.meshSummary(std::get<MeshTargetRequest>(request))',
    'service.readSourcePage(std::get<MeshSourcePageRequest>(request))',
    'service.updateEntity(std::get<EntityUpdateRequest>(request))',
    'service.newDocument(std::get<DocumentNewRequest>(request))']) {
    s = replaceOnce(s, `return response(${call});`,
      `return mini3d::performance::domainAndEncode([&] { return ${call}; }, [](const auto& result) { return response(result); });`);
  }
  return replaceOnce(s, 'return response(\n            service.transformComponents(std::get<MeshTransformComponentsRequest>(request)));',
    'return mini3d::performance::domainAndEncode([&] { return service.transformComponents(std::get<MeshTransformComponentsRequest>(request)); }, [](const auto& result) { return response(result); });');
});
generate('src/editor/observation/ObservationService.cpp', s => {
  s = replaceOnce(s, '    pending->elapsed.start();',
    '    pending->elapsed.start();\n    mini3d::performance::captureId = mini3d::performance::activeId;');
  s = replaceOnce(s, '    auto captured = viewport_->grabStampedFramebuffer();',
    '    mini3d::performance::activeId = mini3d::performance::captureId;\n    mini3d::performance::record("captureFrameWaitMs", double(pending_->elapsed.nsecsElapsed()) / 1e6);\n    const auto perfReadbackStarted = mini3d::performance::nowNs();\n    auto captured = viewport_->grabStampedFramebuffer();\n    mini3d::performance::record("framebufferGrabIncludingPossiblePaintMs", double(mini3d::performance::nowNs() - perfReadbackStarted) / 1e6);');
  s = replaceOnce(s, '    auto image = std::move(captured->image);',
    '    const auto perfScaleStarted = mini3d::performance::nowNs();\n    auto image = std::move(captured->image);');
  s = replaceOnce(s, '    QBuffer buffer(&result.png);',
    '    mini3d::performance::record("imageScaleMs", double(mini3d::performance::nowNs() - perfScaleStarted) / 1e6);\n    const auto perfPngStarted = mini3d::performance::nowNs();\n    QBuffer buffer(&result.png);');
  s = replaceOnce(s, '    if (std::size_t(result.png.size()) > api::limits::capturePngBytes) {',
    '    mini3d::performance::record("pngEncodeMs", double(mini3d::performance::nowNs() - perfPngStarted) / 1e6);\n    if (std::size_t(result.png.size()) > api::limits::capturePngBytes) {');
  return replaceOnce(s, '                           callback(ObservationJsonCodec::response(result));',
    '                           const auto perfResultStarted = mini3d::performance::nowNs();\n                           const auto response = ObservationJsonCodec::response(result);\n                           mini3d::performance::record("captureDtoBase64EncodeMs", double(mini3d::performance::nowNs() - perfResultStarted) / 1e6);\n                           callback(response);');
});
fs.writeFileSync(path.join(destination, 'instrumentation.json'), JSON.stringify(manifest, null, 2), 'utf8');
console.log(JSON.stringify({ version: manifest.version, copies: manifest.sources.length }));
