# SwiftFind

一个 Listary 式的极简启动器：**双击 Ctrl** 唤出，搜应用和文件，回车打开。

单个 exe，无运行时依赖，不写注册表（除非你主动开启开机自启）。

![SwiftFind 搜索框](docs/screenshot.png)

## 体积与性能（本机实测）

| 项目 | 数值 |
|---|---|
| exe 体积 | **216 KB**（221,184 字节） |
| 私有内存 | 42.8 MB（含 49 万条全盘索引） |
| 首次建索引 | 4.4 秒（全盘 492,298 个对象） |
| 索引文件 | 30.8 MB（`%LOCALAPPDATA%\SwiftFind\index.bin`） |
| 单次搜索 | 4~10 ms |
| 启动 | 瞬时（索引直接 mmap 读入） |

## 体积是怎么压下来的

从 308,736 字节压到 219,136，减掉 29%。每一步都是实测，不是估算：

| 改动 | 省下 | 说明 |
|---|---|---|
| `-Os` 取代 `-O2` | 25,600 | 这个项目对速度不敏感，对体积敏感 |
| `-ffunction-sections` + `--gc-sections` | 4,096 | 丢掉没被引用的 libstdc++ 成员 |
| 手写数字格式化，去掉 `swprintf` | 12,288 | 只用两处格式化，却拖进整个 `__pformat` + `__gdtoa`（浮点转字符串就 7 KB） |
| 图标全部改 PNG 压缩 | 19,968 | 小尺寸原本是未压缩 32bpp BMP，一个 48px 就要 9.6 KB |
| `CreateThread` 取代 `std::thread` | 27,648 | `libpthread` 被整套拖进来（thread/cond/rwlock/mutex/sched），而实际只有三处「起线程-等待」 |
| `SRWLOCK` 取代 `std::mutex` | — | 体积不变，但零初始化、更快，也不用再链 pthread |

剩下的 214 KB 里，**自己的代码只有 41,952 字节**，其余是静态链接的 libstdc++/CRT —— 用 `std::string` / `std::vector` / `std::shared_ptr` 就得付这份钱。再往下压只有两条路，都没走：

- `-fno-exceptions`（约 6 KB）：会让我们自己的栈帧失去 `.pdata` 展开信息，内存分配失败时从「干净退出」变成「崩溃」，还可能干扰 libstdc++ 的异常传播。不划算。
- UPX 之类加壳（估计能到 90 KB 左右）：启动器常驻开机自启，加壳容易触发杀软误报。要的话可以单独出一版。

## 用法

| 操作 | 说明 |
|---|---|
| 双击 `Ctrl` | 唤出搜索框（**只有输入框，高 64px**） |
| 输入 | 开始输入才向下展开结果区，实时模糊搜索，应用排在最前 |
| `↑` `↓` | 选择结果 |
| `Enter` | 打开（文件用默认程序，文件夹用资源管理器） |
| `Esc` | 收起 |
| 框内右键 | 应用菜单：设置… / 重建索引 / 退出 |
| `Ctrl` + `,` | 直接打开设置面板 |

清空输入框会自动收回成只剩搜索框的状态。

## 设置面板

有两个入口，**都不依赖托盘图标**：

- 搜索框里按 `Ctrl` + `,`
- 搜索框里右键 → 「设置…」

托盘图标右键是第三个入口（如果托盘图标没隐藏）。

| 项目 | 说明 |
|---|---|
| 开机自动启动 | 勾选后写入 `HKCU\...\Run`，登录时自动驻留托盘 |
| 失焦自动隐藏 | 默认开启。关闭后窗口不会因为点到别处而收起，需要手动 `Esc` |
| 跟随鼠标位置打开 | 默认关闭。开启后窗口出现在鼠标指针处；关闭时窗口在屏幕正中 |
| 显示托盘图标 | 默认开启。关掉后托盘图标移除，设置改走上面两个入口；此时该行下方会直接标出入口位置，避免找不到菜单 |
| 主题 | 浅色 / 深色 / 跟随系统，三选一，切换即时生效 |
| 索引统计 | 当前对象数与索引文件体积 |
| 重建索引 | 后台重建，按钮期间显示「索引中…」 |
| 打开索引目录 | 在资源管理器中打开 `%LOCALAPPDATA%\SwiftFind` |

面板可拖动（拖标题区域），`Esc` 或右上角 `×` 关闭。设置项存在 `HKCU\Software\SwiftFind`。

### 圆角与开关的抗锯齿

GDI 的 `RoundRect` / `Ellipse` **不做抗锯齿**，24px 高的开关在圆角处有明显台阶。所以开关和主题分段控件改用
GDI+ 路径填充（`SmoothingModeAntiAlias`）绘制，文字仍然走 GDI —— 两者可以安全混用，各自在同一个 HDC 上按序绘制。

边框也是抗锯齿的：先填一个外圈圆角矩形（描边色），再内缩 1px 填内容色。用 GDI 画笔描边会退化成硬边。

代价是 `gdiplus.dll` 这个导入（系统自带，不增加分发文件）和约 2 KB 体积。

### 主题

所有颜色都从一份调色板取，浅色/深色各一套，绘制代码不分叉。

- **跟随系统**（默认）读 `HKCU\...\Themes\Personalize\AppsUseLightTheme`，并在收到 `WM_SETTINGCHANGE` / `ImmersiveColorSet` 时实时切换，不用重启
- 深色下搜索框、结果列表、选中行、页脚、设置面板、滚动条全部跟随；滚动条通过 `SetWindowTheme(..., L"DarkMode_Explorer")` 变暗
- 输入框占位提示由系统按文字色自动淡化，深色下仍然可见

窗口位置规则：默认居中于当前显示器的**工作区**（扣掉任务栏后的区域），水平垂直都居中；开启「跟随鼠标位置打开」后出现在光标右下方，并始终夹取在工作区内 —— 即使光标在屏幕最底部展开结果列表也不会溢出。

托盘菜单和搜索框右键菜单是**同一个构造函数**，只是托盘那版多一项「打开搜索框」。这样隐藏托盘图标时不会把「设置 / 重建索引 / 退出」一起弄丢。

原生弹出菜单由系统绘制，不吃自定义调色板。深色下靠 `uxtheme.dll` 的 `SetPreferredAppMode`（序号 135/136/104，按版本依次尝试）切到强制深色，失败就静默退回系统默认。

## 命令行

```bash
SwiftFind.exe                # 静默启动，只留托盘
SwiftFind.exe --show         # 启动并立即显示窗口
SwiftFind.exe --query node   # 启动并预填查询（自动展开）
SwiftFind.exe --reindex      # 重建索引并输出统计
SwiftFind.exe --search node  # 纯命令行搜索（调试用，输出路径与耗时）
SwiftFind.exe --settings     # 直接打开设置面板（调试用）
SwiftFind.exe --nohide       # 调试：失焦不自动隐藏（方便截图）
```

窗口是两态的：查询为空时高 64px 只显示输入框；一旦有输入就展开到 520px 显示结果列表和底部统计。

## 索引范围

- 所有**固定磁盘**（`DRIVE_FIXED`）的完整目录树，跳过 junction/符号链接防止死循环
- 开始菜单全部 `.lnk`（`%ProgramData%` 和 `%APPDATA%` 两处）
- 排除 `$Recycle.Bin`、`System Volume Information`

索引存储为紧凑结构：目录去重存一份，文件名单独存，每条记录 12 字节。

## 重新编译

需要便携版 MinGW-w64（w64devkit）：

```bash
./build.sh
# 或指定工具链位置
W64DEVKIT=/c/tools/w64devkit ./build.sh
```

只依赖 Windows 自带的 DLL，静态链接 libstdc++/libgcc，所以 exe 拷到任何 Windows 10/11 上都能直接跑。

## 图标

主图形是「镜片 + 闪电」：镜片对应搜索，闪电对应 Swift（快），闪电从镜片里**镂空**出来，
所以整枚图标只有两个颜色（品牌蓝 + 白），不靠第二种颜色制造细节。

小尺寸单独出了简化字形——16/20/24px 去掉闪电只留实心镜片。因为 16px 下闪电会缩成一个
杂散的点，镜片看起来像有个缺口（读成「钥匙孔」）；去掉后反而干净可辨。这是图标设计的
常规做法：小尺寸换简化字形，而不是硬缩大图。

`app.ico` 内含 9 个尺寸（256 到 16），**全部用 PNG 压缩**，合计 17 KB。Vista 以后各尺寸都支持
PNG 图标，而这个程序本来就要求 Win10，所以没留未压缩 BMP 兜底——那会让图标从 17 KB 涨到 37 KB。

```
python tools/make_app_icon.py     # 从 icon_studio 的 v3 重新生成 src/app.ico
python tools/icon_final.py        # 定稿预览图 icon-final.png
python tools/icon_studio.py       # 六个备选方案 + 对照表 icon-sheet.png
```

## 源码结构

```
src/main.cpp         全部逻辑（约 1000 行）
src/res.rc           图标 + 版本信息 + manifest
src/app.manifest     comctl32 v6 + Per-Monitor DPI
tools/icon_studio.py 图标方案生成器（SDF 渲染，纯标准库）
tools/make_app_icon.py  生成多尺寸 app.ico
tools/iconcheck.cpp  校验 exe 内图标资源能否按各尺寸加载
tools/wins.cpp       枚举顶层窗口（排查窗口/可见性）
tools/zoom.py        放大截图局部（看 16px 托盘图标）
tools/shot.cpp       截图工具（验证 UI 用）
tools/keys.cpp       发送双击 Ctrl / 输入文字（验证交互用）
```

## 已知限制

- 索引是**静态快照**，新建文件后需要托盘菜单「重建索引」（或重启时按需重建）
- 搜索只匹配**文件名**，不匹配完整路径
- 不做同目标 `.lnk` 去重（同一个应用的多个快捷方式会分别出现）
- 仅支持固定磁盘，移动硬盘/U 盘不索引
