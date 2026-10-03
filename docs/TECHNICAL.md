# 实现要点

当前源码对应应用v1.3，C++17、15×15自由五子棋。规则是连续至少五子获胜，没有三三/四四禁手。

## 模块

| 文件 | 责任 |
|---|---|
| `src/game.*` | board/history/turn/result/scores；合法落子、四方向胜线、重开与历史前缀 |
| `src/storage.*` | GOMOKU 1棋谱文本、FNV-1a检错、逐手验证、随机临时文件完整写入后rename |
| `src/ui_state.*` | 复盘、坐标、端口、终局身份、旧AI结果拒绝 |
| `src/ai.*` | 棋局副本上的限时走法选择 |
| `src/network.*` | 非阻塞TCP状态机、主机裁决、身份与重连 |
| `src/main.cpp` | raylib资源、每帧调度与用户操作 |

棋盘索引`y*15+x`，0空、1黑、2白。`Game::play`失败前不改状态；终局后拒绝新手，所以只结算一次比分。`reset`保留比分。复盘通过`prefix`重建显示副本，保留真实局。

## AI

三档预算100/400/1200ms；根候选最多12/12/18，中高级内层最多8/10，最大深度5/8（含根落子）。立即赢优先于挡对手立即赢，战术扫描覆盖全盘；普通候选位于已有子周围5×5邻域。

五格窗口权重为0、2、24、350、16000、2000000，普通候选按2倍本方shape、3倍对方shape和中心项排序。它是窗口计数评分，不是完整活三/活四分类。中高级采用negamax、alpha-beta和迭代加深，只接纳完整深度；候选截断可能漏解。

后台任务持有Game副本与atomic取消标记。主线程通过future收结果，验证generation、手数、回合和可落子状态；模式、复盘或新局变化不会让旧结果落入新棋局。没有云API、训练或专业棋力评级。

## 保存与复盘

单槽路径由用户HOME动态得到：`~/Library/Application Support/GomokuCourse/game.gmk`。文件保存版本、累计比分、手数、每手坐标/玩家与CHECK；派生棋盘、回合与胜线由同一规则重建。

验证在候选Game上完成，失败保留当前局。写入使用同目录mkstemp独占临时文件，完整write、fsync、close后rename；最后保存者覆盖该槽。FNV-1a只检错，不防恶意篡改；没有父目录fsync的断电级保证。

自动复盘约0.65秒推进一手，至末尾暂停；复盘只读，退出恢复真实局。

## LAN协议

IPv4 TCP，每消息为4字节big-endian长度加payload，单payload最多64KiB，输出队列最多256KiB。非阻塞连接与收发由每帧tick推进，短写偏移与不完整帧保留到下帧。

握手`HELLO → WELCOME → READY → START`，WELCOME带完整校验棋谱。READY之前是可替换临时身份，完成后保护token。主机执黑并裁决，客户端执白只发送MOVE；STATE带完整棋谱、round、seq和重开提议。seq拒绝重复手，round区分重开前后。

双方同意才重开，比分保留。约2秒PING/PONG、10秒无完整帧判失联，每个握手阶段约8秒超时。主机保留棋局与监听，同一客户端对象加入相同endpoint保留内存token；不自动发现、自动重试或跨进程保存身份。重启后的持久恢复是存档建新房，不能在线覆盖已连接比赛。

## 构建与验证

`make app`在`build/v1.3/lor.gomoku.app`构建arm64目标，声明最低macOS26.0，包含固定字体、图片、许可并执行ad-hoc签名。`make test`包含core、UI状态、AI、network及font测试。运行字体测试不创建窗口。

测试数量包含343核心断言、19UI检查、198AI检查、13组本机TCP场景，以及两套字体各379个界面字符。数字不表示同等数量的独立场景或全面验收。原生GUI完整操作、双GUI进程、两设备LAN、其他Mac/Intel、Windows与公证仍有待验证，见[验收清单](ACCEPTANCE.md)。
