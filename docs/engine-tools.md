# 引擎命令行工具 (Engine Command-Line Tools)

一组不依赖任何 IME 宿主 / GUI 的命令行工具，用于对核心引擎做纯算法测试、
性能验收与打分调参。本文重点介绍 **`paiza_query`**（键盘序列 → 候选词列表），
末尾简述其余三个工具。

所有工具都链接 `paiza_core`，是构建期/开发期工具（`EXCLUDE_FROM_ALL`，
不随默认构建产出）。产物位于 `build/bin/<Config>/`。

---

## paiza_query — 无界面候选词查询

输入一段拉丁键序，返回引擎给出的候选词列表——没有输入法框架、没有窗口，
纯引擎。它走的是三端前端相同的管线（键位映射 → `CandidateEngine`
覆盖「用户词库 + 静态词库」，经由 `InputSession`），因此能真实复现
精确/前缀匹配、容错匹配、用户调频与 bigram 上下文等全部行为。

源码：`tools/paiza_query/main.cc`。

### 编译

```
cmake --build build --config Debug --target paiza_query
```

产物：`build/bin/Debug/paiza_query`（Windows 为 `paiza_query.exe`）。

> 提示：`lexicon.bin` 与 `dictionary.txt` 会在构建时部署到 `build/bin/<Config>/`
> （紧挨着 `PaizaService`）。在该目录下直接运行，默认路径即可命中。

### 命令行选项

| 选项 | 含义 | 默认值 |
|---|---|---|
| `--lexicon <bin>` | 编译后的静态词库 `lexicon.bin` | 当前目录 `lexicon.bin`（缺失则仅用文本词库） |
| `--dict <txt>` | 文本词库 `dictionary.txt`（回退层） | 当前目录 `dictionary.txt` |
| `--keymap <txt>` | 键位映射文件 `keymap.txt` | 内置默认映射 |
| `--user <db>` | 用户词库 SQLite 文件 | 空（内存态，退出即弃，测试可复现） |
| `<seq> ...` | 直接给出一个或多个待查键序 | 无（则进入交互模式） |

### 两种运行模式

**一次性（可脚本化）**：把键序作为参数传入，逐条打印结果后退出。

**交互式**：不带任何键序参数运行，从标准输入逐行读取键序。REPL 命令：

| 命令 | 作用 |
|---|---|
| `:N` | 上屏上一次结果中的第 N 个候选：记入用户词库（调频/记忆），并把它设为下一次查询的 bigram 上下文 |
| `:reset` | 清除 bigram 上下文 |
| `:q` / `:quit` | 退出 |

### 输出格式

每条键序输出：
- `preedit:` 行——键位映射对该键序的**直译**（Mongolian 预编辑串），
  括号内是 `U+XXXX` 码点；
- 若干候选行——序号、蒙古文（UTF-8）、`[来源]`、`score=` 综合打分、
  以及 `U+XXXX` 码点串。

来源标签：`[user]` 用户词库（优先级 200）、`[lexicon]` 静态词库（100）、
`[predict]` bigram 联想。分数为对数域打分器的缩放整数，越大越靠前；
容错命中会被编辑代价压成较低甚至负分，因此精确匹配恒排在错拼之前。

### 示例

**1) 精确匹配**——`sain` 返回两种拼写，按词频排序：

```
$ ./paiza_query.exe sain
> sain
  preedit: ᠰᠠᠢᠨ  (U+1830 U+1820 U+1822 U+1828)
  1. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  U+1830 U+1820 U+1836 U+1822 U+1828
  2. ᠰᠠᠢᠨ  [lexicon]  score=3714  U+1830 U+1820 U+1822 U+1828
```

**2) 容错匹配**——`mongl`（漏打一个 `o`）、`bicig`（漏打 `h`）仍能命中，
但分数被编辑代价压低（可能为负），排在任何精确匹配之后：

```
$ ./paiza_query.exe mongl bicig
> mongl
  preedit: ᠮᠥᠩᠯ  (U+182E U+1825 U+1829 U+182F)
  1. ᠮᠣᠩᠭᠣᠯ  [lexicon]  score=-1385  U+182E U+1823 U+1829 U+182D U+1823 U+182F
  2. ᠮᠣᠩᠭᠣᠯᠴᠤᠳ  [lexicon]  score=-2566  ...
> bicig
  preedit: ᠪᠢᠴᠢᠭ  (U+182A U+1822 U+1834 U+1822 U+182D)
  1. ᠪᠢᠴᠢᠭ  [lexicon]  score=-1489  U+182A U+1822 U+1834 U+1822 U+182D
```

**3) 用户词库学习**——交互模式下上屏一个候选，再次查询即见它以
`[user]` 身份排到最前（优先级高于静态词库）：

```
$ printf 'sain\n:2\nsain\n:q\n' | ./paiza_query.exe
> sain
  1. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  ...
  2. ᠰᠠᠢᠨ  [lexicon]  score=3714  ...
  [committed ᠰᠠᠢᠨ; context set for next query]
> sain
  1. ᠰᠠᠢᠨ  [user]     score=1     U+1830 U+1820 U+1822 U+1828
  2. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  ...
```

> bigram 上下文同理由 `:N` 触发：`SetContextWord` 会令下一次查询中该词的
> 后继候选获得增益（前提是 `lexicon.bin` 编入了 bigram 数据；示例词库暂无）。

### Windows 编码与字体说明

- 工具启动时调用 `SetConsoleOutputCP(CP_UTF8)`，直接在控制台运行不会乱码。
- **乱码 ≠ 缺字形**：若显示成 `□` 方框（豆腐块），是当前控制台**字体不含
  蒙古文字形**，与编码无关——换成含蒙古文的字体（如 Noto Sans Mongolian）即可。
- 无论字体如何，行尾的 **`U+XXXX` 码点始终可读**，是纯算法验证最可靠的依据。
- 传统蒙古文是**竖排**，控制台横排显示，方向"不像"真正的蒙古文属正常现象，
  码点顺序才是判据。
- 在 PowerShell 中**捕获/重定向**输出（`> 文件` 或赋值给变量）时，PowerShell
  会用自身的 `[Console]::OutputEncoding` 再解码一次，可能引入 UTF-16 等问题。
  要看原始字节，用 `cmd` 重定向或 Git Bash 管道最干净；直接在终端交互查看则正常。

### 工作原理

`paiza_query` 按与真实前端一致的顺序装配引擎：

```
CandidateEngine
 ├─ UserDictionary   优先级 200   （--user，缺省内存态）
 ├─ StaticLexicon    优先级 100   （--lexicon，mmap + DAWG×自动机容错）
 └─ LocalDictionary  回退         （--dict，文本词库）
```

键序经 `KeyMappingStore`（`--keymap` 或内置默认）转成预编辑串，再由
`InputSession`（跨平台键策略契约，见 `src/core/input_session.h`）驱动
`CandidateEngine::Query`。因此命令行看到的结果，就是真实 IME 对同样按键
会给出的候选。

---

## 相关工具

| 工具 | 用途 | 源码 |
|---|---|---|
| `compile_lexicon` | 离线编译器：`dictionary.txt`(+`bigrams.txt`) → `lexicon.bin`（构建期自动运行） | `tools/compile_lexicon/main.cc` |
| `build_dictionary` | 语料 → `dictionary.txt`：从蒙古文 txt 抽取单词+短语并计频 | `tools/build_dictionary/main.cc` |
| `lexicon_bench` | 性能台架：合成大词库，测量 P50/P90/P99 查询延迟与 DAWG 压缩比 | `tools/lexicon_bench/main.cc` |
| `tune_scoring` | 打分权重评测：对带标注的查询集报告 top-1/top-3/MRR | `tools/tune_scoring/main.cc` |

### build_dictionary — 语料转词库

把含蒙古文的 UTF-8 txt（新闻、作文等）转成本项目 `dictionary.txt` 格式
（`latin_key<TAB>蒙古文<TAB>词频`),抽取单词与短语并计频。

```
cmake --build build --config Debug --target build_dictionary
./build/bin/Debug/build_dictionary --out words.txt --suffix-out sfx.txt \
    --phrase-out phrases.txt [--min-freq N] [--phrase-min N] a.txt b.txt ...
```

三份**独立输出**(均为 `latin_key<TAB>蒙古文<TAB>词频`,短语除外):

- **词干**(`--out`,缺省 stdout):在 **MVS(U+180E)/NNBSP(U+202F) 处切开词干与附加
  成分**——词干计入单词。FVS/NIRUGU 是字形选择符,留在词内不切。
- **附加成分**(`--suffix-out`):词干之后的每段**单独统计**,存储时**不带前导连接符**。
- **短语**(`--phrase-out`):相邻词干二元组,频次 ≥ `--phrase-min`(默认 2),以
  **bigram 格式**输出(`latin1<TAB>m1<TAB>latin2<TAB>m2<TAB>权重`),可直接喂
  `compile_lexicon --bigram`,驱动引擎的**下一词联想 / 上下文重排**(见 Phase 2/3
  的 `SetContextWord`/`PredictNext`)。

- **latin key** 用项目的 `Romanize`(默认布局逆映射,剥离连接符)生成,与引擎查询键一致。
- **切词**:按 ASCII 空格/数字/标点切分,词内连接符暂不切;`--min-freq`(默认 1)过滤低频。

词干产物可作为 `data/dictionary.txt`;短语 bigram 喂 `compile_lexicon --bigram`
即启用联想。

编译示例：

```
cmake --build build --config Release --target lexicon_bench
./build/bin/Release/lexicon_bench 500000 30000      # 键数 查询数

cmake --build build --config Release --target tune_scoring
./tune_scoring --lexicon lexicon.bin --queries labelled.tsv --scoring scoring.txt
```

`compile_lexicon` 单独调用：

```
./compile_lexicon --out lexicon.bin --dict dictionary.txt [--bigram bigrams.txt]
```

引擎架构与分期实现详见 [core-engine-spec.md](core-engine-spec.md)。
