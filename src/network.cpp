#include "network.hpp"
#include "storage.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <charconv>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdint>
namespace gomoku {
namespace {
constexpr size_t maxFrame=65536,maxQueue=262144,maxInput=131080;
using Clock=std::chrono::steady_clock;
bool setup(int fd){
 int flags=fcntl(fd,F_GETFL,0);
 if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return false;
 int descriptorFlags=fcntl(fd,F_GETFD,0);
 if(descriptorFlags<0||fcntl(fd,F_SETFD,descriptorFlags|FD_CLOEXEC)<0)return false;
 int yes=1;setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes));
#ifdef SO_NOSIGPIPE
 if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes))<0)return false;
#endif
 return true;
}
void closeSocket(int& fd){if(fd>=0){close(fd);fd=-1;}}
bool tokenValid(const std::string& s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string newToken(){std::random_device random;const char* hex="0123456789abcdef";std::string s(32,'0');for(char& c:s)c=hex[random()&15];return s;}
std::vector<std::string> words(const std::string& s){std::istringstream in(s);std::vector<std::string> out;std::string w;while(in>>w)out.push_back(w);return out;}
bool number(const std::string& s,int& out,int maximum=1000000000){if(s.empty()||!std::all_of(s.begin(),s.end(),[](unsigned char c){return c>='0'&&c<='9';}))return false;auto r=std::from_chars(s.data(),s.data()+s.size(),out);return r.ec==std::errc()&&r.ptr==s.data()+s.size()&&out<=maximum;}
bool address(const std::string& ip,in_addr& out){return ip.find('\0')==std::string::npos&&inet_pton(AF_INET,ip.c_str(),&out)==1;}
}
struct Network::Impl {
 enum class Phase {Idle,Connecting,Hello,Welcome,Ready,Start,Playing};
 Game game;
 int listener=-1,peer=-1,player=0,boundPort=0,round=1,sequence=0,restart=0;
 bool pendingMove=false;
 bool identityCommitted=false;
 Phase phase=Phase::Idle;
 std::string token,input,output,status="未连接";
 std::string endpointIp;
 int endpointPort=0,pingSerial=0,pingOutstanding=0;
 size_t sent=0;
 Clock::time_point began=Clock::now();
 Clock::time_point lastReceived=Clock::now(),lastPing=Clock::now();
 ~Impl(){closeSocket(peer);closeSocket(listener);}
 bool online()const{return phase==Phase::Playing&&peer>=0;}
 void drop(const std::string& reason){closeSocket(peer);phase=Phase::Idle;input.clear();output.clear();sent=0;pendingMove=false;restart=0;pingOutstanding=0;status=reason;}
 void ready(const std::string& message){phase=Phase::Playing;lastReceived=lastPing=Clock::now();pingOutstanding=0;status=message;}
 bool queue(const std::string& payload){
  if(peer<0||payload.empty()||payload.size()>maxFrame)return false;
  if(output.size()-sent+payload.size()+4>maxQueue){drop("发送队列已满，连接断开");return false;}
  if(sent){output.erase(0,sent);sent=0;}
  uint32_t length=htonl(uint32_t(payload.size()));output.append(reinterpret_cast<const char*>(&length),4);output+=payload;return true;
 }
 std::string state()const{return "STATE "+std::to_string(round)+" "+std::to_string(sequence)+" "+std::to_string(restart)+"\n"+serialize(game);}
 bool reject(const std::string& reason){status="请求被拒绝："+reason;return queue("REJECT "+std::to_string(round)+" "+std::to_string(sequence)+" "+reason);}
 void malformed(){drop("协议格式或版本错误");}
 bool parseState(const std::string& payload,const std::vector<std::string>& header,int& nextRound,int& nextSequence,Game& next){
  auto newline=payload.find('\n');std::string error;
  return newline!=std::string::npos&&number(header[header.size()-2],nextRound)&&nextRound>0&&number(header.back(),nextSequence,225)&&deserialize(payload.substr(newline+1),next,error)&&int(next.history.size())==nextSequence;
 }
 void handle(const std::string& payload){
  if(payload.find('\0')!=std::string::npos){malformed();return;}
  auto newline=payload.find('\n');auto header=words(payload.substr(0,newline));
  if(header.empty()){malformed();return;}
  if(phase==Phase::Hello){
   if(newline!=std::string::npos||header.size()!=3||header[0]!="HELLO"||header[1]!="1"||(header[2]!="-"&&!tokenValid(header[2]))){malformed();return;}
   if(identityCommitted){
    if(header[2]!=token){drop("身份不匹配，保留原对局");return;}
   }else if(header[2]=="-")token=newToken();
   else if(token.empty()||header[2]!=token){drop("身份不匹配，保留原对局");return;}
   phase=Phase::Ready;began=Clock::now();queue("WELCOME 1 "+token+" "+std::to_string(round)+" "+std::to_string(sequence)+"\n"+serialize(game));return;
  }
  if(phase==Phase::Welcome){
   int nr=0,ns=0;Game candidate;
   if(header.size()!=5||header[0]!="WELCOME"||header[1]!="1"||!tokenValid(header[2])||(!token.empty()&&header[2]!=token)||!parseState(payload,header,nr,ns,candidate)||(!token.empty()&&nr<round)){malformed();return;}
   token=header[2];round=nr;sequence=ns;game=std::move(candidate);restart=0;phase=Phase::Start;began=Clock::now();queue("READY 1 "+token+" "+std::to_string(round)+" "+std::to_string(sequence));return;
  }
  if(phase==Phase::Ready){
   int nr=0,ns=0;
   if(newline!=std::string::npos||header.size()!=5||header[0]!="READY"||header[1]!="1"||header[2]!=token||!number(header[3],nr)||!number(header[4],ns,225)||nr!=round||ns!=sequence){malformed();return;}
   if(queue("START "+std::to_string(round)+" "+std::to_string(sequence))){identityCommitted=true;ready("已连接：你执黑棋");}return;
  }
  if(phase==Phase::Start){
   int nr=0,ns=0;
   if(newline!=std::string::npos||header.size()!=3||header[0]!="START"||!number(header[1],nr)||!number(header[2],ns,225)||nr!=round||ns!=sequence){malformed();return;}
   ready("已连接：你执白棋");return;
  }
  if(!online()){malformed();return;}
  if(header[0]=="PING"||header[0]=="PONG"){
   int nonce=0;
   if(newline!=std::string::npos||header.size()!=2||!number(header[1],nonce)||nonce==0){malformed();return;}
   if(header[0]=="PING"){queue("PONG "+std::to_string(nonce));return;}
   if(nonce!=pingOutstanding){malformed();return;}
   pingOutstanding=0;return;
  }
  if(player==1){
   int nr=0,ns=0,x=0,y=0;
   bool isMove=header[0]=="MOVE",isRequest=header[0]=="RESTART",isAccept=header[0]=="ACCEPT";
   if(newline!=std::string::npos||(!isMove&&!isRequest&&!isAccept)||header.size()!=(isMove?5u:3u)||!number(header[1],nr)||!number(header[2],ns,225)){malformed();return;}
   if(isMove&&(!number(header[3],x)||!number(header[4],y))){malformed();return;}
   if(nr!=round||ns!=(isMove?sequence+1:sequence)){reject("stale");return;}
   if(isMove){
    if(restart||game.turn!=2||!game.play(x,y)){reject("illegal");return;}
    ++sequence;queue(state());status="已同步对手落子";return;
   }
   if(isRequest){if(restart){reject("pending");return;}restart=2;queue(state());status="对手请求重新开始";return;}
   if(restart!=1){reject("agreement");return;}
   resetRound();return;
  }
  if(header[0]=="REJECT"){
   int nr=0,ns=0;
   if(newline!=std::string::npos||header.size()!=4||!number(header[1],nr)||!number(header[2],ns,225)||nr!=round||ns!=sequence||(header[3]!="stale"&&header[3]!="illegal"&&header[3]!="pending"&&header[3]!="agreement")){malformed();return;}
   pendingMove=false;status="请求被主机拒绝";return;
  }
  int nr=0,ns=0,proposal=0;Game candidate;
  if(header.size()!=4||header[0]!="STATE"||!number(header[1],nr)||nr<1||!number(header[2],ns,225)||!number(header[3],proposal,2)||newline==std::string::npos){malformed();return;}
  std::string error;
  if(!deserialize(payload.substr(newline+1),candidate,error)||int(candidate.history.size())!=ns){malformed();return;}
  Game expected=game;
  if(nr==round){
   if(ns==sequence+1){auto last=candidate.history.back();if(!expected.play(last.x,last.y)){malformed();return;}}
   else if(ns!=sequence){malformed();return;}
  }else if(nr==round+1&&ns==0&&proposal==0){expected.reset();}
  else {malformed();return;}
  if(serialize(expected)!=serialize(candidate)){malformed();return;}
  game=std::move(candidate);round=nr;sequence=ns;restart=proposal;pendingMove=false;status=restart?(restart==player?"等待对手同意重新开始":"对手请求重新开始"):"对局已同步";
 }
 bool resetRound(){
  if(round>=1000000000){drop("局编号已达上限，请重新建房");return false;}
  game.reset();++round;sequence=0;restart=0;pendingMove=false;status="双方同意，已重新开始";return queue(state());
 }
 void flush(){
  size_t budget=131072;
  while(peer>=0&&sent<output.size()&&budget){
   auto count=std::min(budget,output.size()-sent);
#ifdef MSG_NOSIGNAL
   auto n=send(peer,output.data()+sent,count,MSG_NOSIGNAL);
#else
   auto n=send(peer,output.data()+sent,count,0);
#endif
   if(n>0){sent+=size_t(n);budget-=size_t(n);continue;}
   if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return;
   if(n<0&&errno==EINTR)return;
   drop("连接断开，棋局已保留");return;
  }
  if(sent==output.size()){output.clear();sent=0;}
 }
 void receive(){
  char bytes[8192];size_t budget=131072,frames=0;
  while(peer>=0){
   while(input.size()>=4&&frames<64){
    uint32_t length=0;std::memcpy(&length,input.data(),4);length=ntohl(length);
    if(length==0||length>maxFrame){drop("帧长度超过协议限制");return;}
    if(input.size()<size_t(length)+4)break;
    auto payload=input.substr(4,length);input.erase(0,size_t(length)+4);++frames;if(online())lastReceived=Clock::now();handle(payload);
    if(peer<0)return;
   }
   if(frames>=64||budget==0)return;
   auto n=recv(peer,bytes,std::min(sizeof(bytes),budget),0);
   if(n>0){input.append(bytes,size_t(n));budget-=size_t(n);if(input.size()>maxInput){drop("输入缓冲超过限制");return;}continue;}
   if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return;
   drop("连接断开，棋局已保留");return;
  }
 }
 void tick(){
  if(listener>=0){
   for(int i=0;i<8;++i){
    int accepted=accept(listener,nullptr,nullptr);
    if(accepted<0)break;
    if(peer>=0||!setup(accepted)){close(accepted);continue;}
    peer=accepted;phase=Phase::Hello;began=Clock::now();status="正在验证对手身份";
   }
  }
  if(peer<0)return;
  if(phase!=Phase::Playing&&Clock::now()-began>std::chrono::seconds(8)){drop("连接或握手超时");return;}
  if(phase==Phase::Connecting){
   pollfd descriptor{peer,POLLOUT,0};int result=poll(&descriptor,1,0);
   if(result<0){if(errno!=EINTR)drop("连接失败");return;}
   if(result==0)return;
   int error=0;socklen_t length=sizeof(error);
   if(getsockopt(peer,SOL_SOCKET,SO_ERROR,&error,&length)<0||error){drop("连接失败，请检查地址和端口");return;}
   phase=Phase::Welcome;began=Clock::now();queue("HELLO 1 "+(token.empty()?std::string("-"):token));
  }
  flush();if(peer>=0)receive();
  if(online()){
   auto now=Clock::now();
   if(now-lastReceived>=std::chrono::seconds(10)){drop("对手响应超时，棋局已保留，可重连");return;}
   if(!pingOutstanding&&now-lastPing>=std::chrono::seconds(2)){
    pingSerial=pingSerial>=1000000000?1:pingSerial+1;pingOutstanding=pingSerial;lastPing=now;queue("PING "+std::to_string(pingSerial));
   }
  }
  if(peer>=0)flush();
 }
};
Network::Network():impl(new Impl){}
Network::~Network()=default;
bool Network::host(int port,const Game& saved){
 if(impl->peer>=0){impl->status="请先断开连接再续盘建房";return false;}
 Game restored;std::string error;
 if(saved.history.size()>225||!deserialize(serialize(saved),restored,error)||restored.board!=saved.board||restored.turn!=saved.turn||restored.result!=saved.result||restored.winningLine.size()!=saved.winningLine.size()||!std::equal(restored.winningLine.begin(),restored.winningLine.end(),saved.winningLine.begin(),[](const Move& a,const Move& b){return a.x==b.x&&a.y==b.y&&a.player==b.player;})){
  impl->status="存档局面无效，原棋局已保留";return false;
 }
 if(!host(port))return false;
 impl->game=std::move(restored);impl->sequence=int(impl->game.history.size());impl->status="存档已恢复，等待白棋连接";return true;
}
bool Network::host(int port){
 if(port<0||port>65535){impl->status="端口必须在 0–65535 之间";return false;}
 if(impl->listener>=0&&port==impl->boundPort){
  impl->drop("等待白棋连接");impl->player=1;impl->token.clear();impl->identityCommitted=false;impl->game=Game{};impl->round=1;impl->sequence=0;return true;
 }
 int fd=socket(AF_INET,SOCK_STREAM,0);
 if(fd<0){impl->status="无法创建监听套接字";return false;}
 int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);addr.sin_port=htons(uint16_t(port));
 if(!setup(fd)||bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0||listen(fd,8)<0){close(fd);impl->status="无法监听端口";return false;}
 socklen_t length=sizeof(addr);
 if(getsockname(fd,reinterpret_cast<sockaddr*>(&addr),&length)<0){close(fd);impl->status="无法读取监听端口";return false;}
 impl->drop("等待白棋连接");closeSocket(impl->listener);impl->listener=fd;impl->player=1;impl->boundPort=ntohs(addr.sin_port);impl->token.clear();impl->identityCommitted=false;impl->game=Game{};impl->round=1;impl->sequence=0;return true;
}
bool Network::join(const std::string& ip,int port){
 in_addr ipAddress{};
 if(port<1||port>65535||!address(ip,ipAddress)){impl->status="请输入有效的 IPv4 地址和端口";return false;}
 int fd=socket(AF_INET,SOCK_STREAM,0);
 if(fd<0||!setup(fd)){closeSocket(fd);impl->status="无法创建连接套接字";return false;}
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr=ipAddress;addr.sin_port=htons(uint16_t(port));
 int result=connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
 if(result<0&&errno!=EINPROGRESS&&errno!=EINTR){close(fd);impl->status="无法连接主机";return false;}
 if(impl->player!=2||impl->endpointIp!=ip||impl->endpointPort!=port){impl->token.clear();impl->game=Game{};impl->round=1;impl->sequence=0;}
 impl->drop("正在连接主机");closeSocket(impl->listener);impl->player=2;impl->boundPort=port;impl->endpointIp=ip;impl->endpointPort=port;impl->peer=fd;impl->phase=Impl::Phase::Connecting;impl->began=Clock::now();return true;
}
void Network::tick(){impl->tick();}
bool Network::requestMove(int x,int y){
 if(!impl->online()||impl->restart||impl->pendingMove||impl->game.turn!=impl->player||impl->game.result||impl->game.at(x,y)!=0)return false;
 if(impl->player==1){if(!impl->game.play(x,y))return false;++impl->sequence;impl->status="落子已发送";return impl->queue(impl->state());}
 if(!impl->queue("MOVE "+std::to_string(impl->round)+" "+std::to_string(impl->sequence+1)+" "+std::to_string(x)+" "+std::to_string(y)))return false;
 impl->pendingMove=true;impl->status="等待主机确认落子";return true;
}
bool Network::requestRestart(){
 if(!impl->online()||impl->restart||impl->pendingMove)return false;
 if(impl->player==1){impl->restart=1;impl->status="等待对手同意重新开始";return impl->queue(impl->state());}
 if(!impl->queue("RESTART "+std::to_string(impl->round)+" "+std::to_string(impl->sequence)))return false;
 impl->restart=2;impl->status="等待对手同意重新开始";return true;
}
bool Network::acceptRestart(){
 if(!impl->online()||!impl->restart||impl->restart==impl->player||impl->pendingMove)return false;
 if(impl->player==1)return impl->resetRound();
 return impl->queue("ACCEPT "+std::to_string(impl->round)+" "+std::to_string(impl->sequence));
}
void Network::disconnect(){impl->drop(impl->listener>=0?"已断开，等待原对手重连":"已断开，棋局已保留");}
const Game& Network::game()const{return impl->game;}
bool Network::connected()const{return impl->online();}
std::string Network::status()const{return impl->status;}
bool Network::restartPending()const{return impl->restart!=0;}
bool Network::restartRequestedByPeer()const{return impl->restart!=0&&impl->restart!=impl->player;}
int Network::localPlayer()const{return impl->player;}
int Network::port()const{return impl->boundPort;}
}
