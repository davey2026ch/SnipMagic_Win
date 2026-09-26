# 截图复制支持 Foxmail + 「带边框复制到外部软件」开关 — 实现方法

> 2026-09-26 Windows 版 v3.2.2 基础上实现。本文记录原理与做法，供 Mac 版复刻。

## 一、问题原理：Foxmail 为什么粘不进

Windows 剪贴板上可以同时放多种"格式"，每个程序粘贴时挑自己认识的：

| 复制来源 | 剪贴板上的格式 | Foxmail 能贴吗 |
|---|---|---|
| 截图工具（改前） | 位图 CF_DIB + PNG | **不能** |
| Word / 微信 | HTML 格式(CF_HTML) + 位图 + PNG 等 | 能 |

Foxmail 的写邮件窗口是**网页式编辑器**（邮件正文本质是 HTML），插图片只认
"HTML Format"（注册名 `HTML Format`，即 CF_HTML）这种"带图片引用的网页片段"
格式，对裸位图视而不见。Word/微信复制时会额外打包一份 HTML 格式，所以能贴。

**解法：复制时额外多放一份 HTML 格式。** 多放不影响 Word/微信（它们优先读
PNG/位图），只让 Foxmail 这类"只认 HTML"的程序能贴。

## 二、Windows 版实现要点

### 1. HTML 格式里放什么（⚠️ 2026-09-26 下午修订版）

CF_HTML = 一段**头部 + HTML 文本**。头部四个 10 位数字偏移（按 UTF-8 字节算），
指向 HTML 起点、终点、正文片段起点、终点。图片源的选型是这次最关键的教训：

| 图片源 | Word | Foxmail |
|---|---|---|
| `file:///` 临时文件 | **静默跳过 → 粘出空白页**（Word 优先消费 HTML 格式，失败后不回退到位图） | ✓ 能贴 |
| `data:image/png;base64,...` 内嵌 | ✓ 唯一认的方式（超大约 2MB 的图仍会失败） | ✓ 新内核；老 IE 内核超 32KB 不支持 |

最终写法——**data URI 为主 + file:/// 兜底**：

```html
<img src="data:image/png;base64,xxxxx" width="320" height="200"
     onerror="this.onerror=null;this.src='file:///C:/Users/.../SnipMagicClip/clip_xxx.png'">
```

- Word / Chromium 系编辑器：直接吃 data URI（Word 忽略 onerror，无所谓）；
- 老 IE 内核编辑器（Foxmail 旧版）：data URI 超 32KB 报错 → onerror 回退读临时文件。

实现步骤（对应 `src/util.h` 的 `SetClipboardHtmlFormat`）：

1. PNG 只编码一次，同时供 "PNG" 剪贴板格式和 base64 用；
2. base64 内嵌进 `<img src="data:...">`；
3. 同时把 PNG 落盘到 `%TEMP%\SnipMagicClip\clip_<TickCount>_<序号>.png`
   （每次复制一个新文件，避免后一次复制覆盖前一次的兜底引用），
   路径转 `file:///` URI（UTF-8 + 百分号编码，中文路径也能用）写进 onerror；
4. 拼头部 + HTML，整体按 UTF-8 放上剪贴板（注册格式 `HTML Format`）；
5. 清理临时目录里超过 24 小时的旧 PNG。

### 2. 踩过的坑（重要）

- **头部总长不是固定 101 字节。** 四个字段名长度不同（`EndHTML:` 8 字符、
  `StartFragment:` 14 字符……），实际是 105。正确做法：先用 0 占位格式化一遍
  测出真实头长，再回填真实偏移——偏移恒为 10 位数字，回填不会改变头长。
  头长写错，Foxmail/Word 解析偏移全部错位，格式等于白放。
- **Word 只认 base64 内嵌图片**，`file:///` 会被静默丢弃且不回退到剪贴板位图
  ——第一版只放 file:/// 引用，Word 粘出空白页，就是踩了这个。
- 偏移按**字节**算（UTF-8），不是字符数；临时文件路径可能含中文，必须
  先转 UTF-8 再数。
- `std::string::data()` 在 C++14 下返回 const 指针，不能传给
  `WideCharToMultiByte` 的输出参数，用 `&out[0]`。

### 3. 验证方法（可复用）

写了个控制台测试（`测试文件/test_clip_settings.cpp`），断言：

- 剪贴板上同时有 CF_DIB、PNG、HTML Format 三种；
- 头部 Version:0.9，四个偏移与实际数据吻合，fragment 以 `<img src="file:///` 开头；
- 把 HTML 里的 URI 反解出来，临时文件存在且字节与剪贴板 PNG 格式完全一致；
- 设置持久化：ini 写 0/1/删键回落默认，三轮读写一致。

## 三、「带边框复制到外部软件」开关

### 需求落点

- 设置页新增开关（自绘滑动开关，默认**开**）；
- 开：复制（含快捷键）到微信、Word 等外部软件自动加 1px 内边框；
- 关：外部复制不加边框；
- **程序内粘贴（当前画布 / 其他页签）永远不加边框**——与开关无关。

### 做法（核心思想：内外两份，各给各的）

复制时产出**两份位图**，去向不同：

| 份额 | 内容 | 去向 |
|---|---|---|
| external | 按开关决定加不加边框 | 系统剪贴板（CF_DIB/PNG/HTML） |
| internal | 永远原图无边框 | 应用内粘贴缓冲（本程序专用） |

改前系统剪贴板和应用内缓冲共用同一处理，改后彻底分开。对应 `src/document.cpp`
的 `CopySelectionToClipboard`：先裁出原图 → internal 存应用内缓冲 → external 按开关
加边框后放系统剪贴板。

细节与坑：

- 开关值存 `AppSettings::borderCopyToExternal`，ini 键名 `BorderCopyToExternal`，
  默认 1；读取用 `GetPrivateProfileInt`，**键缺失自动回落默认开**。
- **浮动图片图层（透明底贴纸）特例**：先检测位图是否含透明像素
  （`BitmapHasAlpha`），含透明就跳过边框——不然会在透明图边缘画出一条悬空的
  边框线，还破坏了"复制保留透明底"的既有特性。完全不透明的浮动图正常按开关加边框。
- 设置窗口高度从 520 加到 562 容纳新行；开关是 BS_OWNERDRAW 自绘按钮
  （蓝底白圆块=开，灰底白圆块=关），点击翻转草稿值、重绘，点"确定"才落盘。

## 四、Mac 版实现指引

Mac 的剪贴板（NSPasteboard）思路相同——**多放几种类型，各程序各取所需**：

1. **基础图片类型**：`public.png`（对应 Windows 的 PNG 格式）和
   `public.tiff`（Mac 图形环境更通用，建议两个都放）。
2. **HTML 类型**（对应 CF_HTML）：`NSPasteboardTypeHTML`，内容同样是
   `<img src="...">` 片段。Mac 上有两点比 Windows 省心：
   - `data:image/png;base64,...` 内嵌数据 URI 在 WebKit 系编辑器里没有
     Windows IE 那个 32KB 限制，小图可以直接内嵌，免落盘；
   - 若图较大或目标编辑器（如 Foxmail Mac）不支持 data URI，退回 Windows 同款
     方案：PNG 落盘到 `NSTemporaryDirectory()/SnipMagicClip/`，HTML 引用
     `file://` URL，并做同样的"旧文件清理"。
3. **边框开关**：macOS 用 `UserDefaults` 存 `borderCopyToExternal`（默认 true）；
   复制时同样产出两份——`NSPasteboard` 放带边框版，应用内粘贴缓冲保持原图。
   透明图检测：遍历像素 alpha（或用 Core Image 判断），含透明跳过边框。
4. **验证**：Mac 上可以用 `Pasteboard Viewer` 或
   `osascript -e 'clipboard info'` 看放上去的类型清单；粘贴实测 Foxmail /
   Mail / Pages。

## 五、改动清单（Windows 版）

| 文件 | 改动 |
|---|---|
| `src/util.h` | 新增 `SetClipboardHtmlFormat`/`WideToUtf8`/`CleanupClipboardTempFiles`/`BitmapHasAlpha`；`BitmapToClipboard` 追加 HTML Format |
| `src/document.cpp` | 复制分内外两份；边框按开关；透明图跳过边框 |
| `src/settings.h/.cpp` | 新增 `borderCopyToExternal`（默认 true）+ ini 读写 |
| `src/settingsdlg.cpp` | 设置页新增开关行（自绘滑动开关），窗口加高 |

验证：编译通过；剪贴板/设置单元测试全 PASS；真机启动后设置对话框截图核对
（开关渲染、开↔关切换、确定后 ini 落值均正常）。
