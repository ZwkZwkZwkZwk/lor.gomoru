#pragma once
#include "game.hpp"
#include <string>
namespace gomoku {
std::string serialize(const Game&);
bool deserialize(const std::string&,Game&,std::string&);
bool saveGame(const std::string&,const Game&,std::string&);
bool loadGame(const std::string&,Game&,std::string&);
}
