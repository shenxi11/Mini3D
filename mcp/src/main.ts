#!/usr/bin/env node
/*
模块名: main
功能概述: 官方 SDK stdio 入口，仅连接明确指定的 Mini3D 描述文件。
对外接口: node dist/main.js --descriptor <绝对路径>。
依赖关系: 官方 serveStdio 兼容层、BridgeClient、工具目录。
输入输出: 宿主 stdio 协议；诊断只写 stderr，secret 不进入命令行或日志。
异常与错误: 参数/描述文件无效则退出非零，不扫描或接管其他窗口。
维护说明: 宿主退出只关闭 adapter 自有连接，不关闭 Mini3D。
*/
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { BridgeClient, readDescriptor } from './bridge.js';
import { createServer } from './tools.js';

async function main(): Promise<void> {
  const [major, minor] = process.versions.node.split('.').map(Number);
  if (major !== 24 || minor! < 13) throw new Error('Unsupported Node version');
  const args = process.argv.slice(2);
  if (args.length !== 2 || args[0] !== '--descriptor' || !args[1]) throw new Error('Explicit descriptor is required');
  const bridge = new BridgeClient(await readDescriptor(args[1]));
  const handle = serveStdio(() => createServer(bridge), {
    onerror: () => process.stderr.write('Mini3D MCP：协议处理失败。\n')
  });
  const close = () => { bridge.close(); void handle.close(); };
  process.stdin.once('end', close);
  process.once('SIGINT', close);
  process.once('SIGTERM', close);
}

main().catch(() => {
  process.stderr.write('Mini3D MCP：启动失败，请检查 Node 版本、--descriptor 绝对路径与冻结合同。\n');
  process.exitCode = 1;
});
