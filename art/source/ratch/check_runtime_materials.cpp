#include "render/GltfSkinnedModel.hpp"
#include <iostream>
#include <cmath>
int main(int argc,char**argv){
 lg::GltfSkinnedModel m;
 if(argc!=2||!m.load(argv[1]))return 1;
 const auto& a=m.materialMetadata();
 bool ok=a.valid()&&a.authoredTextureFilesAvailable()&&!a.materialCells;
 unsigned varying=0;
 for(const auto& p:m.primitives()){
  bool varies=false;
  if(!p.vertices.empty())for(const auto& v:p.vertices){
   varies=varies||std::abs(v.u-p.vertices[0].u)>1e-6f||std::abs(v.v-p.vertices[0].v)>1e-6f;
   ok=ok&&v.albedoTextureMode!=0;
  }
  varying+=varies;ok=ok&&varies;
 }
 std::cout<<"{\"pass\":"<<(ok?"true":"false")<<",\"manifest_valid\":"<<(a.valid()?"true":"false")<<",\"texture_files_available\":"<<(a.authoredTextureFilesAvailable()?"true":"false")<<",\"material_cell_mode\":"<<(a.materialCells?"true":"false")<<",\"varying_uv_primitives\":"<<varying<<",\"primitive_count\":"<<m.primitives().size()<<"}\n";
 if(!ok)std::cerr<<a.diagnostic<<"\n";
 return ok?0:2;
}
