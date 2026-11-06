#include "NifFile.hpp"
#include <fstream>
#include <sstream>
#include <set>
#include <iostream>
#include <cstring>
using namespace nifly;
int wmain(int argc,wchar_t** argv){
 try {
  if(argc!=3)return 2;
  NifFile n;if(n.Load(std::filesystem::path(argv[1])))return 3;
  auto& h=n.GetHeader();std::ofstream out{std::filesystem::path(argv[2])};out<<"[";
  std::ofstream raw(std::filesystem::path(argv[2]).wstring()+L".blocks.bin",std::ios::binary);
  for(uint32_t i=0;i<h.GetNumBlocks();++i){
   auto* b=h.GetBlock<NiObject>(i);if(!b)return 4;
   auto serialize=[&](){std::ostringstream s(std::ios::out|std::ios::binary);NiOStream o(&s,&h);b->Put(o);return s.str();};
   auto original=serialize();std::set<NiRef*> refs;b->GetChildRefs(refs);std::set<NiPtr*> ptrs;b->GetPtrs(ptrs);
   for(auto* p:ptrs)refs.insert(p);
   std::vector<uint32_t> values;for(auto* p:refs){values.push_back(p->index);p->index=0x000FEDEB;}
   auto mutated=serialize();size_t j=0;for(auto* p:refs)p->index=values[j++];
   if(original.size()!=mutated.size())return 5;
   if(i)out<<",";out<<"{\"index\":"<<i<<",\"size\":"<<original.size()<<",\"offsets\":[";
   bool first=true;std::vector<bool> touched(original.size(),false);size_t found=0;
   for(size_t k=0;k+4<=mutated.size();++k){uint32_t v;memcpy(&v,mutated.data()+k,4);if(v!=0x000FEDEB)continue;
    if(!first)out<<",";first=false;out<<k;++found;for(size_t t=k;t<k+4;++t)touched[t]=true;k+=3;
   }
   // GetChildRefs also exposes legacy fields absent in the SSE schema.
   if(found>refs.size())return 6;
   for(size_t k=0;k<original.size();++k)if(!touched[k]&&original[k]!=mutated[k])return 7;
   out<<"]}";
   // Preserve exact pre-remap block bytes for Python to compare with source.
   raw.write(original.data(),original.size());
  }
  out<<"]";return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 8;}
}

