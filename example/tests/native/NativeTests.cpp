#include <iostream>
#include <cstring>
#include <cmath>
#include <cassert>
#include <atomic>
#include <thread>
#include <future>
#include <csignal>
#include <sys/resource.h>
#include "IORequestHostObject.hpp"
#include <jsc/JSCRuntime.h>
#include <jsi/decorator.h>
#include "JSIEncoding.hpp"
#include "FSHostObject.hpp"
#include "ThreadPoolFactory.hpp"

using namespace facebook;
class ChunkedStringRuntime final : public jsi::RuntimeDecorator<jsi::Runtime> {
 public:
  explicit ChunkedStringRuntime(jsi::Runtime& rt):RuntimeDecorator(rt){}
  struct Chunk { bool ascii; std::string narrow; std::u16string wide; };
  std::vector<Chunk> chunks;
  bool oversized=false;
  void getStringData(const jsi::String&, void* ctx,
                    void (*cb)(void*,bool,const void*,size_t)) override {
    if(oversized){cb(ctx,true,nullptr,rct_io::maxBufferBytes+1);return;}
    for(const auto& chunk:chunks)cb(ctx,chunk.ascii,
      chunk.ascii?static_cast<const void*>(chunk.narrow.data()):chunk.wide.data(),
      chunk.ascii?chunk.narrow.size():chunk.wide.size());
  }
};
class TestInvoker : public react::CallInvoker {
 public:
  std::mutex mutex;
  std::deque<react::CallFunc> tasks;
  void invokeAsync(react::CallFunc&& fn) noexcept override {
    std::lock_guard lock(mutex); tasks.push_back(std::move(fn));
  }
  void invokeSync(react::CallFunc&&) override { throw std::runtime_error("unused"); }
  void pump(jsi::Runtime& rt) {
    std::deque<react::CallFunc> pending;
    { std::lock_guard lock(mutex); pending.swap(tasks); }
    for(auto& fn:pending)fn(rt);
  }
};
namespace rct_io::network {
class TestHttpClient final : public IOHttpClient {
 public:
  HttpResponse request(const HttpRequestConfig& c) override {
    if(c.url!="https://test" || c.headers.at("X-Test")!="yes")throw std::runtime_error("Incorrect request arguments");
    HttpResponse r; r.success=true;r.body=c.getBodyBytes();return r;
  }
  DownloadResult download(const DownloadConfig&,DownloadProgressCallback) override { return {}; }
  UploadResult upload(const UploadConfig& c,UploadProgressCallback) override {
    if(c.headers.at("X-Test")!="yes"||c.formFields.at("name")!="value")throw std::runtime_error("Lost multipart arguments");
    UploadResult r;r.success=true;return r;
  }
};
std::shared_ptr<IOHttpClient> IOHttpClient::create(){return std::make_shared<TestHttpClient>();}
}
int main(int argc, char** argv){
 assert(argc==2);
 const std::string testRoot=argv[1];
 {
   // The large string allocation must be transferred, not copied. Small-string
   // storage lives inside its owner and must remain writable after the move.
   for(size_t length:std::vector<size_t>{0,1,15,16,17,22,23,24,25,4096,1024*1024}){
     std::string input(length,'\0');
     for(size_t i=0;i<length;++i)input[i]=static_cast<char>(i);
     const auto expected=input;
     const auto* original=input.data();
     jsi_utils::ByteBuffer<std::string> storage(std::move(input));
     assert(storage.size()==length);
     if(length>1024)assert(storage.data()==reinterpret_cast<const uint8_t*>(original));
     if(length){
       assert(std::memcmp(storage.data(),expected.data(),length)==0);
       storage.data()[0]=255;
       assert(storage.data()[0]==255&&expected[0]==0);
     }
   }
   // Real JSC lacks MutableBuffer support: verify its owned-copy fallback,
   // including NUL/high bytes and outputs retained across later allocations.
   auto rt=jsc::makeJSCRuntime();
   std::vector<jsi::ArrayBuffer> retained;
   std::vector<std::string> expected;
   for(size_t length:std::vector<size_t>{0,1,15,16,17,22,23,24,25,4096,1024*1024}){
     std::string input(length,'\0');
     for(size_t i=0;i<length;++i)input[i]=static_cast<char>(i);
     expected.push_back(input);
     retained.push_back(jsi_utils::toArrayBuffer(*rt,std::move(input)));
   }
   assert(!jsi_utils::runtimeState(*rt)->nativeBuffers);
   for(size_t i=0;i<retained.size();++i){
     auto& buffer=retained[i];
     assert(buffer.size(*rt)==expected[i].size());
     if(!expected[i].empty()){
       assert(std::memcmp(buffer.data(*rt),expected[i].data(),expected[i].size())==0);
       buffer.data(*rt)[0]=128;
       assert(buffer.data(*rt)[0]==128&&expected[i][0]==0);
     }
   }
 }
 {
   auto engine=jsc::makeJSCRuntime();ChunkedStringRuntime rt(*engine);
   auto value=jsi::String::createFromAscii(rt,"ignored");
   rt.chunks={{true,"a",{}},{false,{},std::u16string(u"\0b",2)},{true,"",{}},{false,{},u"c"}};
   auto bytes=jsi_utils::tryEncodeAscii(rt,value);
   assert(bytes&&*bytes==std::vector<uint8_t>({97,0,98,99}));
   rt.chunks={{false,{},std::u16string(1024*1024+3,u'x')}};
   bytes=jsi_utils::tryEncodeAscii(rt,value);assert(bytes&&bytes->size()==1024*1024+3&&bytes->back()=='x');
   for(const auto& unicode:{std::u16string(u"é"),std::u16string(u"中文"),std::u16string(u"😀"),std::u16string(1,0xD800)}){
     rt.chunks={{true,"prefix",{}},{false,{},unicode}};
     assert(!jsi_utils::tryEncodeAscii(rt,value));
   }
   rt.chunks.clear();assert(jsi_utils::tryEncodeAscii(rt,value)->empty());
   rt.oversized=true;bool rejected=false;
   try{(void)jsi_utils::tryEncodeAscii(rt,value);}catch(const std::length_error&){rejected=true;}
   assert(rejected);
 }
 {
   rct_io::network::HttpRequestConfig config;
   config.bodyString=std::string("a\0\xFF",3);
   auto text=config.getBodyView();
   assert(text.size()==3&&text.data()==reinterpret_cast<const uint8_t*>(config.bodyString.data()));
   assert(config.getBodyBytes()==std::vector<uint8_t>({97,0,255}));
   config.body={0,128,255};
   auto binary=config.getBodyView();
   assert(binary.data()==config.body.data()&&binary.size()==3); // Binary takes precedence.
   config.body.clear();config.bodyString.clear();assert(config.getBodyView().empty());
 }
 // Exercise the configured limit and limit+1 without allocating payload memory.
 using Lease = rct_io::AsyncPayloadLease;
 assert(Lease::used(Lease::Kind::Input)==0);
 {
   Lease full(Lease::Kind::Input); full.add(Lease::limit);
   Lease rejected(Lease::Kind::Input);
   bool limited=false; try { rejected.add(1); } catch(const std::length_error&) { limited=true; }
   assert(limited && Lease::used(Lease::Kind::Input)==Lease::limit);
   Lease independent(Lease::Kind::Output); independent.add(1);
 }
 assert(Lease::used(Lease::Kind::Input)==0 && Lease::used(Lease::Kind::Output)==0);
 {
   auto first=rct_io::makeThreadPool(4), second=rct_io::makeThreadPool(4);
   bool limited=false; try { auto extra=rct_io::makeThreadPool(1); } catch(const std::runtime_error&) { limited=true; }
   assert(limited && rct_io::liveWorkerThreads==8);
 }
 assert(rct_io::liveWorkerThreads==0);
 {
   auto syncRuntime=jsc::makeJSCRuntime();
   auto syncInvoker=std::make_shared<TestInvoker>();
   auto syncHost=std::make_shared<rct_io::FSHostObject>(*syncRuntime,2,syncInvoker);
   syncRuntime->global().setProperty(*syncRuntime,"testRoot",jsi::String::createFromUtf8(*syncRuntime,testRoot));
   syncRuntime->global().setProperty(*syncRuntime,"fs",jsi::Object::createFromHostObject(*syncRuntime,syncHost));
   syncRuntime->evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
     fs.createDirectorySync(testRoot+'',true);
     fs.existsSync(testRoot+'');
   )JS"),"sync-only.js");
   assert(rct_io::liveWorkerThreads==0);
 }
 assert(rct_io::liveWorkerThreads==0);
 {
   std::promise<void> started, release, drained, cleaned;
   auto releaseFuture=release.get_future().share();
   rct_io::ThreadPoolExecutor lazyExecutor(2);
   assert(rct_io::liveWorkerThreads==0);
   lazyExecutor.execute([&started,&drained,releaseFuture] {
     started.set_value();
     releaseFuture.wait();
     drained.set_value();
   });
   assert(started.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
   assert(rct_io::liveWorkerThreads==2);
   lazyExecutor.shutdown([&cleaned] { cleaned.set_value(); });
   bool rejected=false;
   try { lazyExecutor.execute([] {}); } catch(const std::runtime_error&) { rejected=true; }
   assert(rejected);
   release.set_value();
   assert(drained.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
   assert(cleaned.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
 }
 for(int i=0;i<200&&rct_io::liveWorkerThreads!=0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(rct_io::liveWorkerThreads==0);
 {
   std::promise<void> retired;
   auto retiring=rct_io::makeThreadPool(1);
   retiring->detach_task([held=retiring,&retired]() mutable {
     rct_io::ThreadPoolReaper::instance().retire(std::move(held));
     retired.set_value();
   });
   retiring.reset();
   assert(retired.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
   for(int i=0;i<200&&rct_io::liveWorkerThreads!=0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
   assert(rct_io::liveWorkerThreads==0);
 }
 assert(rct_io::liveWorkerThreads==0);
 {
   auto closedRuntime=jsc::makeJSCRuntime();
   auto closedInvoker=std::make_shared<TestInvoker>();
   auto closedHost=std::make_shared<rct_io::FSHostObject>(*closedRuntime,1,closedInvoker);
   closedRuntime->global().setProperty(*closedRuntime,"testRoot",jsi::String::createFromUtf8(*closedRuntime,testRoot));
   closedRuntime->global().setProperty(*closedRuntime,"fs",jsi::Object::createFromHostObject(*closedRuntime,closedHost));
   closedRuntime->evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
     globalThis.disposedTaskDone=false;
     const savedAsync=fs.exists;
     const savedSync=fs.existsSync;
     const handle=fs.openFile(testRoot+'/disposed-drain',4);
     fs.fileWriteString(handle,'accepted before close').then(()=>disposedTaskDone=true);
     fs.dispose();
     fs.dispose();
     let asyncRejected=false;try{savedAsync(testRoot+'/disposed-drain')}catch(e){asyncRejected=true}
     let syncRejected=false;try{savedSync(testRoot+'/disposed-drain')}catch(e){syncRejected=true}
     if(!asyncRejected||!syncRejected)throw Error('saved native methods bypassed dispose');
   )JS"),"dispose.js");
   for(int i=0;i<1000&&!closedRuntime->global().getProperty(*closedRuntime,"disposedTaskDone").getBool();++i){
     closedInvoker->pump(*closedRuntime);closedRuntime->drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   assert(closedRuntime->global().getProperty(*closedRuntime,"disposedTaskDone").getBool());
 }
 for(int i=0;i<200&&rct_io::liveWorkerThreads!=0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(rct_io::liveWorkerThreads==0);
 std::cout<<"PASS: payload limits, lazy workers, close boundary, pool drain and release\n";
 auto runtime=jsc::makeJSCRuntime(); auto& rt=*runtime;
 rt.global().setProperty(rt,"testRoot",jsi::String::createFromUtf8(rt,testRoot));
 auto invoker=std::make_shared<TestInvoker>();
 auto host=std::make_shared<rct_io::FSHostObject>(rt,2,invoker);
 auto request=std::make_shared<rct_io::IORequestHostObject>(rt,std::make_shared<BS::thread_pool<>>(1),invoker);
 rt.global().setProperty(rt,"http",jsi::Object::createFromHostObject(rt,request));
 rt.global().setProperty(rt,"fs",jsi::Object::createFromHostObject(rt,host));
 rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
   function assert(x){if(!x)throw Error('assertion failed')}
   fs.createDirectorySync((testRoot+''),true);
   fs.writeStringSync((testRoot+'/log'),'中文日志');
   assert(fs.readStringSync((testRoot+'/log'))==='中文日志');
   for(const size of [0,1,3,4,5,7,1025]){
     const bytes=new Uint8Array(size);for(let i=0;i<size;i++)bytes[i]=i%256;
     fs.writeBytesSync((testRoot+'/bin'),bytes.buffer);
     const result=new Uint8Array(fs.readBytesSync((testRoot+'/bin')));
     assert(result.length===size);assert(result.every((x,i)=>x===bytes[i]));
   }
   fs.writeStringSync(testRoot+'/atomic','原子',2,true);
   assert(fs.readStringSync(testRoot+'/atomic')==='原子');
   fs.writeBytesSync(testRoot+'/atomic',new Uint8Array([0,1,255]).buffer,3,true);
   assert(new Uint8Array(fs.readBytesSync(testRoot+'/atomic'))[2]===255);
   // Exercise the borrowed ArrayBuffer for every write mode, including empty
   // buffers, byteLength tampering and a temporary owner with a large payload.
   for(const mode of [0,1,2,3]){
     const path=testRoot+'/borrowed-'+mode;
     const bytes=new Uint8Array([0,128,255]);
     Object.defineProperty(bytes.buffer,'byteLength',{get(){throw Error('JS byteLength must not be read')}});
     fs.writeBytesSync(path,bytes.buffer,mode,true);
     bytes.fill(7);
     assert(Array.from(new Uint8Array(fs.readBytesSync(path))).join(',')==='0,128,255');
     fs.writeBytesSync(path,new ArrayBuffer(0),mode,true);
     assert(fs.getFileSizeSync(path)===(mode===1?3:0));
   }
   const borrowedLargeSize=4*1024*1024+3;
   fs.writeBytesSync(testRoot+'/borrowed-large',new Uint8Array(borrowedLargeSize).fill(165).buffer);
   const borrowedLarge=new Uint8Array(fs.readBytesSync(testRoot+'/borrowed-large'));
   assert(borrowedLarge.length===borrowedLargeSize&&borrowedLarge.every(x=>x===165));
   for(const mode of [0,1,2,3]){
     const path=testRoot+'/uint8-view-'+mode;
     const backing=new Uint8Array([99,0,128,255,88]);
     const view=backing.subarray(1,4);
     let propertyReads=0;
     for(const name of ['buffer','byteOffset','byteLength'])
       Object.defineProperty(view,name,{get(){propertyReads++;throw Error('Do not read view properties')}});
     Object.defineProperty(view,Symbol.toStringTag,{value:'ForgedTag'});
     fs.writeBytesSync(path,view,mode,true);
     assert(propertyReads===0);
     assert(backing.join(',')==='99,0,128,255,88');
     assert(Array.from(new Uint8Array(fs.readBytesSync(path))).join(',')==='0,128,255');
     fs.writeBytesSync(path,backing.subarray(5),mode,true);
     assert(fs.getFileSizeSync(path)===(mode===1?3:0));
   }
   const fakeView={buffer:new ArrayBuffer(4),byteOffset:-1,byteLength:4294967295,[Symbol.toStringTag]:'Uint8Array'};
   for(const invalid of [[],fakeView,new Uint16Array(2),new Uint8ClampedArray(2),new DataView(new ArrayBuffer(2)),Object.create(Uint8Array.prototype),new Proxy(new Uint8Array(2),{})]){
     for(const write of [()=>fs.writeBytesSync(testRoot+'/invalid-view',invalid),()=>fs.writeBytes(testRoot+'/invalid-view',invalid)]){
       let rejected=false;try{write()}catch(e){rejected=true}assert(rejected);
     }
   }
   assert(!fs.existsSync(testRoot+'/invalid-view'));
   if(typeof SharedArrayBuffer!=='undefined'){
     let rejected=false;
     try{fs.writeBytesSync(testRoot+'/shared-view',new Uint8Array(new SharedArrayBuffer(4)))}catch(e){rejected=true}
     assert(rejected&&!fs.existsSync(testRoot+'/shared-view'));
   }
   const RealBuffer=ArrayBuffer;globalThis.ArrayBuffer=function(){return new RealBuffer(0)};
   assert(fs.readBytesSync((testRoot+'/bin')).byteLength===1025);
   globalThis.ArrayBuffer=RealBuffer;
	   for(const fn of [()=>fs.writeStringSync(),()=>fs.writeStringSync(1,'bad'),()=>fs.openFile('bad',NaN),()=>fs.fileRead(1,Infinity),()=>fs.readString(''),()=>fs.readString('bad\0path'),()=>http.request('https://test','GET',[],null,0,true)]){
     let threw=false;try{fn()}catch(e){threw=true}assert(threw);
   }
   globalThis.networkFinished=false;
   const networkBytes=new Uint8Array([0,128,255]);
   const networkWrite=http.request('https://test','POST',['X-Test','yes'],networkBytes.buffer,30000,true);
   networkBytes.fill(7);
   Promise.all([
     http.request('https://test','POST',['X-Test','yes'],'body',30000,true).then(r=>assert(r.success&&r.body.byteLength===4)),
     networkWrite.then(r=>assert(Array.from(new Uint8Array(r.body)).join(',')==='0,128,255')),
     http.request('https://test','POST',['X-Test','yes'],new ArrayBuffer(0),30000,true).then(r=>assert(r.success&&r.body.byteLength===0)),
     http.request('https://test','POST',['X-Test','yes'],'a\0b',30000,true).then(r=>assert(Array.from(new Uint8Array(r.body)).join(',')==='97,0,98')),
     http.upload('https://test','/tmp/input','file','','',['X-Test','yes'],['name'],['value'],30000).then(r=>assert(r.success))
   ]).then(()=>networkFinished=true);
   globalThis.finished=false;
   fs.writeString((testRoot+'/async'),'async').then(()=>fs.readString((testRoot+'/async'))).then(x=>{assert(x==='async');finished=true});
   globalThis.asyncBytesFinished=false;
   // The backing allocation exceeds the test's 64 KiB async payload budget:
   // only the selected bytes may be copied or charged to that budget.
   const asyncBacking=new Uint8Array(128*1024);asyncBacking.set([0,128,255],1000);
   const asyncBytes=asyncBacking.subarray(1000,1003);
   const binaryHandle=fs.openFile(testRoot+'/handle-bytes',4);
   const handleBytes=new Uint8Array([99,1,2,88]).subarray(1,3);
   Promise.all([
     fs.writeBytes(testRoot+'/async-bytes',asyncBytes).then(()=>fs.readBytes(testRoot+'/async-bytes')).then(buffer=>{
       assert(Array.from(new Uint8Array(buffer)).join(',')==='0,128,255');
     }),
     fs.fileWrite(binaryHandle,handleBytes).then(count=>{assert(count===2);return fs.fileCloseAsync(binaryHandle)})
       .then(()=>fs.readBytes(testRoot+'/handle-bytes')).then(buffer=>{
         assert(Array.from(new Uint8Array(buffer)).join(',')==='1,2');
     })
   ]).then(()=>asyncBytesFinished=true);
   asyncBytes.fill(7);handleBytes.fill(9); // Writes must retain their input snapshots.
	   globalThis.asyncCloseFinished=false;
	   const asyncCloseHandle=fs.openFile((testRoot+'/async-close'),4);
	   fs.fileWriteString(asyncCloseHandle,'closed off JS thread').then(()=>fs.fileCloseAsync(asyncCloseHandle)).then(()=>asyncCloseFinished=true);
 )JS"),"tests.js");
	 for(int i=0;i<1000&&(!rt.global().getProperty(rt,"finished").getBool()||!rt.global().getProperty(rt,"asyncBytesFinished").getBool()||!rt.global().getProperty(rt,"networkFinished").getBool()||!rt.global().getProperty(rt,"asyncCloseFinished").getBool());++i){
   invoker->pump(rt);rt.drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 assert(rt.global().getProperty(rt,"finished").getBool());
 assert(rt.global().getProperty(rt,"asyncBytesFinished").getBool());
 assert(rt.global().getProperty(rt,"networkFinished").getBool());
	 assert(rt.global().getProperty(rt,"asyncCloseFinished").getBool());
 rct_io::IOFileSystem io;
 // Text reads preserve raw bytes and file position without an intermediate vector.
 for(size_t size: {size_t(0),size_t(1),size_t(15),size_t(16),size_t(17),size_t(4095),size_t(4096),size_t(4097),size_t(4*1024*1024+3)}) {
   std::string content(size,'\0');
   for(size_t i=0;i<size;++i)content[i]=static_cast<char>(i%256);
   const auto textPath=testRoot+"/text-native";
   io.writeString(textPath,content);
   assert(io.readString(textPath)==content);
   auto bytes=io.readBytes(textPath);
   assert(bytes.size()==content.size()&&(bytes.empty()||std::memcmp(bytes.data(),content.data(),bytes.size())==0));
   rct_io::IOFileHandle textHandle(textPath,rct_io::FileOpenMode::Read);
   assert(textHandle.readString(0).empty()&&textHandle.getPosition()==0);
   const auto prefix=std::min(size,size_t(7));
   assert(textHandle.readString(7)==content.substr(0,prefix));
   assert(textHandle.readString()==content.substr(prefix));
   assert(textHandle.readString().empty());
   textHandle.seek(0);
   assert(textHandle.readString(static_cast<int64_t>(size)+10)==content);
 }
 std::cout<<"PASS: direct text reads, embedded NUL/high bytes, partial reads, EOF and HTTP input snapshots\n";
 {
   const auto oversized=testRoot+"/oversized";io.writeString(oversized,"");
   assert(::truncate(oversized.c_str(),static_cast<off_t>(rct_io::maxBufferBytes)+1)==0);
   bool textRejected=false,binaryRejected=false;
   try{(void)io.readString(oversized);}catch(const std::length_error&){textRejected=true;}
   try{(void)io.readBytes(oversized);}catch(const std::length_error&){binaryRejected=true;}
   assert(textRejected&&binaryRejected);io.deleteFile(oversized);
 }
 const auto path=(testRoot+"/hash");
 io.writeString(path,"abc");
 io.createFile(path);assert(io.readString(path)=="abc");
 assert(io.calcHash(path,rct_io::HashAlgorithm::SHA256)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 assert(io.calcHash(path,rct_io::HashAlgorithm::MD5)=="900150983cd24fb0d6963f7d28e17f72");
 bool overLimit=false;try{rct_io::checkBufferSize(rct_io::maxBufferBytes+1);}catch(const std::length_error&){overLimit=true;}assert(overLimit);
 auto age=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()-io.getModifiedTime(path);
 assert(std::abs(age)<5000);
 auto handle=std::make_shared<rct_io::IOFileHandle>((testRoot+"/handle"),rct_io::FileOpenMode::WriteRead);
 handle->writeString("abc\r\ndef\rx");handle->seek(0);
 assert(handle->readLine(3)=="abc");assert(handle->readLine()=="");assert(handle->readLine()=="def\rx");
 handle->seek(100);assert(handle->read().empty());handle->seek(0);
 assert(handle->readString(1)=="a");handle->writeString("Z");
 handle->seek(0);assert(handle->readString(2)=="aZ");
 std::vector<std::thread> writers;
 for(int i=0;i<4;++i)writers.emplace_back([handle]{for(int j=0;j<1000;++j){try{handle->writeLine("record");}catch(const std::runtime_error&){}}});
 std::thread closer([handle]{handle->close();});
 for(auto& writer:writers)writer.join();closer.join();
 // Atomic replacement must preserve permissions, reject symlinks and clean failures.
 const auto atomicPath=testRoot+"/atomic-native";
 io.writeString(atomicPath,"old");::chmod(atomicPath.c_str(),0640);
 io.writeString(atomicPath,"new",rct_io::WriteMode::Atomic);
 struct stat permissions{};::stat(atomicPath.c_str(),&permissions);assert((permissions.st_mode&0777)==0640);
 const auto alias=testRoot+"/alias";::symlink(atomicPath.c_str(),alias.c_str());
 bool symlinkRejected=false;try{io.writeString(alias,"wrong",rct_io::WriteMode::Atomic);}catch(const std::invalid_argument&){symlinkRejected=true;}
 assert(symlinkRejected && io.readString(atomicPath)=="new");
 assert(io.getMetadata(alias).type==rct_io::EntityType::File);
 const auto missingAlias=testRoot+"/missing-alias";::symlink((testRoot+"/absent").c_str(),missingAlias.c_str());
 assert(io.getMetadata(missingAlias).type==rct_io::EntityType::NotFound);
 bool foundAlias=false,foundMissing=false;
 for(const auto& entry:io.listDirectory(testRoot)) {
   if(entry.name=="alias"){assert(entry.type==rct_io::EntityType::File&&entry.size==3);foundAlias=true;}
   if(entry.name=="missing-alias"){assert(entry.type==rct_io::EntityType::NotFound);foundMissing=true;}
 }
 assert(foundAlias&&foundMissing);
 std::string versionA(4096,'A'),versionB(4096,'B');io.writeString(atomicPath,versionA,rct_io::WriteMode::Atomic);
 std::atomic<bool> done{false},consistent{true};
 std::thread reader([&]{while(!done){auto data=io.readString(atomicPath);if(data!=versionA&&data!=versionB)consistent=false;}});
 for(int i=0;i<50;++i)io.writeString(atomicPath,i%2?versionA:versionB,rct_io::WriteMode::Atomic);
 done=true;reader.join();assert(consistent);
 io.writeString(atomicPath,"old",rct_io::WriteMode::AtomicDurable);
 // Buffered close errors must not be reported as a successful archive write.
 struct rlimit previous;getrlimit(RLIMIT_FSIZE,&previous);auto limited=previous;limited.rlim_cur=4;
 auto previousSignal=std::signal(SIGXFSZ,SIG_IGN);setrlimit(RLIMIT_FSIZE,&limited);
 bool failed=false;try{io.writeString((testRoot+"/full"),"too much data");}catch(const std::exception&){failed=true;}
 bool atomicFailed=false;try{io.writeString(atomicPath,"too much data",rct_io::WriteMode::Atomic);}catch(const std::exception&){atomicFailed=true;}
 const std::array<uint8_t,8> binaryPayload{0,1,2,3,4,5,6,255};
 bool binaryFailed=false;try{io.writeBytes(testRoot+"/full-bytes",binaryPayload);}catch(const std::exception&){binaryFailed=true;}
 bool binaryAtomicFailed=false;try{io.writeBytes(atomicPath,binaryPayload,rct_io::WriteMode::Atomic);}catch(const std::exception&){binaryAtomicFailed=true;}
 setrlimit(RLIMIT_FSIZE,&previous);std::signal(SIGXFSZ,previousSignal);assert(failed&&atomicFailed&&binaryFailed&&binaryAtomicFailed);
 assert(io.readString(atomicPath)=="old");
 for(const auto& entry:std::filesystem::directory_iterator(testRoot))assert(entry.path().filename().string().find(".rct-io-")!=0);
 std::cout<<"PASS: atomic JSI modes, permissions, symlink rejection, concurrent readers, failure rollback and stage cleanup\n";
 std::cout<<"PASS: hashes, Unix timestamps, line boundaries, EOF, concurrent close, buffered write failure, HTTP header/form parsing\n";

 std::cout<<"PASS: real JSI sync/async I/O, binary boundaries, tampered ArrayBuffer, malformed arguments\n";
 rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
   globalThis.queueDone=false;const pending=[];
   for(let i=0;i<256;++i)pending.push(fs.exists((testRoot+'/log')));
   let rejected=false;try{fs.exists((testRoot+'/log'))}catch(e){rejected=true}assert(rejected);
   Promise.all(pending).then(()=>queueDone=true);
 )JS"),"backpressure.js");
 for(int i=0;i<1000&&!rt.global().getProperty(rt,"queueDone").getBool();++i){invoker->pump(rt);rt.drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(rt.global().getProperty(rt,"queueDone").getBool());
 assert(Lease::used(Lease::Kind::Input)==0 && Lease::used(Lease::Kind::Output)==0);
 // The pending-operation ceiling is shared across separate HTTP HostObjects;
 // one-use Request proxy instances must not bypass it.
 {
   auto sharedPool=std::make_shared<BS::thread_pool<>>(1);
   std::vector<std::shared_ptr<rct_io::IORequestHostObject>> clients;
   jsi::Array clientArray(rt,257);
   clients.reserve(257);
   for(size_t i=0;i<257;++i){
     auto client=std::make_shared<rct_io::IORequestHostObject>(rt,sharedPool,invoker);
     clientArray.setValueAtIndex(rt,i,jsi::Object::createFromHostObject(rt,client));
     clients.push_back(std::move(client));
   }
   rt.global().setProperty(rt,"clients",std::move(clientArray));
   rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
     globalThis.crossHostDone=false;const crossHost=[];
     for(let i=0;i<256;++i)crossHost.push(clients[i].request('https://test','POST',['X-Test','yes'],'body',30000,true));
     let crossHostRejected=false;try{clients[256].request('https://test','POST',['X-Test','yes'],'body',30000,true)}catch(e){crossHostRejected=true}
     assert(crossHostRejected);Promise.all(crossHost).then(()=>crossHostDone=true);
   )JS"),"cross-host-backpressure.js");
   for(int i=0;i<2000&&!rt.global().getProperty(rt,"crossHostDone").getBool();++i){invoker->pump(rt);rt.drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   assert(rt.global().getProperty(rt,"crossHostDone").getBool());
   rt.global().setProperty(rt,"clients",jsi::Value::undefined());
   clients.clear();
 }
 // Fill the real callback backlog while the JS thread deliberately does not drain.
 rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(R"JS(
   globalThis.outputDone=false;globalThis.outputFailures=0;
   const outputs=[];for(let i=0;i<100;++i)outputs.push(fs.readBytes(testRoot+'/bin').catch(()=>{outputFailures++}));
   Promise.all(outputs).then(()=>outputDone=true);
 )JS"),"output-budget.js");
 for(int i=0;i<1000;++i){size_t count;{std::lock_guard lock(invoker->mutex);count=invoker->tasks.size();}if(count==100)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(Lease::used(Lease::Kind::Output)<=Lease::limit);
 for(int i=0;i<1000&&!rt.global().getProperty(rt,"outputDone").getBool();++i){invoker->pump(rt);rt.drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(rt.global().getProperty(rt,"outputDone").getBool());
 assert(rt.global().getProperty(rt,"outputFailures").getNumber()>0);
 assert(Lease::used(Lease::Kind::Input)==0 && Lease::used(Lease::Kind::Output)==0);
 std::cout<<"PASS: bounded pending completions and allocation limits\n";
 // Retain only a method, release native owner: invocation must throw, never dereference freed this.
 rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>("globalThis.detached=fs.readStringSync;fs=undefined"),"gc.js");
 host.reset();
 rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>("let caught=false;try{detached('/tmp/x')}catch(e){caught=true}assert(caught)"),"released.js");
 request.reset();
 rt.global().setProperty(rt,"http",jsi::Value::undefined());
 rt.global().setProperty(rt,"fs",jsi::Value::undefined());
 invoker->pump(rt);
 // A pending task may outlive its originating runtime. Drain callbacks against
 // a different runtime to verify they never touch old JSI values or pointers.
 {
   auto old = jsc::makeJSCRuntime();
   auto oldHost = std::make_shared<rct_io::FSHostObject>(*old,1,invoker);
   old->global().setProperty(*old,"testRoot",jsi::String::createFromUtf8(*old,testRoot));
   old->global().setProperty(*old,"fs",jsi::Object::createFromHostObject(*old,oldHost));
   old->evaluateJavaScript(std::make_shared<jsi::StringBuffer>("fs.readString((testRoot+'/log'))"),"teardown.js");
   oldHost.reset();
   old.reset();
 }
 for(int i=0;i<20;++i){invoker->pump(rt);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 std::cout<<"PASS: callback completion after runtime teardown\n";
 return 0;
}
