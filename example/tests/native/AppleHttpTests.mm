#include "IOHttpClientIOS.hpp"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <chrono>
int main(int argc,char**argv){
 assert(argc==3);
 using namespace rct_io::network;
 auto client=IOHttpClient::create();
 const std::string base=argv[1],path=std::string(argv[2])+"/output";
 HttpRequestConfig invalidTimeout;invalidTimeout.url=base+"/full";invalidTimeout.timeoutMs=0;
 auto invalidStart=std::chrono::steady_clock::now();assert(!client->request(invalidTimeout).success);
 assert(std::chrono::steady_clock::now()-invalidStart<std::chrono::seconds(1));
 HttpRequestConfig config;config.url=base+"/redirect";config.timeoutMs=1000;config.followRedirects=false;
 assert(client->request(config).statusCode==302);
 config.followRedirects=true;auto response=client->request(config);assert(response.statusCode==200&&response.body.size()==8);
 // Exercise the real NSData transport with empty, binary and string-backed bodies.
 for(size_t size: {size_t(0),size_t(3),size_t(1024*1024+3)}) {
   HttpRequestConfig echo;echo.url=base+"/echo";echo.method=HttpMethod::POST;echo.timeoutMs=5000;
   echo.body.resize(size);for(size_t i=0;i<size;++i)echo.body[i]=static_cast<uint8_t>(i%256);
   auto binary=client->request(echo);assert(binary.success&&binary.body==echo.body);
   if(size)echo.bodyString.assign(reinterpret_cast<const char*>(echo.body.data()),size);
   echo.body.clear();auto text=client->request(echo);
   assert(text.success&&text.body==echo.getBodyBytes());
 }
 {std::ofstream out(path);out<<"original";}
 DownloadConfig download;download.url=base+"/error";download.destinationPath=path;download.timeoutMs=1000;
 assert(!client->download(download).success);
 {std::ifstream in(path);std::string value;in>>value;assert(value=="original");}
 download.url=base+"/full";download.resumable=true;assert(client->download(download).success);
 {std::ifstream in(path);std::string value;in>>value;assert(value=="complete");}
 {std::ofstream out(path);out<<"filedata";}
 UploadConfig upload;upload.url=base+"/upload";upload.filePath=path;upload.timeoutMs=1000;upload.fieldName="file";upload.formFields["caption"]="hello";
 auto result=client->upload(upload);assert(result.success&&result.statusCode==200);
 config.url=base+"/slow";config.timeoutMs=100;
 auto start=std::chrono::steady_clock::now();assert(!client->request(config).success);
 assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
 std::filesystem::remove(path);
 std::cout<<"PASS: Apple binary/text HTTP echo, redirect policy, failed-download preservation, replacement, streamed multipart, timeout\n";
}
