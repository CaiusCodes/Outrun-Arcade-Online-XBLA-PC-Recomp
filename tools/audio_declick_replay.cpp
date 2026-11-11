#include "audio_declick.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
int main(int argc,char** argv) {
 if(argc!=3 || std::filesystem::exists(argv[2]))return 1;
 std::ifstream in(argv[1],std::ios::binary);std::array<char,20> header{};in.read(header.data(),20);
 if(!in || std::memcmp(header.data(),"ORPCM001",8))return 2;
 std::ofstream out(argv[2],std::ios::binary);out.write(header.data(),20);
 outrun::audio::BoundaryCorrector filter;unsigned frames=0,fixed=0;
 std::array<uint32_t,1536> words;std::array<float,1536> values;uint64_t stamp;
 while(in.read(reinterpret_cast<char*>(&stamp),8)) {
  if(!in.read(reinterpret_cast<char*>(words.data()),6144))return 3;
  for(unsigned i=0;i<1536;++i)values[i]=std::bit_cast<float>(std::byteswap(words[i]));
  fixed+=filter.Process(values);++frames;
  for(unsigned i=0;i<1536;++i)words[i]=std::byteswap(std::bit_cast<uint32_t>(values[i]));
  out.write(reinterpret_cast<char*>(&stamp),8);out.write(reinterpret_cast<char*>(words.data()),6144);
 }
 out.flush();if(!out)return 4;
 std::cout<<"Processed "<<frames<<" frames; corrected "<<fixed<<" channel edges\n";
}
