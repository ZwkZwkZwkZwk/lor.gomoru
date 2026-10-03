#include "game.hpp"
#include <algorithm>
namespace gomoku {
int Game::at(int x,int y)const{return x<0||y<0||x>=15||y>=15?-1:board[y*15+x];}
bool Game::play(int x,int y){
 if(result||at(x,y)!=0)return false;
 const int player=turn;
 board[y*15+x]=player;history.push_back({x,y,player});winningLine.clear();
 const int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};
 for(auto&d:dirs){
  int ax=x,ay=y;
  while(at(ax-d[0],ay-d[1])==player){ax-=d[0];ay-=d[1];}
  std::vector<Move> line;
  while(at(ax,ay)==player){line.push_back({ax,ay,player});ax+=d[0];ay+=d[1];}
  if(line.size()>=5){result=player;winningLine=std::move(line);break;}
 }
 if(!result&&history.size()==225)result=3;
 if(result)++scores[result-1];
 turn=3-turn;
 return true;
}
void Game::reset(){board.fill(0);history.clear();turn=1;result=0;winningLine.clear();}
Game Game::prefix(int count)const{
 Game g;count=std::clamp(count,0,int(history.size()));
 for(int i=0;i<count;++i)g.play(history[i].x,history[i].y);
 g.scores=scores;return g;
}
}
