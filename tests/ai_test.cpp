#include "ai.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace gomoku;
using Clock = std::chrono::steady_clock;
int checks = 0;
void check(bool value, const char* name) {
 ++checks;
 if (!value) throw std::runtime_error(name);
}
bool none(const Move& move) { return move.x == -1 && move.y == -1 && move.player == 0; }
bool same(const Game& left, const Game& right) {
 if (left.board != right.board || left.turn != right.turn || left.result != right.result ||
     left.scores != right.scores || left.history.size() != right.history.size() ||
     left.winningLine.size() != right.winningLine.size()) return false;
 for (size_t i = 0; i < left.history.size(); ++i)
  if (left.history[i].x != right.history[i].x || left.history[i].y != right.history[i].y ||
      left.history[i].player != right.history[i].player) return false;
 for (size_t i = 0; i < left.winningLine.size(); ++i)
  if (left.winningLine[i].x != right.winningLine[i].x || left.winningLine[i].y != right.winningLine[i].y ||
      left.winningLine[i].player != right.winningLine[i].player) return false;
 return true;
}
Game middle() {
 Game game;
 for (const auto& point : std::vector<Move>{{7,7},{9,8},{5,6},{8,5},{6,10},{10,6},{4,9},{11,10},
      {9,11},{5,12},{12,4},{3,5},{11,13},{13,9},{2,10},{6,3}})
  check(game.play(point.x,point.y),"middle fixture legal");
 return game;
}
int main() {
 int failures = 0;
 const auto run = [&](const std::string& name, const std::function<void()>& test) {
  try { test(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) { ++failures; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
 };
 run("first move center",[] {
  for (int level = 0; level < 3; ++level) {
   Game game; std::atomic<bool> cancel{false}; const auto move = chooseMove(game,level,cancel);
   check(move.x == 7 && move.y == 7 && move.player == 1,"black starts center");
  }
 });
 run("immediate win all directions and colors",[] {
  for (int player = 1; player <= 2; ++player)
   for (const auto& direction : std::vector<Move>{{1,0},{0,1},{1,1},{1,-1}})
    for (int level = 0; level < 3; ++level) {
     Game game; game.turn = player;
     for (int i = 0; i < 4; ++i) game.board[(7+i*direction.y)*15+5+i*direction.x] = player;
     game.board[(7-direction.y)*15+5-direction.x] = 3-player;
     std::atomic<bool> cancel{false}; const auto move = chooseMove(game,level,cancel);
     check(move.x == 5+4*direction.x && move.y == 7+4*direction.y && move.player == player,"complete unique five");
     check(game.play(move.x,move.y) && game.result == player,"selected move really wins");
    }
 });
 run("win before blocking",[] {
  Game game; game.turn = 2;
  for (int i = 0; i < 4; ++i) { game.board[4*15+i] = 2; game.board[10*15+i] = 1; }
  std::atomic<bool> cancel{false}; const auto move = chooseMove(game,2,cancel);
  check(move.x == 4 && move.y == 4 && move.player == 2,"take own win");
 });
 run("gap wins including overline",[] {
  for (int player = 1; player <= 2; ++player) {
   Game game; game.turn = player;
   for (int x : {2,3,5,6,7}) game.board[7*15+x] = player;
   std::atomic<bool> cancel{false}; const auto move = chooseMove(game,0,cancel);
   check(move.x == 4 && move.y == 7 && move.player == player,"bridge six");
  }
 });
 run("unique defense all directions and colors",[] {
  for (int player = 1; player <= 2; ++player)
   for (const auto& direction : std::vector<Move>{{1,0},{0,1},{1,1},{1,-1}})
    for (int level = 0; level < 3; ++level) {
     Game game; game.turn = player;
     for (int i = 0; i < 4; ++i) game.board[(7+i*direction.y)*15+5+i*direction.x] = 3-player;
     game.board[(7-direction.y)*15+5-direction.x] = player;
     std::atomic<bool> cancel{false}; const auto move = chooseMove(game,level,cancel);
     check(move.x == 5+4*direction.x && move.y == 7+4*direction.y && move.player == player,"block unique five");
    }
 });
 run("creates an open four from three",[] {
  for (int player = 1; player <= 2; ++player)
   for (int level = 0; level < 3; ++level) {
    Game game; game.turn = player;
    for (int x : {5,6,7}) game.board[7*15+x] = player;
    game.board[2*15+2] = 3-player;
    std::atomic<bool> cancel{false}; const auto move = chooseMove(game,level,cancel);
    check((move.x == 4 || move.x == 8) && move.y == 7 && move.player == player,"construct two winning continuations");
   }
 });
 run("terminal and full board return no move",[] {
  std::atomic<bool> cancel{false};
  for (int result = 1; result <= 3; ++result) {
   Game game; game.result = result; const auto started = Clock::now();
   check(none(chooseMove(game,2,cancel)),"terminal sentinel");
   check(std::chrono::duration<double,std::milli>(Clock::now()-started).count()<50,"terminal no search");
  }
  Game full; full.board.fill(1); check(none(chooseMove(full,2,cancel)),"no legal move");
 });
 run("pre-cancelled search",[] {
  auto game = middle(); const auto before = game; std::atomic<bool> cancel{true}; const auto started = Clock::now();
  check(none(chooseMove(game,2,cancel)),"cancel sentinel");
  check(std::chrono::duration<double,std::milli>(Clock::now()-started).count()<50,"cancel prompt");
  check(same(game,before),"cancel preserves all state");
 });
 run("cancellation during search",[] {
  auto game = middle(); const auto before = game; std::atomic<bool> cancel{false}; Move move;
  const auto started = Clock::now(); std::thread worker([&] { move = chooseMove(game,2,cancel); });
  std::this_thread::sleep_for(std::chrono::milliseconds(5)); cancel.store(true);
  worker.join(); const auto elapsed = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
  std::cout << "cancel elapsed_ms=" << elapsed << '\n';
  check(none(move),"active cancel sentinel"); check(elapsed<200,"active cancel responds within tolerance");
  check(same(game,before),"active cancel preserves all state");
 });
 run("difficulty budgets legal immutable moves",[] {
  const int budgets[] = {100,400,1200};
  for (int level = 0; level < 3; ++level) {
   auto game = middle(); game.scores = {2,3,1}; const auto before = game; std::atomic<bool> cancel{false};
   const auto started = Clock::now(); const auto move = chooseMove(game,level,cancel);
   const auto elapsed = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
   std::cout << "difficulty=" << level << " budget_ms=" << budgets[level] << " elapsed_ms=" << elapsed << '\n';
   check(game.at(move.x,move.y)==0 && move.player==game.turn,"returns legal move for correct player");
   check(same(game,before),"search preserves all state"); check(elapsed<=budgets[level]+250,"budget plus 250ms scheduling tolerance");
  }
 });
 run("opening search deadlines",[] {
  const int budgets[] = {100,400,1200};
  for (int level = 0; level < 3; ++level) {
   Game game; check(game.play(7,7),"opening fixture legal"); const auto before = game;
   std::atomic<bool> cancel{false}; const auto started = Clock::now(); const auto move = chooseMove(game,level,cancel);
   const auto elapsed = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
   std::cout << "opening difficulty=" << level << " budget_ms=" << budgets[level] << " elapsed_ms=" << elapsed << '\n';
   check(game.at(move.x,move.y)==0 && move.player==2,"white opening legal");
   check(same(game,before),"opening state preserved"); check(elapsed<=budgets[level]+250,"opening budget tolerance");
  }
 });
 std::cout << "AI " << checks << " checks, " << failures << " failures\n";
 return failures == 0 ? 0 : 1;
}
