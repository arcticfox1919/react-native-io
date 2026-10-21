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
#include "FSHostObject.hpp"
#include "ThreadPoolFactory.hpp"

using namespace facebook;
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
    HttpResponse r; r.success=true;r.body=c.body;return r;
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
   const RealBuffer=ArrayBuffer;globalThis.ArrayBuffer=function(){return new RealBuffer(0)};
   assert(fs.readBytesSync((testRoot+'/bin')).byteLength===1025);
   globalThis.ArrayBuffer=RealBuffer;
	   for(const fn of [()=>fs.writeStringSync(),()=>fs.writeStringSync(1,'bad'),()=>fs.openFile('bad',NaN),()=>fs.fileRead(1,Infinity),()=>fs.readString(''),()=>fs.readString('bad\0path'),()=>http.request('https://test','GET',[],null,0,true)]){
     let threw=false;try{fn()}catch(e){threw=true}assert(threw);
   }
   globalThis.networkFinished=false;
   Promise.all([
     http.request('https://test','POST',['X-Test','yes'],'body',30000,true).then(r=>assert(r.success&&r.body.byteLength===4)),
     http.upload('https://test','/tmp/input','file','','',['X-Test','yes'],['name'],['value'],30000).then(r=>assert(r.success))
   ]).then(()=>networkFinished=true);
   globalThis.finished=false;
   fs.writeString((testRoot+'/async'),'async').then(()=>fs.readString((testRoot+'/async'))).then(x=>{assert(x==='async');finished=true});
	   globalThis.asyncCloseFinished=false;
	   const asyncCloseHandle=fs.openFile((testRoot+'/async-close'),4);
	   fs.fileWriteString(asyncCloseHandle,'closed off JS thread').then(()=>fs.fileCloseAsync(asyncCloseHandle)).then(()=>asyncCloseFinished=true);
 )JS"),"tests.js");
	 for(int i=0;i<1000&&(!rt.global().getProperty(rt,"finished").getBool()||!rt.global().getProperty(rt,"networkFinished").getBool()||!rt.global().getProperty(rt,"asyncCloseFinished").getBool());++i){
   invoker->pump(rt);rt.drainMicrotasks();std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 assert(rt.global().getProperty(rt,"finished").getBool());
 assert(rt.global().getProperty(rt,"networkFinished").getBool());
	 assert(rt.global().getProperty(rt,"asyncCloseFinished").getBool());
 rct_io::IOFileSystem io;
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
 setrlimit(RLIMIT_FSIZE,&previous);std::signal(SIGXFSZ,previousSignal);assert(failed&&atomicFailed);
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
