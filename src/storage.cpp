#include "storage.hpp"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <cstdint>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <vector>
namespace gomoku {
static uint64_t checksum(const std::string&s){uint64_t v=14695981039346656037ULL;for(unsigned char c:s){v^=c;v*=1099511628211ULL;}return v;}
std::string serialize(const Game&g){
 std::ostringstream out;out<<"GOMOKU 1\n"<<g.scores[0]<<' '<<g.scores[1]<<' '<<g.scores[2]<<'\n'<<g.history.size()<<'\n';
 for(auto&m:g.history)out<<m.x<<' '<<m.y<<' '<<m.player<<'\n';
 auto body=out.str();out<<"CHECK "<<std::hex<<checksum(body)<<'\n';return out.str();
}
bool deserialize(const std::string&data,Game&g,std::string&error){
 error.clear();auto bad=[&](const char*s){error=s;return false;};
 if(data.size()>65536)return bad("存档超过大小限制");
 auto pos=data.rfind("CHECK ");if(pos==std::string::npos)return bad("缺少校验值");
 std::istringstream tail(data.substr(pos+6));uint64_t sum=0;tail>>std::hex>>sum;
 if(!tail)return bad("校验格式错误");tail>>std::ws;if(!tail.eof()||sum!=checksum(data.substr(0,pos)))return bad("存档校验失败");
 std::istringstream in(data.substr(0,pos));std::string magic;int version=0,n=0;Game candidate;std::array<int,3>scores{};
 if(!(in>>magic>>version)||magic!="GOMOKU"||version!=1)return bad("不支持的存档版本");
 if(!(in>>scores[0]>>scores[1]>>scores[2]>>n)||n<0||n>225)return bad("存档头错误");
 for(int score:scores)if(score<0||score>1000000)return bad("比分范围错误");
 for(int i=0;i<n;++i){Move m;if(!(in>>m.x>>m.y>>m.player)||m.player!=candidate.turn||!candidate.play(m.x,m.y))return bad("棋谱顺序或落子非法");}
 in>>std::ws;if(!in.eof())return bad("存档存在多余内容");
 if(candidate.result&&scores[candidate.result-1]<1)return bad("终局比分不一致");
 candidate.scores=scores;g=std::move(candidate);return true;
}
bool saveGame(const std::string&path,const Game&g,std::string&error){
 error.clear();std::string pattern=path+".tmp.XXXXXX";
 std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
 int fd=mkstemp(name.data());if(fd<0){error="无法创建独立存档临时文件";return false;}
 std::string data=serialize(g);size_t offset=0;bool ok=true;
 while(offset<data.size()){
  auto n=write(fd,data.data()+offset,data.size()-offset);
  if(n>0){offset+=size_t(n);continue;}
  if(n<0&&errno==EINTR)continue;
  ok=false;break;
 }
 if(ok&&fsync(fd)<0)ok=false;
 if(close(fd)<0)ok=false;
 if(!ok){unlink(name.data());error="存档写入失败";return false;}
 std::error_code ec;std::filesystem::rename(name.data(),path,ec);
 if(ec){error="无法替换存档："+ec.message();unlink(name.data());return false;}
 return true;
}
bool loadGame(const std::string&path,Game&g,std::string&error){
 std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec){error="未找到存档";return false;}if(size>65536){error="存档超过大小限制";return false;}
 std::ifstream in(path,std::ios::binary);if(!in){error="无法读取存档";return false;}std::string data((std::istreambuf_iterator<char>(in)),{});return deserialize(data,g,error);
}
}
