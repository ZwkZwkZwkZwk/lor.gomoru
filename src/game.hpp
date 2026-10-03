#pragma once
#include <array>
#include <vector>
namespace gomoku {
struct Move { int x=-1,y=-1,player=0; };
class Game {
public:
 std::array<int,225> board{};
 std::vector<Move> history;
 int turn=1,result=0;
 std::array<int,3> scores{};
 std::vector<Move> winningLine;
 int at(int x,int y) const;
 bool play(int x,int y);
 void reset();
 Game prefix(int count) const;
};
}
