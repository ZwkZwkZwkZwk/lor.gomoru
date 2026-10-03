#include "ui_state.hpp"
#include "storage.hpp"
#include <iostream>
#include <stdexcept>
using namespace gomoku;
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(){try{
 auto cell=boardCell(86,138);check(cell.x==0&&cell.y==0,"top left coordinate");
 cell=boardCell(618,670);check(cell.x==14&&cell.y==14,"bottom right coordinate");
 check(boardCell(49,100).x==-1&&boardCell(641,693).x==-1,"outside board rejected");
 Game g;g.play(7,7);g.play(8,8);g.play(7,8);UiState u;
 check(u.canPlay(g),"live human input");auto gen=u.generation;
 u.enterReplay(g);check(u.replay==3&&!u.canPlay(g),"replay blocks input");
 check(!u.acceptsAI(g,gen,3),"replay rejects stale search");
 u.seek(g,-100);check(u.replay==0,"replay first clamp");u.playing=true;
 u.advance(g);check(u.replay==1,"auto replay next");u.seek(g,999);u.advance(g);
 check(u.replay==3&&!u.playing,"end pauses");
 u.changed();check(u.replay==-1&&u.canPlay(g),"return live");
 gen=u.generation;check(u.acceptsAI(g,gen,3),"current AI accepts");
 u.changed();check(!u.acceptsAI(g,gen,3),"new generation rejects old AI");
 check(!u.acceptsAI(g,u.generation,2),"wrong ply rejects AI");
 Game winner;for(int i=0;i<5;++i){winner.play(i,0);if(i<4)winner.play(i*2,14);}
 check(indicatorPlayer(winner)==1,"winner status indicator uses winner not next turn");
 Game aiWin;for(int i=0;i<4;++i){aiWin.play(i*2,0);aiWin.play(i,7);}aiWin.play(8,0);UiState frameUi;
 auto frame=frameUi.resolveAI(aiWin,{4,7,2},frameUi.generation,9,false);
 check(aiWin.result==2&&aiWin.scores[1]==1,"AI winning result applied");
 check(serialize(frame)==serialize(aiWin),"AI completion frame Save includes new move and score");
 auto before=serialize(aiWin);frame=frameUi.resolveAI(aiWin,{3,4,2},frameUi.generation,10,true);
 check(serialize(frame)==before&&serialize(aiWin)==before,"cancelled frame preserves live state");
 int port=0;check(parsePort("34567",port)&&port==34567,"valid port");
 check(!parsePort("1234.",port),"port rejects trailing dot instead of numeric prefix");
 check(!parsePort("0",port)&&!parsePort("65536",port)&&!parsePort("-3",port)&&!parsePort("",port),"port range and syntax");
 std::cout<<"PASS ui-state 19 scenarios\n";
}catch(const std::exception&e){std::cerr<<"FAIL ui-state: "<<e.what()<<'\n';return 1;}}
