#include "network.hpp"
#include "storage.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <sstream>
#include <vector>
using namespace gomoku;
static void check(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
static bool pump(Network& h,Network& c,const std::function<bool()>& done,int limit=1500){
 for(int i=0;i<limit;++i){h.tick();c.tick();if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;
}
static void settle(Network& h,Network& c,int count=30){pump(h,c,[]{return false;},count);}
static void link(Network& h,Network& c){
 check(h.host(0),"host binds a real localhost socket");check(h.port()>0,"port zero resolves to allocated port");
 check(c.join("127.0.0.1",h.port()),"join starts asynchronously");
 check(pump(h,c,[&]{return h.connected()&&c.connected();}),"handshake readies both peers");
}
static void move(Network& h,Network& c,int x,int y){
 auto& peer=h.game().turn==1?h:c;auto size=h.game().history.size();
 check(peer.requestMove(x,y),"correct player move accepted");
 check(pump(h,c,[&]{return h.game().history.size()==size+1&&serialize(h.game())==serialize(c.game());}),"authoritative move reaches both peers");
}
static std::string frame(const std::string& payload){uint32_t n=htonl(uint32_t(payload.size()));std::string out(4,'\0');std::memcpy(out.data(),&n,4);return out+payload;}
struct Raw {
 int fd=-1;std::string input;std::vector<std::string> messages;bool closed=false;
 explicit Raw(int port){fd=socket(AF_INET,SOCK_STREAM,0);check(fd>=0,"raw socket creation");sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(port));inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);check(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"raw socket connect");fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);
#ifdef SO_NOSIGPIPE
 int yes=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
 }
 ~Raw(){if(fd>=0)close(fd);}
 void bytes(const std::string& b){size_t at=0;while(at<b.size()){
#ifdef MSG_NOSIGNAL
 auto n=send(fd,b.data()+at,b.size()-at,MSG_NOSIGNAL);
#else
 auto n=send(fd,b.data()+at,b.size()-at,0);
#endif
 check(n>0,"raw test writes complete small payload");at+=size_t(n);}}
 void sendFrame(const std::string& s){bytes(frame(s));}
 void read(){char b[8192];for(;;){auto n=recv(fd,b,sizeof(b),0);if(n==0){closed=true;break;}if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;closed=true;break;}input.append(b,size_t(n));}while(input.size()>=4){uint32_t n;std::memcpy(&n,input.data(),4);n=ntohl(n);check(n<=65536,"server output frame bounded");if(input.size()<4+n)break;messages.push_back(input.substr(4,n));input.erase(0,4+n);}}
};
static bool rawPump(Network& h,Raw& r,const std::function<bool()>& done,int limit=1000){for(int i=0;i<limit;++i){h.tick();r.read();if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;}
struct Handshake {std::string token;int round=0,seq=0;};
static Handshake rawLink(Network& h,Raw& r,bool split=false,const std::string& resumeToken="-"){
 auto hello=frame("HELLO 1 "+resumeToken);
 if(split){for(char c:hello){r.bytes(std::string(1,c));h.tick();}}else r.bytes(hello);
 check(rawPump(h,r,[&]{return !r.messages.empty();}),"raw HELLO receives WELCOME");
 Handshake data;std::istringstream in(r.messages.front().substr(0,r.messages.front().find('\n')));std::string verb;int version=0;
 check(bool(in>>verb>>version>>data.token>>data.round>>data.seq)&&verb=="WELCOME"&&version==1,"WELCOME carries version/token/round/sequence");
 r.sendFrame("READY 1 "+data.token+" "+std::to_string(data.round)+" "+std::to_string(data.seq));
 check(rawPump(h,r,[&]{return h.connected()&&r.messages.size()>=2;}),"raw READY receives START");return data;
}
static void ordinary(){
 Network h,c;check(!h.requestMove(7,7),"offline moves rejected");check(!c.join("localhost",12345),"hostname rejected");check(!c.join("300.1.1.1",12345),"invalid IPv4 rejected");check(!h.host(-1),"negative port rejected");check(!c.join("127.0.0.1",0),"zero client port rejected");
 link(h,c);check(h.localPlayer()==1&&c.localPlayer()==2,"host black/client white");check(!c.requestMove(0,0),"wrong turn locally rejected");
 move(h,c,7,7);check(!h.requestMove(8,8),"black cannot play twice");check(!c.requestMove(7,7),"occupied point rejected");check(!c.requestMove(15,0),"out of board rejected");move(h,c,8,7);
 auto saved=serialize(h.game());c.disconnect();check(!c.connected()&&!c.requestMove(9,9),"disconnect disables client move");
 check(pump(h,c,[&]{return !h.connected();}),"host observes disconnect");check(h.port()>0&&!h.requestMove(0,0),"host listener survives and offline move rejected");check(serialize(h.game())==saved&&serialize(c.game())==saved,"disconnect preserves both games");
 Network intruder;check(intruder.join("127.0.0.1",h.port()),"new identity connects TCP");settle(h,intruder,100);check(!h.connected()&&!intruder.connected(),"new identity cannot take retained session");
 check(c.join("127.0.0.1",h.port()),"same client reconnect begins");check(pump(h,c,[&]{return h.connected()&&c.connected();}),"same token reconnects");check(serialize(c.game())==saved,"reconnect synchronizes retained snapshot");
 check(c.requestRestart(),"white can propose restart");check(pump(h,c,[&]{return h.restartPending()&&c.restartPending();}),"restart proposal synchronizes");check(!c.acceptRestart(),"requester cannot self-accept");check(!h.requestMove(2,2),"moves disabled during restart agreement");check(h.acceptRestart(),"recipient accepts restart");check(pump(h,c,[&]{return h.game().history.empty()&&c.game().history.empty()&&!c.restartPending();}),"agreement resets both games");
 for(int x=0;x<5;++x){move(h,c,x,0);if(x<4)move(h,c,x,1);}check(h.game().result==1&&c.game().scores[0]==1,"win and score synchronize");
 check(h.requestRestart(),"black can propose restart");check(pump(h,c,[&]{return c.restartPending();}),"black proposal reaches white");check(c.acceptRestart(),"white accepts black proposal");check(pump(h,c,[&]{return h.game().history.empty()&&c.game().history.empty();}),"second round resets");check(h.game().scores[0]==1&&c.game().scores[0]==1,"restart preserves scores");
 std::cout<<"PASS ordinary synchronization, identity reconnect, mutual restart, scores\n";
}
static void rawProtocol(){
 Network h;check(h.host(0),"raw host binds");Raw r(h.port());auto session=rawLink(h,r,true);check(h.requestMove(0,0),"raw session host black move");check(rawPump(h,r,[&]{return h.game().history.size()==1&&r.messages.size()>=3;}),"raw peer receives black state");
 auto m=[&](int round,int seq,int x,int y){return "MOVE "+std::to_string(round)+" "+std::to_string(seq)+" "+std::to_string(x)+" "+std::to_string(y);};
 r.bytes(frame(m(session.round,2,1,0))+frame(m(session.round,2,1,0))+frame(m(session.round,99,2,0))+frame(m(session.round-1,3,2,0))+frame(m(session.round,3,2,0)));
 check(rawPump(h,r,[&]{return r.messages.size()>=8;}),"coalesced valid and invalid commands all answered");check(h.connected()&&h.game().history.size()==2&&h.game().at(2,0)==0,"duplicate, bad sequence, old round, wrong turn leave game unchanged");
 auto saved=serialize(h.game());r.sendFrame("STATE 1 9 0\n"+serialize(Game{}));check(rawPump(h,r,[&]{return !h.connected();}),"client snapshot rejected");check(serialize(h.game())==saved,"snapshot injection preserves authoritative game");
 std::cout<<"PASS split/coalesced frames, duplicate/sequence/round/turn checks, snapshot injection\n";
}
static void badHandshake(const std::string& payload){Network h;check(h.host(0),"malformed host binds");Raw r(h.port());r.sendFrame(payload);check(rawPump(h,r,[&]{return r.closed;}),"malformed handshake disconnects");check(!h.connected()&&h.game().history.empty(),"malformed handshake preserves game");}
static void malformed(){
 badHandshake("HELLO 2 -");badHandshake("HELLO 1 - extra");badHandshake("HELLO 1 x");badHandshake("MOVE 1 1 0 0");badHandshake(std::string("HELLO 1 -\0garbage",17));
 badHandshake("PING 1");badHandshake("PONG 1");
 for(uint32_t size:{0u,65537u,0xffffffffu}){Network h;check(h.host(0),"oversize host binds");Raw r(h.port());auto net=htonl(size);std::string b(4,'\0');std::memcpy(b.data(),&net,4);r.bytes(b);check(rawPump(h,r,[&]{return r.closed;}),"invalid frame length disconnects before payload");check(h.game().history.empty(),"invalid frame preserves game");}
 Network h;check(h.host(0),"malformed command host binds");Raw r(h.port());rawLink(h,r);r.sendFrame("MOVE 1 1 0 0 trailing");check(rawPump(h,r,[&]{return r.closed;}),"extra command arguments disconnect");
 Network stale;check(stale.host(0),"stale malformed host binds");Raw invalid(stale.port());rawLink(stale,invalid);invalid.sendFrame("MOVE 1 99 nope 0");check(rawPump(stale,invalid,[&]{return invalid.closed;}),"malformed coordinates checked even with stale sequence");
 std::cout<<"PASS malformed/version/length limits\n";
}
static void lifecycle(){
 Network h,c;link(h,c);move(h,c,3,3);
 Raw outsider(h.port());outsider.sendFrame("HELLO 1 -");check(rawPump(h,outsider,[&]{return outsider.closed;}),"active session rejects third socket");check(h.connected()&&c.connected(),"outsider leaves active session connected");
 check(h.requestRestart(),"restart can be proposed before disconnect");check(pump(h,c,[&]{return c.restartPending();}),"restart visible before disconnect");h.disconnect();check(pump(h,c,[&]{return !c.connected();}),"client observes host disconnect");check(!h.restartPending()&&!c.restartPending(),"disconnect cancels unfinished restart consent");
 check(c.join("127.0.0.1",h.port()),"client can resume host-initiated disconnect");check(pump(h,c,[&]{return h.connected()&&c.connected();}),"host listener reaccepts identity");check(h.game().history.size()==1&&serialize(h.game())==serialize(c.game()),"cancelled restart does not reset board");
 c.disconnect();check(pump(h,c,[&]{return !h.connected();}),"disconnect acknowledged before partial frame");
 Raw partial(h.port());partial.bytes(frame("HELLO 1 -").substr(0,6));h.tick();check(!h.connected()&&h.game().history.size()==1,"partial handshake does not mutate game");
 std::cout<<"PASS active-peer isolation, host disconnect, pending restart cancellation, partial frames\n";
}
static void backpressure(){
 Network h;check(h.host(0),"backpressure host binds");Raw r(h.port());rawLink(h,r);
 int small=1024;check(setsockopt(r.fd,SOL_SOCKET,SO_RCVBUF,&small,sizeof(small))==0,"raw receiver window reduced");
 auto request=frame("MOVE 1 1 0 0");std::string burst;for(int i=0;i<256;++i)burst+=request;
 size_t written=0;
 for(int i=0;i<20000&&h.connected();++i){
#ifdef MSG_NOSIGNAL
  auto n=send(r.fd,burst.data(),burst.size(),MSG_NOSIGNAL);
#else
  auto n=send(r.fd,burst.data(),burst.size(),0);
#endif
  if(n>0)written+=size_t(n);h.tick();
 }
 check(written>262144,"backpressure sends more than output cap worth of traffic");check(!h.connected(),"stalled receiver eventually disconnects at bounded output cap");check(h.game().history.empty(),"backpressure never accepts wrong-player move");
 std::cout<<"PASS bounded output under stalled receiver ("<<written<<" input bytes)\n";
}
static void rehost(){
 Network h,c;link(h,c);move(h,c,2,2);int original=h.port();h.disconnect();check(pump(h,c,[&]{return !c.connected();}),"old client observes rehost disconnect");
 check(h.host(original),"explicit rehost reuses retained listener port");check(h.port()==original&&h.game().history.empty(),"new room clears board and retains requested port");
 Network fresh;check(fresh.join("127.0.0.1",original),"fresh identity joins explicitly new room");check(pump(h,fresh,[&]{return h.connected()&&fresh.connected();}),"new room accepts new session identity");
 std::cout<<"PASS explicit same-port rehost opens a fresh session\n";
}
static void restartIdentity(){
 Network h,c;link(h,c);check(!h.restartRequestedByPeer()&&!c.restartRequestedByPeer(),"no peer restart initially");
 check(c.requestRestart(),"client proposes restart for UI state");check(pump(h,c,[&]{return h.restartPending();}),"client proposal reaches host UI");
 check(h.restartRequestedByPeer()&&!c.restartRequestedByPeer(),"recipient can show agree and requester can show waiting");check(h.acceptRestart(),"host accepts peer proposal for UI state");check(pump(h,c,[&]{return !c.restartPending();}),"accepted proposal clears client UI");
 check(h.requestRestart(),"host proposes second UI restart");check(pump(h,c,[&]{return c.restartPending();}),"host proposal reaches client UI");check(c.restartRequestedByPeer()&&!h.restartRequestedByPeer(),"UI roles work in either direction");
 std::cout<<"PASS restart requester identity exposed to GUI\n";
}
static void newEndpoint(){
 Network first,c;link(first,c);move(first,c,4,4);c.disconnect();check(pump(first,c,[&]{return !first.connected();}),"old endpoint disconnected");
 Network second;check(second.host(0),"second room binds distinct port");check(second.port()!=first.port(),"test rooms have different endpoints");check(c.join("127.0.0.1",second.port()),"same client switches room");check(pump(second,c,[&]{return second.connected()&&c.connected();}),"changed endpoint creates fresh session identity");check(c.game().history.empty(),"new endpoint loads its own fresh board");
 std::cout<<"PASS changing endpoint clears old client identity\n";
}
static void heartbeat(){
 Network h;check(h.host(0),"heartbeat host binds");Raw r(h.port());auto session=rawLink(h,r);check(h.requestMove(4,4),"heartbeat fixture has retained board");check(rawPump(h,r,[&]{return r.messages.size()>=3;}),"heartbeat fixture receives state");std::string before=h.status();
 check(rawPump(h,r,[&]{return r.messages.size()>=4;},4000),"Playing emits heartbeat after idle interval");std::istringstream in(r.messages.back());std::string verb,nonce,extra;check(bool(in>>verb>>nonce)&&verb=="PING"&&!(in>>extra),"raw peer receives version-1 PING");
 r.sendFrame("PONG "+nonce);check(rawPump(h,r,[&]{return h.connected();},5),"matching PONG preserves connection");check(h.status()==before,"heartbeats do not replace game status");
 auto began=std::chrono::steady_clock::now();check(rawPump(h,r,[&]{return !h.connected();},12000),"silent established peer disconnects without OS TCP close");auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();check(seconds>=9.5&&seconds<12,"silent peer timeout is approximately ten seconds");check(h.game().history.size()==1&&h.game().at(4,4)==1,"heartbeat timeout preserves game");Raw recovered(h.port());auto resumed=rawLink(h,recovered,false,session.token);check(h.connected()&&resumed.seq==1,"heartbeat timeout keeps listener and session identity resumable");
 Network host,client;link(host,client);before=host.status();auto end=std::chrono::steady_clock::now()+std::chrono::seconds(11);while(std::chrono::steady_clock::now()<end){host.tick();client.tick();check(host.connected()&&client.connected(),"mutual PING/PONG keeps idle pair connected");std::this_thread::sleep_for(std::chrono::milliseconds(2));}check(host.status()==before,"healthy idle heartbeats preserve status");
 std::cout<<"PASS idle PING/PONG and ten-second silent-peer timeout\n";
}
static Handshake welcomeOnly(Network& h,Raw& r){
 r.sendFrame("HELLO 1 -");check(rawPump(h,r,[&]{return !r.messages.empty();}),"uncommitted peer receives WELCOME");
 Handshake data;std::istringstream in(r.messages.front().substr(0,r.messages.front().find('\n')));std::string verb;int version=0;check(bool(in>>verb>>version>>data.token>>data.round>>data.seq)&&verb=="WELCOME"&&version==1,"provisional WELCOME identity parsed");return data;
}
static void abandon(Network& h,Raw& r){
 close(r.fd);r.fd=-1;Network unused;settle(h,unused,30);check(!h.connected(),"abandoned first handshake never becomes connected");
}
static void pendingBeforeWelcome(){
 Network h;check(h.host(0),"pending-before-WELCOME host binds");Raw first(h.port());first.sendFrame("HELLO 1 -");Network unused;settle(h,unused,30);check(first.messages.empty(),"first peer does not read WELCOME before interruption");abandon(h,first);
 Network fresh;check(fresh.join("127.0.0.1",h.port()),"fresh client retries unused room");check(pump(h,fresh,[&]{return h.connected()&&fresh.connected();}),"interruption before WELCOME does not reserve unused room");
 std::cout<<"PASS interruption before WELCOME permits fresh retry\n";
}
static void pendingAfterWelcome(){
 Network h;check(h.host(0),"pending-after-WELCOME host binds");Raw first(h.port());auto pending=welcomeOnly(h,first);abandon(h,first);
 Network fresh;check(fresh.join("127.0.0.1",h.port()),"fresh client enters abandoned provisional room");check(pump(h,fresh,[&]{return h.connected()&&fresh.connected();}),"abandoned WELCOME identity can be replaced before READY");
 fresh.disconnect();check(pump(h,fresh,[&]{return !h.connected();}),"committed replacement client disconnects");Raw obsolete(h.port());obsolete.sendFrame("HELLO 1 "+pending.token);check(rawPump(h,obsolete,[&]{return obsolete.closed;}),"replaced provisional token cannot take over committed session");
 check(fresh.join("127.0.0.1",h.port()),"committed replacement reconnect begins");check(pump(h,fresh,[&]{return h.connected()&&fresh.connected();}),"committed replacement retains exclusive reconnect identity");
 std::cout<<"PASS abandoned WELCOME replacement and established identity protection\n";
}
static void pendingTokenRetry(){
 Network h;check(h.host(0),"provisional-token retry host binds");Raw first(h.port());auto pending=welcomeOnly(h,first);abandon(h,first);Raw retry(h.port());auto restored=rawLink(h,retry,false,pending.token);check(restored.token==pending.token&&h.connected(),"original provisional token can finish interrupted handshake");abandon(h,retry);
 Network outsider;check(outsider.join("127.0.0.1",h.port()),"outsider connects after original identity commits");settle(h,outsider,50);check(!h.connected()&&!outsider.connected(),"READY commitment forbids fresh identity takeover");
 std::cout<<"PASS provisional token retry commits only on READY\n";
}
static void restoredLobby(){
 Game saved;check(saved.play(7,7)&&saved.play(8,7)&&saved.play(7,8),"saved partial LAN fixture legal");saved.scores={4,2,1};std::string error;Game persisted;check(deserialize(serialize(saved),persisted,error),"saved LAN fixture survives storage roundtrip");
 Network h,c;check(h.host(0,persisted),"new host can restore validated saved lobby before connection");check(serialize(h.game())==serialize(saved),"host restores history and scores before join");check(c.join("127.0.0.1",h.port()),"fresh identity joins restored room");check(pump(h,c,[&]{return h.connected()&&c.connected();}),"restored room handshakes");check(serialize(c.game())==serialize(saved)&&c.game().turn==2,"fresh client receives saved snapshot and white turn");
 auto before=serialize(h.game());check(!h.host(h.port(),Game{}),"saved restore cannot overwrite connected authoritative room");check(h.connected()&&serialize(h.game())==before,"rejected online restore preserves room and game");move(h,c,8,8);check(h.game().history.size()==4&&h.game().scores==saved.scores,"restored sequence continues without resetting scores");
 c.disconnect();check(pump(h,c,[&]{return !h.connected();}),"restored client disconnects");check(c.join("127.0.0.1",h.port()),"restored session token reconnect begins");check(pump(h,c,[&]{return h.connected()&&c.connected();}),"restored session reconnects");check(serialize(h.game())==serialize(c.game()),"restored session reconnect snapshot stays coherent");
 c.disconnect();check(pump(h,c,[&]{return !h.connected();}),"offline restore validation fixture disconnects");before=serialize(h.game());Game invalid=persisted;invalid.board[0]=2;check(!h.host(h.port(),invalid),"inconsistent saved board rejected");check(serialize(h.game())==before,"invalid saved board preserves previous lobby");invalid=persisted;invalid.turn=1;check(!h.host(h.port(),invalid),"inconsistent saved turn rejected");invalid=persisted;invalid.scores[0]=-1;check(!h.host(h.port(),invalid),"negative saved score rejected");invalid=persisted;invalid.history.push_back({7,7,2});check(!h.host(h.port(),invalid),"illegal saved history rejected");check(serialize(h.game())==before,"all invalid restore cases leave original authoritative game intact");
 Game terminal;for(int x=0;x<5;++x){check(terminal.play(x,0),"terminal restored black fixture");if(x<4)check(terminal.play(x,1),"terminal restored white fixture");}terminal.scores={7,3,2};Game storedTerminal;check(deserialize(serialize(terminal),storedTerminal,error),"terminal saved game storage roundtrip");
 Network terminalHost,terminalClient;check(terminalHost.host(0,storedTerminal),"terminal saved game restores into new host");check(terminalClient.join("127.0.0.1",terminalHost.port()),"fresh client joins terminal restore");check(pump(terminalHost,terminalClient,[&]{return terminalHost.connected()&&terminalClient.connected();}),"terminal restore handshake synchronizes");check(serialize(terminalHost.game())==serialize(terminal)&&serialize(terminalClient.game())==serialize(terminal),"terminal restore does not rescore win");check(!terminalHost.requestMove(5,0)&&!terminalClient.requestMove(5,1),"restored terminal game rejects further moves");
 terminalClient.disconnect();check(pump(terminalHost,terminalClient,[&]{return !terminalHost.connected();}),"terminal restore client disconnects");check(terminalClient.join("127.0.0.1",terminalHost.port()),"terminal restored token reconnect begins");check(pump(terminalHost,terminalClient,[&]{return terminalHost.connected()&&terminalClient.connected();}),"terminal restored token reconnects");check(terminalClient.game().scores==terminal.scores,"terminal reconnect never resettles scores");check(terminalHost.requestRestart(),"restored terminal host proposes restart");check(pump(terminalHost,terminalClient,[&]{return terminalClient.restartPending();}),"restored terminal proposal synchronizes");check(terminalClient.acceptRestart(),"restored terminal peer agrees restart");check(pump(terminalHost,terminalClient,[&]{return terminalHost.game().history.empty()&&terminalClient.game().history.empty();}),"restored terminal can start next round");check(terminalHost.game().scores==terminal.scores&&terminalClient.game().scores==terminal.scores,"restart after terminal restore preserves saved totals");
 std::cout<<"PASS saved LAN lobby restore, legal continuation, validation, terminal scores/reconnect/restart\n";
}
int main(int argc,char** argv){
 std::string mode=argc>1?argv[1]:"all";
 if(mode=="restart"){restartIdentity();return 0;}if(mode=="endpoint"){newEndpoint();return 0;}if(mode=="heartbeat"){heartbeat();return 0;}
 if(mode=="pending-before"){pendingBeforeWelcome();return 0;}if(mode=="pending-after"){pendingAfterWelcome();return 0;}if(mode=="pending-retry"){pendingTokenRetry();return 0;}if(mode=="restore"){restoredLobby();return 0;}
 ordinary();rawProtocol();malformed();lifecycle();backpressure();rehost();restartIdentity();newEndpoint();heartbeat();pendingBeforeWelcome();pendingAfterWelcome();pendingTokenRetry();restoredLobby();std::cout<<"All network tests passed\n";
}
