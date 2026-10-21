#include <iostream>
#include <chrono>
#include <array>
#include IO_FILESYSTEM_HEADER
int main(int argc,char**argv){
 if(argc!=2)return 1;
 auto begin=std::chrono::steady_clock::now();
 rct_io::IOFileSystem fs;
 std::string result;
 for(int i=0;i<3;++i)result=fs.calcHash(argv[1],rct_io::HashAlgorithm::SHA256);
 std::cout<<"sha256="<<result<<" mean_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/3<<"\n";
}
