<div align="center">
  <img src="assets/logo.png" alt="One-Key Input" width="360" />

  # One-Key Input

  **一键语音输入 · AI 润色 · 上下文感知更准确**

  [English](README.en.md) · [快速开始](#快速开始) · [下载](https://github.com/zwcih/one-key-input/releases) · [Issues](https://github.com/zwcih/one-key-input/issues) · [Discussions](https://github.com/zwcih/one-key-input/discussions)

  <sub>按住一个键 · 说话 · 松开 · 润色后的文字落到光标处</sub>
</div>

---

## 这是什么

按住一个键（默认 **F9**）说话，松开后：

1. 语音被实时识别
2. 大模型按你选的风格润色（去口水词 / 改正式 / 保留原样）
3. 处理后的文字直接注入到当前焦点（聊天框、编辑器、地址栏，**几乎任何应用**）

不打开 App，不切窗口，不复制粘贴。你的输入流不被打断。

> 截图位待补：`assets/demo-placeholder.png`

## 为什么再做一个

主流语音输入要么：

- **不带润色** —— 出来一堆"嗯"、"那个"、"就是"，要么自己删要么忍着
- **带润色但完全没有上下文** —— 在写代码时把"括号"翻成文字而不是 `()`；在写邮件时却太口语；在 BBS 里回帖却用论文腔
- **要切窗口 / 走云端剪贴板** —— 流程笨重

One-Key Input 想做到的是：

- **按住即说，松开就到** —— 一个键完成全流程，零额外操作
- **上下文感知** —— 这是核心差异点（见下）
- **后端可换** —— ASR 和 LLM 都是接口化的：现在用 Azure Speech + Azure OpenAI；本地 Whisper + 本地 LLM 正在路上
- **跨平台目标** —— 当前是 Windows 原生，架构层抽象做了，Linux/macOS 后续

## 上下文感知 —— 这是什么意思

绝大多数语音输入只看"你说了什么"。One-Key Input 在润色之前会先看**你此刻在看什么、要回复什么**，让结果真的贴合场景：

- 在 **YouTube / Bilibili** 看视频，要写评论：视频标题 + 简介 + 当前播放的字幕都进上下文 —— 你说"挺有意思的"，润色后是切题的一句评论，而不是泛泛"This is interesting"
- 在 **微信 / Slack / Teams** 聊天框：取当前会话最近几条消息 —— "好的我看看" 会被润成回复对方刚问的那个问题的口气，而不是孤立的回复
- 在 **代码编辑器**：你光标附近的代码 + 文件类型 —— 说"加个错误处理"，润色后用的是这门语言的惯用法，符号自动正确
- 在 **邮件草稿**：取主题 + 上一封内容 —— 自动匹配收件人的称呼和正式程度
- 在 **论坛 / Reddit / 知乎**：取帖子标题 + 你正在回复的那条 —— 回复贴题，不是说什么是什么

你说的还是同一句话。改变的是大模型在润色时**知道你说话的语境**，所以输出更像"懂你的合作者写的"，而不是"一个不知道发生了什么的助手随手改写的"。

> **它怎么做到的**：按下热键的瞬间，Core 会用 Windows 的 UIAutomation 接口对前台窗口做一次有限时（800ms 预算）的快照——包括应用名、窗口标题、当前焦点控件类型、光标前后 ~200/50 字符、选中文本，以及同窗口内其它可见文本区域（聊天历史 / 文档正文等）。这些会被压成一个简短上下文块附加到 LLM 的 system prompt 里，原话不变。录音和上下文抓取并行进行，不增加总延迟。
>
> 不想要的话，`config.json` 里把 `polish.use_context` 改成 `false` 即可完全关闭（连 UIA 调用都不会发起）。
>
> **当前局限**：UIA 对原生 Win32 / WinUI / WPF 应用读取最佳；对 Electron / 浏览器内嵌内容的读取程度取决于该应用是否暴露完整 a11y 树。针对浏览器和主流 IM 的专用适配器在路线图上。

## 快速开始

### 1. 下载

从 [Releases](https://github.com/zwcih/one-key-input/releases) 下载 `OneKeyInput-x.y.z-portable.zip`，解压到任意目录。

> 文件夹大小 ~25 MB，便携，无需安装。

> ⚠️ **首次运行：Windows 会拦一下**  
> 因为 exe 没有代码签名证书（开源软件买不起也没必要），Windows SmartScreen 会弹一个蓝色警告框 "Windows protected your PC"。点 **More info（更多信息）→ Run anyway（仍要运行）** 即可。Defender 不会真把它当病毒——只是因为这个文件下载量还不够多、没建立信誉。  
> 想一劳永逸消除提示：解压前右键 zip → **属性 → 勾选 "Unblock（取消阻止）" → 确定**，再解压出来的 exe 不会带"网络下载"标记。
>
> 介意的话，欢迎自己 [从源码编译](#开发) —— 自己编出来的 exe 不会被拦。

### 2. 第一次运行

双击 `onekey-core.exe`：
- 没有 `config.json` 时会自动弹出设置窗口
- 填入你的 Azure Speech key + region，以及润色 LLM 的 key / endpoint
- 点 **"保存并启动"** —— 设置会先用真实接口验证凭据，通过后才写入 + 启动 Core

### 3. 使用

按住 **F9** → 说话 → 松开 → 文字落在焦点处。

托盘图标右键有：
- **设置 / Settings** —— 重新打开配置
- **Pause / Resume** —— 暂停 / 恢复热键
- **Polish mode** —— Raw（不润色） / Tidy（默认） / Formal（正式改写）
- **Open Log Folder** —— 出问题时看日志

### 4. 配置文件

直接编辑 `config.json` 也可以 —— Core 自带文件监视，保存后自动重启加载新配置。但 GUI 保存时的凭据预验证会被绕过，手改请自己确认 key 没填错。

## 翻译模式（F8）

除了「按住 F9 = 录音 + 润色」，还提供「按住 F8 = 录音 + 翻译」。

按住 **F8** 用母语口述 → 松开 → 焦点处出现目标语言的版本。**复用同一套 ASR、上下文抓取和注入管线**，只是在调 LLM 时换了一个结构化的翻译 prompt。

- **没有"翻译方向"概念**：配置里只指定**目标语言**。ASR 识别出的语言 ≠ 目标语言时才翻译。
  - 目标语言 = 英文 → 说中文翻英文；说英文不翻
  - 目标语言 = 中文 → 说英文翻中文；说中文不翻
- **润色风格沿用**：当前选的 Raw / Tidy / Formal 同样作用在翻译输出上。Raw 模式的翻译尽量贴原意不加戏；Formal 用正式书面语。翻译没有独立的风格档位。
- **上下文同样起作用**：从焦点窗口抓到的代码符号、产品名、人名等会作为 `[KEEP VERBATIM]` 注入，翻译时保持原样不译；对方称呼的松散度也会用来匹配回复语气。
- **智能目标语言（实验性，默认关）**：`translate.smart_target = true` 时会根据焦点窗口内容自动判断目标语言。**可能不准；要求稳定时请关闭并固定 `target_language`。**

`config.json` 里相应的配置块：

```jsonc
{
  "translate": {
    "enabled": true,
    "hotkey": "f8",
    "min_hold_ms": 250,
    "target_language": "en",   // en/zh/ja/ko/de/fr/es/it/pt/ru ...
    "smart_target": false
  }
}
```

或者直接在设置 UI 的「翻译模式」一节里改。

## 支持的后端

| 类型 | 提供商 | 状态 |
|---|---|---|
| ASR (流式) | Azure Speech | ✅ 默认 |
| ASR (REST) | Azure Speech | ✅ |
| ASR (本地) | Whisper.cpp | 🚧 接口已留 |
| 润色 | Azure OpenAI | ✅ 默认 |
| 润色 | OpenAI | ✅ |
| 润色 | 本地 LLM | 🚧 接口已留 |

## 你需要准备的

- **Windows 10/11 (x64)**
- **Azure Speech 资源** —— 免费档每月 5 小时识别足够日常用
- **一个 LLM 接入**：
  - Azure OpenAI：要个 deployment，推荐 `gpt-4o-mini`（便宜快）
  - 或 OpenAI API key

> 完全自费跑，月度成本通常 < 10 元人民币（除非你天天讲很多）。

## 工作机制

```
F9 按下  → WASAPI 抓麦克风 (16k mono)
         → Azure Speech 流式识别（边讲边出）
F9 松开  → 等最终识别结果
         → LLM 按你选的模式润色（流式输出）
         → SendInput / 剪贴板注入到焦点窗口
         → 错误音 / 提示音 / 托盘 tooltip 反馈状态
```

详细架构文档稍后整理。

## 隐私

- **本地处理**：所有音频和文字都在你的机器上处理后**直接**发到你自己的 Azure / OpenAI 账号。我们没有服务器，没有遥测，没有埋点
- **你自己的 key**：所有云端凭据由你提供，存在 `config.json`（仅本地，gitignored）
- **日志**：本地 `logs/` 目录，明文，可随时删

## 开发

```bash
# 前置：Visual Studio 2022 Build Tools + vcpkg + Node 20+
git clone https://github.com/zwcih/one-key-input.git
cd one-key-input

# Build core
scripts\build.bat

# Build settings UI
cd settings && npm install && npm run tauri build
```

## 反馈

- 🐛 [开 Issue 报 bug](https://github.com/zwcih/one-key-input/issues/new/choose)
- 💡 [Discussions](https://github.com/zwcih/one-key-input/discussions) 聊想法 / 提问
- ⭐ 用得顺手的话给个 star，我能看到

## License

[MIT](LICENSE) © 2026 zwcih
