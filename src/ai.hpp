#pragma once
#include "game.hpp"
#include <atomic>
namespace gomoku {
Move chooseMove(const Game& game, int difficulty, std::atomic<bool>& cancel);
}
