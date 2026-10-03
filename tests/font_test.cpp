#include "raylib.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(){
 std::ifstream source("assets/glyphs.txt");
 std::string text((std::istreambuf_iterator<char>(source)),{});
 int count=0;
 int* codes=LoadCodepoints(text.c_str(),&count);
 if(!source||count<100){std::cerr<<"FAIL UI glyph source\n";UnloadCodepoints(codes);return 1;}
 bool passed=true;
 for(const char* path:{"assets/NotoSansSC-Medium.ttf","assets/NotoSansSC-SemiBold.ttf"}){
  int bytes=0;
  unsigned char* data=LoadFileData(path,&bytes);
  auto glyphs=data&&bytes>100000?LoadFontData(data,bytes,48,codes,count,FONT_DEFAULT):nullptr;
  bool ok=glyphs!=nullptr;
  for(int i=0;ok&&i<count;++i)if(codes[i]!=32)ok=glyphs[i].image.data&&glyphs[i].image.width>0&&glyphs[i].image.height>0;
  if(ok){
   std::vector<Rectangle> recs(count);
   for(int i=0;i<count;++i)recs[i]={0,0,float(glyphs[i].image.width),float(glyphs[i].image.height)};
   Font metric{};metric.baseSize=48;metric.glyphCount=count;metric.glyphs=glyphs;metric.recs=recs.data();
   for(const char* label:{"双人对弈","挑战 AI","局域网","初级 0.1s","中级 0.4s","高级 1.2s"}){
    const float width=MeasureTextEx(metric,label,20,1).x;
    if(width>86){std::cerr<<"FAIL button width "<<label<<" "<<width<<"\n";ok=false;}
   }
  }
  std::cout<<(ok?"PASS":"FAIL")<<" CPU rasterization "<<path<<" "<<count<<" UI glyphs and button metrics (no window)\n";
  passed=passed&&ok;
  if(glyphs)UnloadFontData(glyphs,count);
  if(data)UnloadFileData(data);
 }
 UnloadCodepoints(codes);
 return passed?0:1;
}
