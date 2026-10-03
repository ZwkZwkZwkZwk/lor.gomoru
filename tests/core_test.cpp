#include "game.hpp"
#include "storage.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <sstream>
#include <cstdint>
using namespace gomoku;
int checks=0;
void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
std::string checkedBody(const std::string&body){uint64_t value=14695981039346656037ULL;for(unsigned char c:body){value^=c;value*=1099511628211ULL;}std::ostringstream out;out<<body<<"CHECK "<<std::hex<<value<<'\n';return out.str();}
bool sameState(const Game&a,const Game&b){
 if(serialize(a)!=serialize(b)||a.board!=b.board||a.turn!=b.turn||a.result!=b.result||a.winningLine.size()!=b.winningLine.size())return false;
 for(size_t i=0;i<a.winningLine.size();++i)if(a.winningLine[i].x!=b.winningLine[i].x||a.winningLine[i].y!=b.winningLine[i].y||a.winningLine[i].player!=b.winningLine[i].player)return false;
 return true;
}
Game winning(const std::vector<Move>& black){
 Game g;
 for(size_t i=0;i<black.size();++i){check(g.play(black[i].x,black[i].y),"black fixture move");if(i+1<black.size())check(g.play(int(i)*2,14),"white fixture move");}
 return g;
}
int main(){try{
 Game g;
 check(g.at(7,7)==0&&g.turn==1,"empty black starts");
 check(!g.play(-1,0)&&!g.play(15,0)&&g.history.empty(),"bounds reject no mutation");
 check(g.play(7,7)&&g.turn==2,"legal changes turn");
 check(!g.play(7,7)&&g.history.size()==1&&g.turn==2,"occupied reject");
 for(auto line:std::vector<std::vector<Move>>{
  {{0,0},{1,0},{2,0},{3,0},{4,0}},{{14,0},{14,1},{14,2},{14,3},{14,4}},
  {{0,0},{1,1},{2,2},{3,3},{4,4}},{{14,0},{13,1},{12,2},{11,3},{10,4}},
  {{0,0},{1,0},{2,0},{4,0},{5,0},{3,0}}}){
  auto win=winning(line);
  check(win.result==1&&win.winningLine.size()==line.size(),"directions bridge overline");
  check(win.scores[0]==1,"score once");
  check(!win.play(9,9)&&win.scores[0]==1,"terminal reject");
  auto p=win.prefix(3);check(p.history.size()==3&&p.result==0&&p.scores==win.scores,"prefix read only no rescore");
  win.reset();check(win.history.empty()&&win.turn==1&&win.result==0&&win.scores[0]==1,"reset score");
 }
 Game white;for(int i=0;i<5;++i){check(white.play(i*2,0),"black dispersed");check(white.play(i,7),"white line");}
 check(white.result==2&&white.scores[1]==1,"white win");
 Game draw;std::vector<Move>b,w;
 for(int y=0;y<15;++y)for(int x=0;x<15;++x)(((x+2*y)%4<2)?b:w).push_back({x,y,0});
 check(b.size()==113&&w.size()==112,"draw fixture balance");
 for(size_t i=0;i<b.size();++i){check(draw.play(b[i].x,b[i].y),"draw black");if(i<w.size())check(draw.play(w[i].x,w[i].y),"draw white");}
 check(draw.result==3&&draw.scores[2]==1,"full draw");
 std::string error;Game loaded;auto data=serialize(g);
 check(deserialize(data,loaded,error)&&loaded.board==g.board&&loaded.turn==g.turn,"roundtrip");
 check(loaded.play(8,7),"continue");
 auto terminal=winning({{0,0},{1,0},{2,0},{3,0},{4,0}});
 check(deserialize(serialize(terminal),loaded,error)&&loaded.scores==terminal.scores&&loaded.result==1,"terminal no rescore");
 auto old=loaded.board;data[data.size()/2]^=1;
 check(!deserialize(data,loaded,error)&&loaded.board==old,"checksum transactional");
 check(!deserialize(std::string(70000,'x'),loaded,error),"size cap");
 check(!deserialize("GOMOKU 9\n",loaded,error),"version");
 Game untouched=loaded;
 for(const auto&body:std::vector<std::string>{"GOMOKU 9\n0 0 0\n0\n","GOMOKU 1\n0 0 0\n226\n","GOMOKU 1\n0 0 0\n1\n0 0 2\n","GOMOKU 1\n0 0 0\n2\n0 0 1\n0 0 2\n","GOMOKU 1\n0 0 0\n1\n15 0 1\n","GOMOKU 1\n0 0 0\n0\nEXTRA\n"}){
  check(!deserialize(checkedBody(body),loaded,error)&&sameState(loaded,untouched),"checksummed invalid fixtures preserve every state field");
 }
 std::ostringstream extended;extended<<"GOMOKU 1\n1 0 0\n"<<terminal.history.size()+1<<'\n';
 for(auto&m:terminal.history)extended<<m.x<<' '<<m.y<<' '<<m.player<<'\n';extended<<"9 9 2\n";
 check(!deserialize(checkedBody(extended.str()),loaded,error)&&sameState(loaded,untouched),"checksummed terminal extension rejected");
 std::ostringstream scoreless;scoreless<<"GOMOKU 1\n0 0 0\n"<<terminal.history.size()<<'\n';for(auto&m:terminal.history)scoreless<<m.x<<' '<<m.y<<' '<<m.player<<'\n';
 check(!deserialize(checkedBody(scoreless.str()),loaded,error)&&sameState(loaded,untouched),"checksummed terminal score mismatch rejected");
 const std::string path="build/test-save.gmk";
 {std::ofstream busy(path+".tmp");busy<<"another writer owns this";}
 check(saveGame(path,g,error),"save");
 {std::ifstream busy(path+".tmp");std::string value((std::istreambuf_iterator<char>(busy)),{});check(value=="another writer owns this","save temporary isolation");}
 std::filesystem::remove(path+".tmp");check(loadGame(path,loaded,error)&&loaded.board==g.board,"load");
 {std::ofstream f(path);f<<"broken";}old=loaded.board;
 check(!loadGame(path,loaded,error)&&loaded.board==old,"load corrupt preserves");
 Game other=g;other.play(10,10);std::atomic<int> failures{0};
 auto writer=[&](const Game&state){for(int i=0;i<60;++i){std::string err;if(!saveGame(path,state,err))++failures;}};
 std::thread first(writer,std::cref(g)),second(writer,std::cref(other));first.join();second.join();
 check(failures==0,"two writers save successfully");
 check(loadGame(path,loaded,error)&&(serialize(loaded)==serialize(g)||serialize(loaded)==serialize(other)),"two writer final slot is one complete game");
 std::filesystem::remove(path);std::cout<<"PASS core "<<checks<<" checks\n";
}catch(const std::exception&e){std::cerr<<"FAIL core: "<<e.what()<<"\n";return 1;}}
