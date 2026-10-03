#include "ui_state.hpp"
#include <cmath>
#include <algorithm>
#include <charconv>
namespace gomoku {
Game UiState::resolveAI(Game&g,const Move&m,uint64_t id,size_t ply,bool cancelled)const{if(!cancelled&&acceptsAI(g,id,ply))g.play(m.x,m.y);return g;}
bool parsePort(const std::string&s,int&port){
 if(s.empty()||s.size()>5||!std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}))return false;
 int value=0;auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
 if(parsed.ec!=std::errc()||parsed.ptr!=s.data()+s.size()||value<1||value>65535)return false;
 port=value;return true;
}
int indicatorPlayer(const Game&g){return g.result==1||g.result==2?g.result:g.turn;}
Move boardCell(float x,float y){
 int a=int(std::round((x-86)/38)),b=int(std::round((y-138)/38));
 if(a<0||b<0||a>=15||b>=15||std::abs(x-(86+a*38))>18||std::abs(y-(138+b*38))>18)return {};
 return {a,b,0};
}
void UiState::changed(){++generation;replay=-1;playing=false;}
void UiState::enterReplay(const Game&g){++generation;replay=int(g.history.size());playing=false;}
void UiState::seek(const Game&g,int pos){replay=std::clamp(pos,0,int(g.history.size()));playing=false;}
void UiState::advance(const Game&g){if(replay>=0&&playing){replay=std::min(replay+1,int(g.history.size()));if(replay==int(g.history.size()))playing=false;}}
bool UiState::canPlay(const Game&g)const{return replay<0&&!g.result;}
bool UiState::acceptsAI(const Game&g,uint64_t id,size_t ply)const{return id==generation&&ply==g.history.size()&&g.turn==2&&canPlay(g);}
}
