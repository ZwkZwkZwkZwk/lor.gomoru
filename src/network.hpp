#pragma once
#include "game.hpp"
#include <memory>
#include <string>
namespace gomoku {
class Network {
public:
 Network();
 ~Network();
 Network(const Network&)=delete;
 Network& operator=(const Network&)=delete;
 bool host(int port);
 bool host(int port,const Game& saved);
 bool join(const std::string& ip,int port);
 void tick();
 bool requestMove(int x,int y);
 bool requestRestart();
 bool acceptRestart();
 void disconnect();
 const Game& game() const;
 bool connected() const;
 std::string status() const;
 bool restartPending() const;
 bool restartRequestedByPeer() const;
 int localPlayer() const;
 int port() const;
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
}
