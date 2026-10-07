/*
模块名: files.test
功能概述: 验证M5-04工具共用文件合同、显式覆盖/丢弃与单次请求映射。
对外接口: Node test runner。
依赖关系: 官方MCP client、正式adapter、共享样例、MockBridge。
输入输出: 9/19样例和预置完整结果到合同/序号/错误断言。
异常与错误: 合同漂移或输入错误接触桥即失败。
维护说明: 不模拟文件IO或事务，不替代真实本体文件验收。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {Client} from '@modelcontextprotocol/client';
import {StdioClientTransport} from '@modelcontextprotocol/client/stdio';
import {BridgeClient} from '../dist/bridge.js';
import {methods,limits,toolInput,toolOutput,requireValid,validator} from '../dist/schema.js';
import {toolName} from '../dist/tools.js';
import {MockBridge,commandResult} from './mock-bridge.mjs';

const samples=JSON.parse(readFileSync(new URL('../../api/schema/m5-files-examples.json',import.meta.url),'utf8'));
const names=['file.save','file.importGltf','file.exportObj','document.new','document.open'];
const selected=names.map(name=>methods.find(method=>method.name===name));
const main=fileURLToPath(new URL('../dist/main.js',import.meta.url));
const cwd=fileURLToPath(new URL('..',import.meta.url));
const invalid=[...samples.invalid,...samples.valid.flatMap(sample=>[
  {...sample,params:{...sample.params,clientSessionId:'11111111-1111-4111-8111-111111111111'}},
  {...sample,params:{...sample.params,mutationSequence:'1'}},
  {...sample,params:{...sample.params,extra:true}},
  {...sample,params:{...sample.params,document:{...sample.params.document,extra:true}}}
])];
function responseFor(sample){
  const result={...commandResult,document:sample.params.document,documentRevision:'2',historyRevision:'3',
    created:{entityIds:[]},affectedEntityIds:[],undoable:false,selectionChanged:false,
    status:sample.method.startsWith('document.')?'opened':'saved',path:sample.params.path??'E:/approved/当前工程.m3dscene'};
  if(sample.method==='document.new') result.path='';
  if(sample.method==='file.importGltf') Object.assign(result,{status:'committed',undoable:true,
    created:{entityIds:['9007199254740993','9007199254740994']},affectedEntityIds:['9007199254740993','9007199254740994'],
    rootEntityId:'9007199254740993',warnings:['仅静态资源']});
  if(sample.method==='file.exportObj') Object.assign(result,{entityId:sample.params.entityId,mode:sample.params.mode,byteLength:123});
  return result;
}
const successes=samples.valid.map(sample=>({...sample,result:responseFor(sample)}));
successes.push({...successes[2],result:{...successes[2].result,futureResult:true,warnings:['保留扩展']}});
const errors=[['file.save','OVERWRITE_DENIED'],['file.importGltf','PATH_DENIED'],['file.exportObj','LIMIT_EXCEEDED'],['document.new','UNSAVED_CHANGES'],['document.open','IO_ERROR']].map(([method,code])=>{
  const sample=samples.valid.find(item=>item.method===method);
  return {...sample,error:{code,message:'预置业务拒绝',recovery:'none',document:sample.params.document,documentRevision:'1',historyRevision:'1'}};
});
function reply(mock){
  const queue=[...successes,...errors];let offset=0;
  return request=>{
    if(!names.includes(request.method))return null;
    const sample=queue[offset++];assert(sample);assert.equal(request.method,sample.method);
    return mock.commit(request,sample.error?{error:{code:-32010,message:sample.error.message,data:sample.error}}:{result:sample.result});
  };
}
test('M5-04合同9/19、61方法和独立文件限额；显式覆盖/丢弃，顶层/嵌套输入严格',()=>{
  assert.equal(methods.length,61);assert.equal(samples.valid.length,9);assert.equal(samples.invalid.length,19);
  assert.equal(limits.fileReadBytes,67108864);assert.equal(limits.fileReadDependencies,128);
  assert.equal(limits.importedEntities,2048);assert.equal(limits.exportObjBytes,67108864);
  for(const sample of samples.valid)requireValid(toolInput(methods.find(method=>method.name===sample.method)),sample.params);
  for(const sample of invalid)assert.throws(()=>requireValid(toolInput(methods.find(method=>method.name===sample.method)),sample.params));
  const saveAs=methods.find(method=>method.name==='file.saveAs');
  const saveAsInput={...samples.valid[0].params,path:'E:/approved/另存新工程.m3dscene'};
  requireValid(toolInput(saveAs),saveAsInput);
  for(const overwrite of [false,true])assert.throws(()=>requireValid(toolInput(saveAs),{...saveAsInput,overwrite}));
  assert.equal(selected[0].permission,'file.write');assert.equal(selected[1].permission,'file.read');
  assert.equal(selected[2].permission,'file.write');assert.equal(selected[3].permission,'scene.write');
});
test('M5-04flat完整输出、导入全部身份、OBJ明确目标/模式/字节；扩展字段保留',()=>{
  for(const sample of successes){
    const schema=toolOutput(methods.find(method=>method.name===sample.method));
    requireValid(schema,sample.result);
    for(const field of ['document','documentRevision','historyRevision','status','created','affectedEntityIds','undoable','selectionChanged']){
      const missing=structuredClone(sample.result);delete missing[field];assert.throws(()=>requireValid(schema,missing));
    }
    if(sample.method==='file.importGltf')for(const field of ['path','rootEntityId','warnings']){
      const missing=structuredClone(sample.result);delete missing[field];assert.throws(()=>requireValid(schema,missing));
    }
    if(sample.method==='file.exportObj')for(const field of ['path','entityId','mode','byteLength']){
      const missing=structuredClone(sample.result);delete missing[field];assert.throws(()=>requireValid(schema,missing));
    }
  }
});
test('M5-04非法输入零连接；每个成功/拒绝只一条RPC，原会话序号连续且参数不变',async t=>{
  const mock=await new MockBridge().start(),client=new BridgeClient(mock.descriptor);mock.onRequest=reply(mock);
  t.after(async()=>{client.close();await mock.close();assert.equal(mock.failures.length,0);});
  for(const sample of invalid)await assert.rejects(client.call(sample.method,sample.params));
  assert.equal(mock.requests.length,0);assert.equal(mock.sockets.size,0);
  for(const [index,sample] of successes.entries()){
    const copy=structuredClone(sample.params),result=await client.call(sample.method,sample.params);
    assert.deepEqual(result.result,sample.result);assert.deepEqual(sample.params,copy);
    assert.deepEqual(mock.requests.at(-1).params,{...sample.params,clientSessionId:mock.session,mutationSequence:String(index+1)});
  }
  for(const [index,sample] of errors.entries())await assert.rejects(client.call(sample.method,sample.params),error=>{
    assert.deepEqual(error.details,sample.error);assert.equal(error.rpcCode,-32010);
    assert.deepEqual(error.bridge,mock.bridge(String(successes.length+index+1)));return true;
  });
  assert.equal(mock.requests.filter(item=>item.method==='bridge.hello').length,1);
  assert.deepEqual(mock.requests.filter(item=>item.method!=='bridge.hello').map(item=>item.method),[...successes,...errors].map(item=>item.method));
});
for(const [era,mode] of [['legacy','legacy'],['modern',{pin:'2026-07-28'}]])test(`M5-04官方${era}stdio完整文件参数/结果/业务错误`,async t=>{
  const mock=await new MockBridge().start();mock.onRequest=reply(mock);
  const descriptor=await mock.descriptorFile();
  const transport=new StdioClientTransport({command:process.execPath,args:[main,'--descriptor',descriptor],cwd,env:{...process.env},stderr:'pipe'});
  const client=new Client({name:'mini3d-file-contract-test',version:'0.1.0'},{jsonSchemaValidator:validator,versionNegotiation:{mode,probe:{timeoutMs:5000,maxRetries:0}}});
  const protocolErrors=[];client.onerror=error=>protocolErrors.push(String(error));
  t.after(async()=>{await client.close();await transport.close();await mock.close();assert.equal(mock.failures.length,0);assert.equal(protocolErrors.length,0);});
  await client.connect(transport,{timeout:5000});assert.equal(client.getProtocolEra(),era);
  const listed=await client.listTools();assert.equal(listed.tools.length,methods.length);
  for(const method of selected){const item=listed.tools.find(tool=>tool.name===toolName(method.name));assert(item);assert.deepEqual(item.inputSchema,toolInput(method));assert.deepEqual(item.outputSchema,toolOutput(method));}
  for(const sample of invalid)assert.equal((await client.callTool({name:toolName(sample.method),arguments:sample.params})).isError,true);
  assert.equal(mock.requests.length,0);assert.equal(mock.sockets.size,0);
  for(const [index,sample] of successes.entries()){
    const result=await client.callTool({name:toolName(sample.method),arguments:sample.params});
    assert.notEqual(result.isError,true);assert.deepEqual(result.structuredContent,sample.result);
    assert.deepEqual(mock.requests.at(-1).params,{...sample.params,clientSessionId:mock.session,mutationSequence:String(index+1)});
  }
  for(const sample of errors){const result=await client.callTool({name:toolName(sample.method),arguments:sample.params});assert.equal(result.isError,true);assert.deepEqual(result.structuredContent.error,sample.error);}
  assert.deepEqual(mock.requests.filter(item=>item.method!=='bridge.hello').map(item=>item.method),[...successes,...errors].map(item=>item.method));
});
