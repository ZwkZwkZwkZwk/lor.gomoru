#pragma once
#include "game.hpp"
#include <cstdint>
#include <string>
namespace gomoku {
Move boardCell(float x,float y);
int indicatorPlayer(const Game&);
bool parsePort(const std::string&,int&);
struct UiState {
 int replay=-1;
 bool playing=false;
 uint64_t generation=0;
 void changed();
 void enterReplay(const Game&);
 void seek(const Game&,int);
 void advance(const Game&);
 bool canPlay(const Game&)const;
 bool acceptsAI(const Game&,uint64_t,size_t)const;
 Game resolveAI(Game&,const Move&,uint64_t,size_t,bool)const;
};
}
