# 双方要弈棋 · lor.gomoru

约朋友摆一盘，或者让离线AI认真堵你的四。一个用C++17和raylib做的Mac五子棋小项目，黑先，五子及以上就赢。

- **面对面**：本地双人，非法落子拒绝、胜线与累计比分。
- **挑战AI**：初/中/高三档，100/400/1200ms思考预算。棋形评分、紧急赢挡，中高级采用negamax、alpha-beta与迭代加深；不用云端API。
- **下次继续**：保存棋谱、校验读档，逐手或自动复盘。
- **隔桌对弈**：LAN TCP建房/加入，主机执黑、客方执白，状态同步、双方同意重开、同实例断线重连。
- **看着舒服**：中文、浅色棋盘、落点预览、最后一步红圈和圆角logo。

## 在Mac上跑起来

目标为**Apple Silicon（arm64）、macOS 26.0及以上**；实际构建环境为macOS 26.6.2。需要Apple Command Line Tools中的Clang和Make，固定raylib源码与运行资源已包含。

```sh
git clone https://github.com/ZwkZwkZwkZwk/lor.gomoru.git
cd lor.gomoru
make app
open build/v1.3/lor.gomoku.app
```

仓库叫`lor.gomoru`，应用仍叫`lor.gomoku`。此仓库提供源码，暂未发布二进制Release。

## 连接另一位棋友

主机在“局域网”页创建房间，客方填写主机IPv4和相同端口加入，默认34567。本机两实例可用`127.0.0.1`；两设备需处于可互通LAN并允许网络访问。同一客户端实例断开后点“加入 / 重连”；退出后续盘由主机加载存档建新房，客端选“加入新房”。

## 测试与一点诚实

```sh
make test
```

规则/存档、UI状态、AI、真实localhost TCP和中文字体CPU测试均有自动检查。原生GUI完整操作、双GUI实例和两设备Wi-Fi/LAN尚未形成完整验收证据；Intel、更旧macOS和Windows未验证。AI不保证最优，协议面向可信LAN，无TLS；ad-hoc签名不等于Apple公证。

想看设计、操作与依赖来源，可读[技术说明](docs/TECHNICAL.md)、[手动验收清单](docs/ACCEPTANCE.md)和[第三方说明](THIRD_PARTY.md)。开发与回归测试使用AI辅助。第三方许可保留；项目自身尚未选择开源许可证。
