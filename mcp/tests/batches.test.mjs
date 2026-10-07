/*
模块名: batches.test
功能概述: 验证M6两种批次的共享合同、严格输入和单次请求映射。
对外接口: Node test runner。
依赖关系: 官方MCP client、正式adapter、共享样例、MockBridge。
输入输出: 4/19样例与预置flat结果到数量、序号、顺序和错误断言。
异常与错误: 非法输入接触桥、拆分批次或结果丢字段使验证失败。
维护说明: 夹具只预置响应，不模拟本体准备、事务、历史或最终overlay验收。
*/
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {Client} from '@modelcontextprotocol/client';
import {StdioClientTransport} from '@modelcontextprotocol/client/stdio';
import {BridgeClient} from '../dist/bridge.js';
import {methods,resolveSchema,toolInput,toolOutput,requireValid,validator} from '../dist/schema.js';
import {toolName} from '../dist/tools.js';
import {MockBridge,commandResult} from './mock-bridge.mjs';

const samples=JSON.parse(readFileSync(new URL('../../api/schema/m6-examples.json',import.meta.url),'utf8'));
const names=['batch.createEntities','batch.setTransforms'];
const selected=names.map(name=>{const method=methods.find(item=>item.name===name);assert(method);return method;});
const bases=names.map(name=>samples.valid.find(sample=>sample.method===name));
const main=fileURLToPath(new URL('../dist/main.js',import.meta.url));
const cwd=fileURLToPath(new URL('..',import.meta.url));
function without(object,field){const copy={...object};delete copy[field];return copy;}
function withItem(sample,item){return {...sample,params:{...sample.params,items:[item]}};}
const maximum=bases.map(sample=>({...sample,params:{...sample.params,items:Array.from({length:64},(_,index)=>{
  const item=structuredClone(sample.params.items[0]);
  if(sample.method==='batch.createEntities')item.name='批量零件'+index;
  else item.entityId=String(9007199254740993n+BigInt(index));
  return item;
})}}));
const invalid=[...samples.invalid];
for(const [index,sample] of bases.entries()){
  const item=sample.params.items[0];
  invalid.push({...sample,params:{...sample.params,items:[...maximum[index].params.items,item]}});
  for(const [field,value] of [['clientSessionId','11111111-1111-4111-8111-111111111111'],['mutationSequence','1'],['extra',true]])
    invalid.push({...sample,params:{...sample.params,[field]:value}});
  invalid.push({...sample,params:{...sample.params,document:{...sample.params.document,extra:true}}});
  invalid.push(withItem(sample,{...item,extra:true}));
  invalid.push(withItem(sample,{...item,transform:{...item.transform,extra:true}}));
  invalid.push({...sample,params:{...sample.params,items:[item,bases[1-index].params.items[0]]}});
  for(const field of ['document','expectedDocumentRevision','items'])invalid.push({...sample,params:without(sample.params,field)});
  for(const field of Object.keys(item))invalid.push(withItem(sample,without(item,field)));
  for(const field of Object.keys(item.transform))invalid.push(withItem(sample,{...item,transform:without(item.transform,field)}));
  if(item.surface){
    invalid.push(withItem(sample,{...item,surface:{...item.surface,extra:true}}));
    for(const field of Object.keys(item.surface))invalid.push(withItem(sample,{...item,surface:without(item.surface,field)}));
  }
}
const duplicate={...bases[1],params:{...bases[1].params,items:[structuredClone(bases[1].params.items[0]),structuredClone(bases[1].params.items[0])]}};
const nonUnit=withItem(bases[1],{...bases[1].params.items[0],
  transform:{...bases[1].params.items[0].transform,rotationQuaternion:[0,0,0,2]}});
function responseFor(sample){
  const entityIds=sample.method==='batch.createEntities'
    ? sample.params.items.map((_,index)=>String(9007199254742993n-BigInt(index))):[];
  return {...commandResult,document:sample.params.document,documentRevision:'2',historyRevision:'3',
    created:{entityIds},affectedEntityIds:entityIds.length?entityIds:sample.params.items.map(item=>item.entityId)};
}
const successes=[...samples.valid,...maximum,nonUnit].map(sample=>({...sample,result:responseFor(sample)}));
successes[3].result.affectedEntityIds=['2'];
successes.push({...successes[1],result:{...successes[1].result,futureResult:{preserved:true}}});
successes.push({...bases[1],result:{...responseFor(bases[1]),status:'no_change',documentRevision:'1',historyRevision:'1',
  created:{entityIds:[]},affectedEntityIds:[],undoable:false,selectionChanged:false}});
function rejection(sample,code,fieldPath,recovery='correct_input'){
  return {...sample,error:{code,message:'预置本体领域拒绝',fieldPath,recovery,document:sample.params.document,
    documentRevision:'1',historyRevision:'1',futureError:'保留详情'}};
}
const errors=[
  rejection(withItem(bases[0],{...bases[0].params.items[0],parentId:'18446744073709551615'}),'NOT_FOUND','items[0].parentId'),
  rejection(duplicate,'INVALID_ARGUMENT','items[1].entityId'),
  rejection(withItem(bases[1],{...bases[1].params.items[0],transform:{...bases[1].params.items[0].transform,scale:[1e30,1e30,1e30]}}),
    'UNSUPPORTED_TRANSFORM','items[0].transform'),
  rejection({...bases[1],params:{...bases[1].params,expectedDocumentRevision:'0'}},'REVISION_CONFLICT','expectedDocumentRevision','refetch')
];
const intents=[...successes,...errors,successes[0]];
function reply(mock){
  let offset=0;
  return request=>{
    if(!names.includes(request.method))return null;
    const sample=intents[offset];assert(sample);assert.equal(request.method,sample.method);
    assert.deepEqual(request.params,{...sample.params,clientSessionId:mock.session,mutationSequence:String(++offset)});
    return mock.commit(request,sample.error?{error:{code:-32010,message:sample.error.message,data:sample.error}}:{result:sample.result});
  };
}
function assertRequests(mock){
  assert.equal(mock.requests.filter(item=>item.method==='bridge.hello').length,1);
  assert.equal(mock.requests.length,intents.length+1);
  assert.deepEqual(mock.requests.slice(1).map(item=>item.method),intents.map(item=>item.method));
  assert.deepEqual(mock.requests.slice(1).map(item=>item.params.mutationSequence),intents.map((_,index)=>String(index+1)));
  assert.equal(new Set(mock.requests.slice(1).map(item=>item.id)).size,intents.length);
  assert.equal(mock.highWater,String(intents.length));
}

test('M6共享4/19、61方法、1..64完整同类批次；65拒绝，顶层/嵌套/会话字段严格',()=>{
  assert.equal(methods.length,61);assert.equal(samples.valid.length,4);assert.equal(samples.invalid.length,19);
  assert.deepEqual(selected.map(method=>method.permission),['scene.write','scene.write']);
  assert.deepEqual(selected.map(method=>method.kind),['mutation','mutation']);
  assert.deepEqual(selected.map(method=>method.resultSchema),Array(2).fill('results.schema.json#/$defs/command'));
  assert.deepEqual(selected.map(method=>toolName(method.name)),['mini3d_batch_create_entities','mini3d_batch_set_transforms']);
  for(const sample of [...samples.valid,...maximum,nonUnit]){
    const method=methods.find(item=>item.name===sample.method),copy=structuredClone(sample.params);
    requireValid(resolveSchema(method.schema),sample.params);requireValid(toolInput(method),sample.params);
    assert.deepEqual(sample.params,copy);
  }
  for(const sample of samples.invalid)assert.throws(()=>requireValid(resolveSchema(methods.find(item=>item.name===sample.method).schema),sample.params));
  for(const sample of invalid)assert.throws(()=>requireValid(toolInput(methods.find(item=>item.name===sample.method)),sample.params));
  requireValid(toolInput(selected[1]),duplicate.params);
  for(const sample of errors)requireValid(toolInput(methods.find(item=>item.name===sample.method)),sample.params);
});
test('M6普通flat完整结果；创建身份输入顺序、明确受影响目标、no_change和扩展字段无损',()=>{
  for(const sample of successes){
    const schema=toolOutput(methods.find(method=>method.name===sample.method));
    requireValid(schema,sample.result);
    for(const field of ['document','documentRevision','historyRevision','status','created','affectedEntityIds','undoable','selectionChanged'])
      assert.throws(()=>requireValid(schema,without(sample.result,field)));
    assert.throws(()=>requireValid(schema,{...sample.result,created:{}}));
    assert.throws(()=>requireValid(schema,{command:sample.result}));
  }
  assert.deepEqual(successes[1].result.created.entityIds,['9007199254742993','9007199254742992','9007199254742991','9007199254742990']);
  assert.deepEqual(successes[3].result.affectedEntityIds,['2']);
  const noChange=successes.at(-1).result;
  assert.equal(noChange.status,'no_change');assert.equal(noChange.undoable,false);
  assert.equal(noChange.documentRevision,'1');assert.equal(noChange.historyRevision,'1');
  assert.deepEqual(noChange.created.entityIds,[]);assert.deepEqual(noChange.affectedEntityIds,[]);
});
test('M6非法输入零连接；每个成功/领域拒绝一次RPC、序号连续，adapter不拆分/排序/归一化',async t=>{
  const mock=await new MockBridge().start(),client=new BridgeClient(mock.descriptor);mock.onRequest=reply(mock);
  t.after(async()=>{client.close();await mock.close();assert.equal(mock.failures.length,0);});
  for(const sample of invalid)await assert.rejects(client.call(sample.method,sample.params));
  assert.equal(mock.requests.length,0);assert.equal(mock.sockets.size,0);
  for(const [index,sample] of intents.entries()){
    const copy=structuredClone(sample.params),sequence=String(index+1);
    if(sample.error)await assert.rejects(client.call(sample.method,sample.params),error=>{
      assert.deepEqual(error.details,sample.error);assert.equal(error.rpcCode,-32010);
      assert.deepEqual(error.bridge,mock.bridge(sequence));return true;
    });
    else assert.deepEqual(await client.call(sample.method,sample.params),{result:sample.result,bridge:mock.bridge(sequence)});
    assert.deepEqual(sample.params,copy);assert.equal(mock.requests.length,index+2);
    assert.deepEqual(mock.requests.at(-1).params,{...sample.params,clientSessionId:mock.session,mutationSequence:sequence});
  }
  assertRequests(mock);
});
for(const [era,mode] of [['legacy','legacy'],['modern',{pin:'2026-07-28'}]])test('M6官方'+era+'stdio：61工具、批次参数/flat结果/领域错误和no_change',async t=>{
  const mock=await new MockBridge().start();mock.onRequest=reply(mock);
  const descriptor=await mock.descriptorFile();
  const transport=new StdioClientTransport({command:process.execPath,args:[main,'--descriptor',descriptor],cwd,env:{...process.env},stderr:'pipe'});
  const client=new Client({name:'mini3d-batch-contract-test',version:'0.1.0'},{jsonSchemaValidator:validator,versionNegotiation:{mode,probe:{timeoutMs:5000,maxRetries:0}}});
  const protocolErrors=[];client.onerror=error=>protocolErrors.push(String(error));
  t.after(async()=>{await client.close();await transport.close();await mock.close();assert.equal(mock.failures.length,0);assert.equal(protocolErrors.length,0);});
  await client.connect(transport,{timeout:5000});assert.equal(client.getProtocolEra(),era);
  if(era==='modern')assert.equal(client.getNegotiatedProtocolVersion(),'2026-07-28');
  const listed=await client.listTools();assert.equal(listed.tools.length,61);
  assert.deepEqual(listed.tools.map(tool=>tool.name).sort(),methods.map(method=>toolName(method.name)).sort());
  for(const method of selected){
    const item=listed.tools.find(tool=>tool.name===toolName(method.name));assert(item);
    assert.deepEqual(item.inputSchema,toolInput(method));assert.deepEqual(item.outputSchema,toolOutput(method));
    assert.equal(item.annotations.readOnlyHint,false);assert.equal(item.annotations.destructiveHint,true);
    assert.equal(item.annotations.idempotentHint,false);assert.equal(item.annotations.openWorldHint,false);
  }
  for(const sample of invalid)assert.equal((await client.callTool({name:toolName(sample.method),arguments:sample.params})).isError,true);
  assert.equal(mock.requests.length,0);assert.equal(mock.sockets.size,0);
  for(const [index,sample] of intents.entries()){
    const copy=structuredClone(sample.params),sequence=String(index+1);
    const result=await client.callTool({name:toolName(sample.method),arguments:sample.params});
    if(sample.error){
      assert.equal(result.isError,true);assert.deepEqual(result.structuredContent,{error:sample.error,bridge:mock.bridge(sequence)});
      const content=JSON.parse(result.content.find(item=>item.type==='text').text);
      assert.deepEqual(content,{error:sample.error,bridge:mock.bridge(sequence),rpcCode:-32010});
    }else{
      assert.notEqual(result.isError,true);assert.deepEqual(result.structuredContent,sample.result);
      const adapter=result.content.filter(item=>item.type==='text').map(item=>{try{return JSON.parse(item.text);}catch{return null;}}).find(item=>item?.source==='adapter');
      assert.deepEqual(adapter,{source:'adapter',bridge:mock.bridge(sequence)});
      assert.equal(Object.hasOwn(result.structuredContent,'bridge'),false);
      if(sample.result.status==='no_change')assert(result.content.some(item=>item.type==='text'&&item.text.includes('无变化')));
    }
    assert.deepEqual(sample.params,copy);assert.equal(mock.requests.length,index+2);
    assert.deepEqual(mock.requests.at(-1).params,{...sample.params,clientSessionId:mock.session,mutationSequence:sequence});
  }
  assertRequests(mock);
});
