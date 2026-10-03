# 第三方组件与资源

本文件说明依赖来源与许可，不为本项目的第一方代码授予新的开源许可。

| 项目 | 本仓库使用方式 | 来源与许可 |
|---|---|---|
| raylib 5.5 | 项目内源码静态构建；含其必要内部依赖源码 | [官方5.5版本](https://github.com/raysan5/raylib/releases/tag/5.5)，[raylib LICENSE](vendor/raylib-5.5/LICENSE)及`src/external/`内原有许可说明 |
| Noto Sans SC | 已生成500/600固定字重TTF，正常构建不需字体制作工具 | [Google Fonts项目](https://github.com/google/fonts/tree/main/ofl/notosanssc)，[SIL OFL文本](assets/OFL.txt) |
| 窗口logo / AppIcon | 项目所有者提供；等比适配，界面圆角在运行时内存生成 | 此处未单独授予重用许可 |

raylib下载归档的记录SHA256为`aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e`。仓库包含构建所需源码，不包含该下载归档或预编译静态库。

字体固定实例制作使用FontTools 4.53.1，工具不随运行包或正常构建使用。原变量字体没有纳入此公开副本；两套运行字体与OFL已经包含。

macOS Cocoa、IOKit、OpenGL和CoreVideo来自系统SDK。Git、Clang、Make、Python3、sips、iconutil与codesign用于源码构建、资源和包装；构建脚本不会安装系统软件或修改防火墙。
