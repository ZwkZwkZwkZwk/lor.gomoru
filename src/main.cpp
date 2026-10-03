#include "raylib.h"
#include "game.hpp"
#include "storage.hpp"
#include "ai.hpp"
#include "network.hpp"
#include "ui_state.hpp"
#include <atomic>
#include <future>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <ifaddrs.h>
#include <arpa/inet.h>
using namespace gomoku;
static Font font,titleFont;
static bool blocked=false;
static const Color ink={38,53,31,255},muted={83,99,68,255},accent={78,103,40,255};
static constexpr float titleY=20,titleSize=34,subtitleY=62,subtitleSize=16,logoSize=subtitleY+subtitleSize-titleY;
static Image roundedLogo(const std::filesystem::path&path,int side){
 Image source=LoadImage(path.string().c_str());if(!source.data||source.width<=0||source.height<=0)return {};
 float scale=std::min(float(side)/source.width,float(side)/source.height);int width=std::max(1,int(std::lround(source.width*scale))),height=std::max(1,int(std::lround(source.height*scale)));
 ImageResize(&source,width,height);Image image=GenImageColor(side,side,BLANK);if(!image.data){UnloadImage(source);return {};}
 ImageDraw(&image,source,{0,0,float(width),float(height)},{float(side-width)/2,float(side-height)/2,float(width),float(height)},WHITE);UnloadImage(source);
 auto*pixels=static_cast<Color*>(image.data);float radius=side*0.16f;
 for(int y=0;y<side;++y)for(int x=0;x<side;++x){float dx=std::max(std::abs(x+0.5f-side*0.5f)-(side*0.5f-radius),0.0f),dy=std::max(std::abs(y+0.5f-side*0.5f)-(side*0.5f-radius),0.0f);float coverage=std::clamp(radius+0.5f-std::sqrt(dx*dx+dy*dy),0.0f,1.0f);pixels[y*side+x].a=static_cast<unsigned char>(pixels[y*side+x].a*coverage+0.5f);}
 return image;
}
static Font face(float size){return size>=26?titleFont:font;}
static float fit(const std::string&s,float size,float width){while(width>0&&size>12&&MeasureTextEx(face(size),s.c_str(),size,1).x>width)size-=0.25f;return size;}
static void text(const std::string&s,float x,float y,float size,Color c=ink,float width=0){size=fit(s,size,width);DrawTextEx(face(size),s.c_str(),{x,y},size,1,c);}
static void centered(const std::string&s,Rectangle r,float size,Color c=ink){size=fit(s,std::min(size,r.height-6),r.width-20);auto w=MeasureTextEx(face(size),s.c_str(),size,1);text(s,r.x+(r.width-w.x)/2,r.y+(r.height-w.y)/2,size,c);}
static bool button(const std::string&s,Rectangle r,bool selected=false,bool enabled=true){
 bool hover=!blocked&&enabled&&CheckCollisionPointRec(GetMousePosition(),r);
 Color bg=selected?Color{233,242,209,255}:(hover?Color{226,237,194,255}:Color{244,248,231,255});
 DrawRectangleRounded(r,0.17f,8,bg);if(selected)DrawRectangleRoundedLinesEx(r,0.17f,8,1,accent);
 centered(s,r,20,enabled?ink:Color{115,125,98,255});return hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static void wrap(const std::string&s,float x,float y,float width,float size,Color c=muted){
 int count=0,encodedSize=0;int* cp=LoadCodepoints(s.c_str(),&count);std::string line;
 for(int i=0;i<count;++i){std::string next=line+CodepointToUTF8(cp[i],&encodedSize);if(MeasureTextEx(font,next.c_str(),size,1).x>width&&!line.empty()){text(line,x,y,size,c);y+=size+6;line.clear();}line+=CodepointToUTF8(cp[i],&encodedSize);}
 if(!line.empty())text(line,x,y,size,c);UnloadCodepoints(cp);
}
static void input(std::string&value,Rectangle r,int id,int&focus,size_t max){
 bool hover=!blocked&&CheckCollisionPointRec(GetMousePosition(),r);if(hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus=id;
 DrawRectangleRounded(r,0.13f,6,WHITE);DrawRectangleRoundedLinesEx(r,0.13f,6,1,focus==id?accent:Color{169,184,137,255});
 text(value,r.x+10,r.y+9,20,ink,r.width-20);if(focus==id&&!blocked){int ch=GetCharPressed();while(ch){if(((ch>='0'&&ch<='9')||(id==1&&ch=='.'))&&value.size()<max)value.push_back(char(ch));ch=GetCharPressed();}if(IsKeyPressed(KEY_BACKSPACE)&&!value.empty())value.pop_back();if(IsKeyPressed(KEY_ENTER))focus=0;}
}
static std::string localAddress(){
 ifaddrs* all=nullptr;std::string result="未找到局域网地址";
 if(getifaddrs(&all)==0){for(auto*a=all;a;a=a->ifa_next)if(a->ifa_addr&&a->ifa_addr->sa_family==AF_INET&&std::string(a->ifa_name).rfind("en",0)==0){char buffer[INET_ADDRSTRLEN];auto*addr=reinterpret_cast<sockaddr_in*>(a->ifa_addr);if(inet_ntop(AF_INET,&addr->sin_addr,buffer,sizeof(buffer)))result=buffer;}freeifaddrs(all);}return result;
}
struct Search {uint64_t generation;size_t ply;std::shared_ptr<std::atomic<bool>>cancel;std::future<Move>future;};
int main(int argc,char**argv){
 std::filesystem::path base=GetApplicationDirectory();auto assets=base/"../Resources/assets";if(!std::filesystem::exists(assets))assets=base/"../assets";
 SetConfigFlags(FLAG_WINDOW_HIGHDPI|FLAG_MSAA_4X_HINT);InitWindow(1120,790,"双方要弈棋");SetTargetFPS(60);SetExitKey(KEY_NULL);
 std::ifstream glyphFile(assets/"glyphs.txt");std::string glyphs((std::istreambuf_iterator<char>(glyphFile)),{});int glyphCount=0;int* glyphCodes=LoadCodepoints(glyphs.c_str(),&glyphCount);
 font=LoadFontEx((assets/"NotoSansSC-Medium.ttf").string().c_str(),48,glyphCodes,glyphCount);titleFont=LoadFontEx((assets/"NotoSansSC-SemiBold.ttf").string().c_str(),64,glyphCodes,glyphCount);UnloadCodepoints(glyphCodes);if(!font.texture.id)font=GetFontDefault();if(!titleFont.texture.id)titleFont=GetFontDefault();SetTextureFilter(font.texture,TEXTURE_FILTER_BILINEAR);SetTextureFilter(titleFont.texture,TEXTURE_FILTER_BILINEAR);
 Image logoImage=roundedLogo(assets/"icon-source.png",int(logoSize*2));Texture2D logo{};if(logoImage.data){logo=LoadTextureFromImage(logoImage);UnloadImage(logoImage);}if(logo.id)SetTextureFilter(logo,TEXTURE_FILTER_BILINEAR);
 const float titleLeft=logo.id?50+logoSize+12:50;
 Game local;UiState ui;int mode=0,difficulty=1,focus=0;std::unique_ptr<Network>net=std::make_unique<Network>();
 std::vector<Search> searches;std::string ip="127.0.0.1",portText="34567",message="点击棋盘开始。黑先，无禁手，五子及以上获胜。";
 auto savePath=std::filesystem::path(std::getenv("HOME")?std::getenv("HOME"):".")/"Library/Application Support/GomokuCourse";
 std::error_code ec;std::filesystem::create_directories(savePath,ec);auto saveFile=(savePath/"game.gmk").string();
 double replayTime=0,messageUntil=GetTime()+8;bool showHelp=false;auto cancel=[&](){for(auto&s:searches)s.cancel->store(true);};
 auto announce=[&](const std::string&s){message=s;messageUntil=GetTime()+8;};
 auto changeMode=[&](int next){cancel();ui.changed();net=std::make_unique<Network>();mode=next;focus=0;announce(next==0?"本地双人：双方轮流落子。":next==1?"人机对战：你执黑，AI 执白。":"联网：主机执黑，加入方执白。默认端口 34567。");};
 if(argc>1&&std::string(argv[1])=="--host"){mode=2;net->host(34567);}
 if(argc>2&&std::string(argv[1])=="--join"){mode=2;net->join(argv[2],34567);}
 while(!WindowShouldClose()){
  net->tick();Game&g=local;Game active=mode==2?net->game():g;
  for(size_t i=0;i<searches.size();){if(searches[i].future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto move=searches[i].future.get();if(mode==1)active=ui.resolveAI(local,move,searches[i].generation,searches[i].ply,searches[i].cancel->load());searches.erase(searches.begin()+static_cast<long>(i));}else ++i;}
  if(mode==1&&ui.canPlay(local)&&local.turn==2){bool pending=false;for(auto&s:searches)if(s.generation==ui.generation&&s.ply==local.history.size())pending=true;if(!pending){auto stop=std::make_shared<std::atomic<bool>>(false);Game snapshot=local;int level=difficulty;searches.push_back({ui.generation,local.history.size(),stop,std::async(std::launch::async,[snapshot,level,stop](){return chooseMove(snapshot,level,*stop);})});}}
  if(ui.replay>int(active.history.size()))ui.seek(active,int(active.history.size()));
  if(ui.playing&&GetTime()-replayTime>0.65){ui.advance(active);replayTime=GetTime();}
  if(IsKeyPressed(KEY_ESCAPE)){focus=0;showHelp=false;}
  Game view=ui.replay>=0?active.prefix(ui.replay):active;
  bool allowed=ui.canPlay(active)&&(mode==0||(mode==1&&active.turn==1)||(mode==2&&net->connected()&&active.turn==net->localPlayer()));
  auto cell=boardCell(GetMousePosition().x,GetMousePosition().y);
  if(!showHelp&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&cell.x>=0){focus=0;if(allowed){bool ok=mode==2?net->requestMove(cell.x,cell.y):local.play(cell.x,cell.y);if(!ok)announce("此处不能落子，请选择空交点。");}else announce(ui.replay>=0?"复盘为只读，请先退出复盘。":active.result?"本局已结束，请选择新局。":mode==1?"AI 正在思考，请稍候。":"等待连接或对方落子。");}
  BeginDrawing();ClearBackground(Color{247,250,239,255});blocked=false;
  DrawRectangle(0,0,1120,88,WHITE);if(logo.id)DrawTexturePro(logo,{0,0,float(logo.width),float(logo.height)},{50,titleY,logoSize,logoSize},{0,0},0,WHITE);text("双方要弈棋",titleLeft,titleY,titleSize,accent);text("LOR.GOMOKU  /  15 × 15",titleLeft,subtitleY,subtitleSize,muted);
  text("自由规则 · 黑先 · 五子及以上胜",710,28,20,ink,360);if(button("操作说明",{944,52,126,28},showHelp)){showHelp=!showHelp;focus=0;}blocked=showHelp;
  DrawRectangleRounded({55,110,610,604},0.035f,8,Color{121,139,87,36});DrawRectangleRounded({50,104,610,604},0.035f,8,Color{243,240,213,255});
  for(int y=0;y<15;++y){DrawLineEx({86,138.0f+y*38},{618,138.0f+y*38},1,Color{135,148,91,220});DrawLineEx({86.0f+y*38,138},{86.0f+y*38,670},1,Color{135,148,91,220});text(std::to_string(y+1),57,130.0f+y*38,16,muted);text(std::string(1,char('A'+y)),81.0f+y*38,683,16,muted);}
  for(auto p:std::vector<Move>{{3,3,0},{11,3,0},{7,7,0},{3,11,0},{11,11,0}})DrawCircle(86+p.x*38,138+p.y*38,3,Color{97,114,62,255});
  for(int y=0;y<15;++y)for(int x=0;x<15;++x)if(view.at(x,y)>0){Vector2 p={86.0f+x*38,138.0f+y*38};DrawCircleV({p.x+2,p.y+3},16,Color{91,106,58,60});DrawCircleV(p,16,view.at(x,y)==1?Color{31,35,33,255}:Color{250,250,247,255});DrawCircleLines(int(p.x),int(p.y),16,Color{122,133,102,255});DrawCircleV({p.x-4,p.y-5},5,view.at(x,y)==1?Color{61,66,62,255}:Color{255,255,253,255});}
  if(!view.history.empty()){auto m=view.history.back();DrawCircleLines(86+m.x*38,138+m.y*38,6,Color{218,91,66,255});}
  if(!view.winningLine.empty()){auto a=view.winningLine.front(),b=view.winningLine.back();DrawLineEx({86.0f+a.x*38,138.0f+a.y*38},{86.0f+b.x*38,138.0f+b.y*38},4,Color{172,64,47,230});}
  if(!showHelp&&allowed&&cell.x>=0&&active.at(cell.x,cell.y)==0)DrawCircle(86+cell.x*38,138+cell.y*38,15,active.turn==1?Color{25,30,28,85}:Color{255,255,242,130});
  DrawRectangleRounded({704,104,366,604},0.05f,8,WHITE);
  for(int m=0;m<3;++m)if(button(m==0?"双人对弈":m==1?"挑战 AI":"局域网",{724.0f+m*112,122,106,42},mode==m)){if(mode!=m)changeMode(m);}
  if(mode==1){text("思考预算",725,177,18,muted);for(int d=0;d<3;++d)if(button(d==0?"初级 0.1s":d==1?"中级 0.4s":"高级 1.2s",{724.0f+d*112,199,106,34},difficulty==d)){difficulty=d;cancel();ui.changed();}}
  else text(mode==0?"面对面，轮流执黑与执白。":"主机权威同步 · TCP · 断线可恢复",725,187,18,muted,326);
  DrawRectangleRounded({724,250,326,95},0.1f,8,Color{241,246,226,255});
  std::string status=ui.replay>=0?"复盘 "+std::to_string(ui.replay)+" / "+std::to_string(active.history.size()):active.result==1?"黑棋获胜":active.result==2?"白棋获胜":active.result==3?"满盘和棋":active.turn==1?"黑棋落子":"白棋落子";
  DrawCircle(748,279,11,indicatorPlayer(view)==1?Color{20,25,23,255}:Color{250,250,247,255});DrawCircleLines(748,279,11,muted);text(status,771,264,28,accent,258);
  text("第 "+std::to_string(active.history.size())+" 手"+(mode==1&&active.turn==2&&!active.result&&ui.replay<0?"  ·  AI 思考中":""),742,310,18,muted,290);
  text("累计比分",725,365,18,muted);for(int i=0;i<3;++i){Rectangle r={724.0f+i*112,395,106,65};DrawRectangleRounded(r,0.12f,8,Color{244,248,231,255});text(i==0?"黑胜":i==1?"白胜":"和棋",r.x+12,r.y+8,18,muted);text(std::to_string(active.scores[i]),r.x+12,r.y+32,24,accent,82);}
  if(mode==2){text("加入地址 / 端口",725,472,18,muted);
   if(button("加入新房",{951,467,99,25})){int freshPort=0;if(!parsePort(portText,freshPort))announce("请输入完整有效的端口数字。");else{net=std::make_unique<Network>();ui.changed();if(!net->join(ip,freshPort))announce(net->status());else announce("已离开旧房间，以新身份加入。");}}input(ip,{724,498,216,40},1,focus,15);input(portText,{948,498,102,40},2,focus,5);int port=0;bool portValid=parsePort(portText,port);
   if(button("创建房间",{724,550,158,38})){if(!portValid)announce("端口应在 1 到 65535 之间。");else{net=std::make_unique<Network>();ui.changed();if(!net->host(port))announce(net->status());else announce("房间已创建，本机地址："+localAddress());}}
   if(button("加入 / 重连",{892,550,158,38})){if(!portValid)announce("端口应在 1 到 65535 之间。");else{ui.changed();if(!net->join(ip,port))announce(net->status());}}
   if(button(net->restartRequestedByPeer()?"同意重开":net->restartPending()?"等待同意":"请求重开",{724,600,158,38},net->restartPending(),net->connected()&&(!net->restartPending()||net->restartRequestedByPeer()))){bool ok=net->restartRequestedByPeer()?net->acceptRestart():net->requestRestart();announce(ok?"重开请求已处理。":"等待对方同意重开。");}
   if(button("断开连接",{892,600,158,38})){net->disconnect();ui.changed();announce("已断开；加入方可用原地址重连。");}
   wrap(net->status(),724,649,326,16);text("本机地址："+localAddress(),710,716,16,muted,360);
  }else{wrap(ui.replay>=0?"复盘不改变原棋局与比分。退出后可继续未结束的对局。":"棋盘交点落子。红圈表示最后一手，胜线出现后本局停止。",724,486,326,20);
   text("保存只占用一个槽位",724,659,18,muted);}
  if(ui.replay>=0){
   if(button("首",{50,724,64,40})){ui.seek(active,0);}
   if(button("前",{123,724,64,40})){ui.seek(active,ui.replay-1);}
   if(button(ui.playing?"暂停":"播放",{196,724,96,40},ui.playing)){if(ui.replay==int(active.history.size()))ui.seek(active,0);ui.playing=!ui.playing;replayTime=GetTime();}
   if(button("后",{301,724,64,40})){ui.seek(active,ui.replay+1);}
   if(button("末",{374,724,64,40})){ui.seek(active,int(active.history.size()));}
   if(button("退出复盘",{447,724,213,40})){ui.changed();}
  }else{
   if(button("新局",{50,724,143,40},false,mode!=2)){cancel();ui.changed();local.reset();announce("新局开始，累计比分保留。");}
   if(button("保存",{205,724,143,40})){std::string error;announce(saveGame(saveFile,active,error)?"已保存完整棋局，可退出后继续。":error);}
   if(button(mode==2?"续盘建房":"继续存档",{360,724,143,40},false,mode!=2||!net->connected())){
    Game candidate;std::string error;
    if(!loadGame(saveFile,candidate,error))announce(error);
    else if(mode==2){int restorePort=0;if(!parsePort(portText,restorePort))announce("请输入完整有效的端口数字。");else{net=std::make_unique<Network>();ui.changed();if(net->host(restorePort,candidate))announce("存档已校验，续盘房间已创建，等待对手加入新房。");else announce(net->status());}}
    else{cancel();ui.changed();local=std::move(candidate);announce("存档已校验，继续对局。");}
   }
   if(button("复盘",{515,724,145,40},false,!active.history.empty())){cancel();ui.enterReplay(active);}
  }
  if(GetTime()<messageUntil)wrap(message,710,748,360,16,accent);
  if(showHelp){blocked=false;DrawRectangle(0,88,1120,702,Color{238,244,222,238});DrawRectangleRounded({165,130,790,558},0.04f,8,WHITE);text("操作说明",201,170,32,accent);wrap("双人：同一台电脑轮流落子。AI：你执黑，可选择三个思考预算。非法落子不改变回合。新局保留累计比分。保存写入一个存档槽，继续存档会替换当前局。",201,230,710,20);wrap("复盘：首、前、后、末逐手浏览，播放每 0.65 秒前进一步；复盘中棋盘只读。LAN：主机先建房，另一端填写主机 IPv4 和相同端口后加入；主机黑，客方白。双方同意重开。断线时停止落子，客方在同一程序中点重连恢复。关闭后续盘：主机用续盘建房恢复存档，客方选择加入新房。",201,348,710,20);wrap("本机双实例可用 127.0.0.1。真实局域网需同网段与系统允许网络访问；本程序不会修改防火墙。",201,527,710,18);if(button("明白了",{761,623,151,40}))showHelp=false;}
  EndDrawing();
 }
 cancel();for(auto&s:searches)s.future.wait();net.reset();if(logo.id)UnloadTexture(logo);UnloadFont(font);UnloadFont(titleFont);CloseWindow();return 0;
}
