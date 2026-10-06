/*
 * 模块名: API/MCP 性能证据摘要
 * 功能概述: 从已完成成对数据提取时延、真实阶段和持续内存窗口，保留原始证据路径。
 * 对外接口: node summarize.mjs <run-directory> <new-summary-json>。
 * 依赖关系: Node 内置 fs/path。
 * 输入输出: typed/pipe/trace JSON 到紧凑可审阅摘要。
 * 异常与错误: 缺少完整组或证据路径已存在时失败，不生成假数据。
 * 维护说明: 总时延与阶段分位数分开报告，不相加分位数或把差值称解析成本。
 */
import fs from 'node:fs';
import path from 'node:path';
const [runRoot, output] = process.argv.slice(2);
if (!runRoot || !output) throw new Error('Specify completed run directory and new summary output.');
const read = filename => JSON.parse(fs.readFileSync(filename, 'utf8'));
const metadata = read(path.join(runRoot, 'build-metadata.json'));
const official = read(path.join(runRoot, 'official', 'paired-results.json'));
const instrumented = read(path.join(runRoot, 'instrumented', 'paired-results.json'));
const rank = (values, fraction) => [...values].sort((a, b) => a - b)[Math.max(1, Math.ceil(values.length * fraction)) - 1];
const report = { schema: 'mini3d-api-performance-summary-v1', runRoot, metadata, cases: [] };
for (const entry of official.cases) {
  const typed = read(entry.directReport);
  const phaseCase = instrumented.cases.find(candidate => candidate.scenario === entry.scenario && candidate.size === entry.size);
  if (!phaseCase) throw new Error('Missing instrumented counterpart.');
  const phaseTyped = read(phaseCase.directReport);
  const traces = fs.readFileSync(phaseCase.tracePath, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
  const requestPhases = new Map(traces.filter(item => item.kind === 'request').map(item => [item.id, item]));
  const metrics = entry.pipe.map(metric => {
    const direct = typed.direct.find(candidate => candidate.method === metric.method);
    const phases = phaseCase.pipe.find(candidate => candidate.method === metric.method);
    const residue = phases.samples.map(sample => {
      const server = requestPhases.get(sample.id);
      const observed = Object.keys(phases.serverStages).reduce((sum, key) => sum + (server?.[key] ?? 0), 0);
      return sample.ms - observed - sample.jsonEncodeMs - sample.jsonDecodeMs;
    });
    const preQueueWork = phases.samples.map(sample => {
      const server = requestPhases.get(sample.id);
      return ['utf8BudgetJsonMs', 'mutationCanonicalMs', 'canonicalDigestMs', 'ledgerAdmissionMs']
        .reduce((sum, phase) => sum + (server?.[phase] ?? 0), 0);
    });
    const serverStages = Object.fromEntries(Object.entries(phases.serverStages).map(([phase, stats]) => {
      const shares = phases.samples.map(sample => requestPhases.get(sample.id)?.[phase] / sample.ms * 100)
        .filter(value => Number.isFinite(value));
      return [phase, { ...stats, sameSampleTotalShareMedianPercent: rank(shares, .5),
        sameSampleTotalShareP95Percent: rank(shares, .95) }];
    }));
    return { method: metric.method, sampleCount: metric.sampleCount,
      typedMedianMs: direct.medianMs, typedP95Ms: direct.p95Ms,
      pipeMedianMs: metric.medianMs, pipeP95Ms: metric.p95Ms,
      requestFrameBytesMedian: metric.requestFrameBytesMedian, responseFrameBytesMedian: metric.responseFrameBytesMedian,
      instrumentedPipeMedianMs: phases.medianMs, instrumentedPipeP95Ms: phases.p95Ms,
      serverStages,
      sameSampleObservedPreQueueWorkMedianMs: rank(preQueueWork, .5),
      sameSampleObservedPreQueueWorkP95Ms: rank(preQueueWork, .95),
      observedPreQueueWorkMeaning: 'same-request disjoint observed synchronous work before enqueue; excludes unmeasured envelope/validation glue, not end-to-end latency',
      sameSampleUnattributedMedianMs: rank(residue, .5), sameSampleUnattributedP95Ms: rank(residue, .95),
      residualMeaning: 'instrumented per-request total minus disjoint observed phases and measured client JSON; includes transport, frame reassembly, scheduler, admission glue and observer overhead' };
  });
  const sustained = data => ({ windows: data.continuousWindows,
    totalCalls: data.continuousWindows.reduce((sum, window) => sum + window.calls, 0),
    idle: data.idleWindows, ledger: data.ledgerAfterContinuous ?? null, bounds: data.resourceBounds,
    finalFourWindowsPrivateBytes: data.continuousWindows.slice(-4).flatMap(window =>
      [window.privateBytesFirst, window.privateBytesLast]).filter(value => value !== undefined),
    finalFourWindowsWorkingSetBytes: data.continuousWindows.slice(-4).flatMap(window =>
      [window.workingSetBytesFirst, window.workingSetBytesLast]).filter(value => value !== undefined) });
  const captureSampleCount = phaseTyped.direct.find(candidate => candidate.method === 'viewport.capture')?.sampleCount ?? 0;
  report.cases.push({ scenario: entry.scenario, size: entry.size, fixture: typed.fixture,
    fixtureInstallation: typed.fixtureInstallation, bridgePermissions: entry.bridgePermissions ?? null,
    captureFixture: entry.captureFixture ?? null, hardware: { cpu: typed.cpu, logicalProcessors: typed.logicalProcessors,
      os: typed.os, kernel: typed.kernel, gpu: typed.gpu ?? null, qt: typed.qt },
    metrics, officialSustained: sustained(entry), instrumentedSustained: sustained(phaseCase),
    fullVertexScan: entry.fullVertexScan ?? null, captureExample: entry.captureExample ?? null,
    directCaptureStages: traces.filter(item => item.method === 'direct.viewport.capture').slice(-captureSampleCount),
    instrumentedDirectHardware: { cpu: phaseTyped.cpu, gpu: phaseTyped.gpu ?? null } });
}
fs.writeFileSync(output, JSON.stringify(report, null, 2), { flag: 'wx' });
console.log(JSON.stringify({ summary: output, cases: report.cases.length, totalCalls:
  report.cases.reduce((sum, entry) => sum + entry.officialSustained.totalCalls + entry.instrumentedSustained.totalCalls, 0) }));
