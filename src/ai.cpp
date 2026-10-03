#include "ai.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <vector>
namespace gomoku {
namespace {
using Board = std::array<int,225>;
using Clock = std::chrono::steady_clock;
constexpr int directions[4][2] = {{1,0},{0,1},{1,1},{1,-1}};
constexpr int winScore = 100000000;
constexpr int weights[6] = {0,2,24,350,16000,2000000};
struct Candidate { int index; int score; };
struct Search {
 Board board;
 std::atomic<bool>& cancel;
 Clock::time_point deadline;
 int width;
 bool interrupted = false;
 bool stop() {
  if (cancel.load(std::memory_order_relaxed) || Clock::now() >= deadline) interrupted = true;
  return interrupted;
 }
 int at(int x, int y) const {
  return x < 0 || y < 0 || x >= 15 || y >= 15 ? -1 : board[y*15+x];
 }
 bool wins(int index, int player) const {
  const int x = index%15, y = index/15;
  for (const auto& direction : directions) {
   int length = 1;
   for (int sign : {-1,1}) {
    int distance = 1;
    while (at(x+sign*distance*direction[0],y+sign*distance*direction[1]) == player) {
     ++length; ++distance;
    }
   }
   if (length >= 5) return true;
  }
  return false;
 }
 int shape(int index, int player) const {
  const int x = index%15, y = index/15;
  int score = 0;
  for (const auto& direction : directions) {
   for (int offset = -4; offset <= 0; ++offset) {
    int count = 1;
    bool blocked = false;
    for (int step = offset; step < offset+5; ++step) {
     if (step == 0) continue;
     const int value = at(x+step*direction[0],y+step*direction[1]);
     if (value == -1 || value == 3-player) { blocked = true; break; }
     if (value == player) ++count;
    }
    if (!blocked) score += weights[count];
   }
  }
  return score;
 }
 std::vector<Candidate> candidates(int player, int limit) {
  std::vector<Candidate> choices, winsNow, blocks;
  std::array<bool,225> nearby{};
  bool occupied = false;
  for (int index = 0; index < 225; ++index) {
   if (!board[index]) continue;
   occupied = true;
   for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
     const int x = index%15+dx, y = index/15+dy;
     if (at(x,y) == 0) nearby[y*15+x] = true;
    }
  }
  if (!occupied) return {{112,0}};
  for (int index = 0; index < 225; ++index) {
   if (stop()) return {};
   if (board[index]) continue;
   const int center = 14-std::abs(index%15-7)-std::abs(index/15-7);
   if (wins(index,player)) winsNow.push_back({index,winScore+center});
   if (wins(index,3-player)) blocks.push_back({index,shape(index,player)+center});
   if (nearby[index]) choices.push_back({index,shape(index,player)*2+shape(index,3-player)*3+center});
  }
  auto& selected = !winsNow.empty() ? winsNow : !blocks.empty() ? blocks : choices;
  std::sort(selected.begin(),selected.end(),[](const Candidate& left, const Candidate& right) {
   return left.score != right.score ? left.score > right.score : left.index < right.index;
  });
  if (selected.size() > static_cast<size_t>(limit)) selected.resize(limit);
  return selected;
 }
 int evaluate(int player) {
  int score = 0;
  for (const auto& direction : directions) {
   if (stop()) return 0;
   for (int y = 0; y < 15; ++y)
    for (int x = 0; x < 15; ++x) {
     if (at(x+4*direction[0],y+4*direction[1]) == -1) continue;
     int own = 0, enemy = 0;
     for (int step = 0; step < 5; ++step) {
      const int value = at(x+step*direction[0],y+step*direction[1]);
      own += value == player; enemy += value == 3-player;
     }
     if (!enemy) score += weights[own];
     if (!own) score -= weights[enemy];
    }
  }
  return score;
 }
 int negamax(int player, int depth, int alpha, int beta, int ply) {
  if (stop()) return 0;
  if (depth == 0) return evaluate(player);
  const auto moves = candidates(player,width);
  if (interrupted || moves.empty()) return 0;
  int best = -winScore;
  for (const auto& move : moves) {
   if (stop()) return 0;
   const bool winning = wins(move.index,player);
   board[move.index] = player;
   const int score = winning ? winScore-ply : -negamax(3-player,depth-1,-beta,-alpha,ply+1);
   board[move.index] = 0;
   if (interrupted) return 0;
   best = std::max(best,score); alpha = std::max(alpha,score);
   if (alpha >= beta) break;
  }
  return best;
 }
};
}
Move chooseMove(const Game& game, int difficulty, std::atomic<bool>& cancel) {
 const auto started = Clock::now();
 if (cancel.load(std::memory_order_relaxed) || game.result || (game.turn != 1 && game.turn != 2)) return {-1,-1,0};
 const int level = std::clamp(difficulty,0,2);
 const int budgets[] = {100,400,1200};
 Search search{game.board,cancel,started+std::chrono::milliseconds(budgets[level]),level == 2 ? 10 : 8};
 int fallback = -1;
 for (int index = 0; index < 225; ++index) if (!game.board[index]) { fallback = index; break; }
 if (fallback == -1) return {-1,-1,0};
 auto moves = search.candidates(game.turn,level == 2 ? 18 : 12);
 int selected = moves.empty() ? fallback : moves.front().index;
 if (!moves.empty() && !search.wins(selected,game.turn) && !search.wins(selected,3-game.turn) && level > 0) {
  for (int depth = 1; depth <= (level == 2 ? 8 : 5); ++depth) {
   int best = -winScore, bestIndex = selected, alpha = -winScore;
   std::vector<Candidate> completed;
   for (const auto& move : moves) {
    if (search.stop()) break;
    search.board[move.index] = game.turn;
    const int score = -search.negamax(3-game.turn,depth-1,-winScore,-alpha,1);
    search.board[move.index] = 0;
    if (search.interrupted) break;
    completed.push_back({move.index,score});
    if (score > best) { best = score; bestIndex = move.index; }
    alpha = std::max(alpha,score);
   }
   if (search.interrupted) break;
   selected = bestIndex;
   if (best >= winScore-225) break;
   std::stable_sort(completed.begin(),completed.end(),[](const Candidate& left, const Candidate& right) {
    return left.score > right.score;
   });
   moves = std::move(completed);
  }
 }
 if (cancel.load(std::memory_order_relaxed)) return {-1,-1,0};
 return {selected%15,selected/15,game.turn};
}
}
