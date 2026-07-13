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
| `corpus_normalize` | 语料清洗（`build_dictionary` 前）：把 NNBSP 后缀标准化为 MVS + 谐元 | `tools/corpus_normalize/main.cc` |
| `lexicon_bench` | 性能台架：合成大词库，测量 P50/P90/P99 查询延迟与 DAWG 压缩比 | `tools/lexicon_bench/main.cc` |
| `tune_scoring` | 打分权重评测：对带标注的查询集报告 top-1/top-3/MRR | `tools/tune_scoring/main.cc` |

### build_dictionary — 语料转词库(详解)

从含蒙古文的 UTF-8 文本(新闻、作文等)抽取**单词、附加成分、短语**并计频,产出
本项目 `dictionary.txt` 格式(`latin_key<TAB>蒙古文<TAB>词频`)的词库文件。
源码 `tools/build_dictionary/main.cc`。

#### 编译

```
cmake --build build --config Debug --target build_dictionary
```
产物 `build/bin/Debug/build_dictionary`(Windows 为 `.exe`);`EXCLUDE_FROM_ALL`
开发工具,按需编译。

#### 命令行选项

| 选项 | 含义 | 默认 |
|---|---|---|
| `<input.txt> ...` | 一个或多个 UTF-8 语料文件(位置参数,必填) | — |
| `--out <file>` | **单词(词干)** 输出 | 缺省打到 stdout |
| `--suffix-out <file>` | **附加成分** 输出 | 不输出 |
| `--phrase-out <file>` | **短语 bigram**(联想用)输出 | 不输出 |
| `--phrase-as-entry <file>` | **整串短语词条**(打首词即出整串)输出 | 不输出 |
| `--min-freq N` | 单词/附加成分最低频次 | 1 |
| `--phrase-min N` | 短语最低共现次数 | 2 |
| `--phrase-cond F` | 整串短语的最低条件概率 P(词2\|词1) | 0.2 |

只有给了对应 `--*-out` 才产出该项;不给就跳过。

#### 切词与切分规则

1. **切词**:按 ASCII 空格/数字/标点/拉丁字母切分;蒙古文标点(U+1800–U+1809)也切。
2. **词内连接符**先留在词内(此步不切):NIRUGU(180A)、FVS1–4(180B–180F)、MVS(180E)、
   NNBSP(202F)。
3. **词干 / 附加成分切分**:在 **MVS(U+180E)与 NNBSP(U+202F)** 处把一个词切成
   `[词干, 附加成分, …]`——**首段=词干**(计入单词),其后各段=**附加成分**(单独统计,
   存储**不带前导连接符**)。FVS/NIRUGU 是字形选择符,**不作切分点**,留在所在段内。
4. **latin key**:用 `Romanize`(默认布局逆映射,自动剥离连接符)生成,与引擎查询键一致。
5. **短语**:同一行内相邻**词干**的二元组;整串短语再按条件概率筛。

#### 四种输出

| 输出 | 选项 | 每行格式 | 用途 |
|---|---|---|---|
| 单词(词干) | `--out` | `key<TAB>蒙古文<TAB>频次` | 词库主体 → `data/dictionary.txt` |
| 附加成分 | `--suffix-out` | 同上 | 统计/参考 |
| 短语·联想 | `--phrase-out` | `k1<TAB>m1<TAB>k2<TAB>m2<TAB>权重` | 喂 `compile_lexicon --bigram`,下一词联想 |
| 短语·整串 | `--phrase-as-entry` | `key(首词)<TAB>"词1 词2"<TAB>频次` | 整串候选 → `data/phrases.txt` |

#### 示例

设语料 `news.txt`(以码点示意,`⟨NNBSP⟩`=U+202F 不可见):
```
ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ ᠰᠠᠶᠢᠨ ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ
ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ
ᠤᠯᠤᠰ⟨NNBSP⟩ᠤᠨ ᠤᠯᠤᠰ⟨NNBSP⟩ᠤᠨ        # ᠤᠯᠤᠰ + NNBSP + ᠤᠨ(属格)
```
运行:
```
build_dictionary --out words.txt --suffix-out sfx.txt \
    --phrase-out bigrams.txt --phrase-as-entry phrases.txt news.txt
```

`words.txt`(词干;属格已切走,ᠤᠯᠤᠰ 计 4 次):
```
mqNgql   ᠮᠣᠩᠭᠣᠯ   5
vlvs     ᠤᠯᠤᠰ     4
hele     ᠬᠡᠯᠡ     2
sayin    ᠰᠠᠶᠢᠨ    1
```
`sfx.txt`(附加成分,不带 NNBSP 前缀):
```
vn   ᠤᠨ   2
```
`bigrams.txt`(联想):
```
mqNgql  ᠮᠣᠩᠭᠣᠯ  vlvs  ᠤᠯᠤᠰ  2
mqNgql  ᠮᠣᠩᠭᠣᠯ  hele  ᠬᠡᠯᠡ  2
```
`phrases.txt`(整串,键=首词;`P(词2|词1)=2/5=0.4 ≥ 0.2` 故保留):
```
mqNgql   ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ   2
mqNgql   ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ   2
```
把 `phrases.txt` 放到 `data/phrases.txt` 重编后,打 `mqNgql` 即见:
```
1. ᠮᠣᠩᠭᠣᠯ        [lexicon]   ← 单词在前
2. ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ   [phrase]    ← 整串候选,选中一次上屏两词
3. ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ   [phrase]
```

#### 完整工作流

```
# 1) 语料 → 词库 + 短语（大语料建议提高阈值）
build_dictionary --out data/dictionary.txt --suffix-out sfx.txt \
    --phrase-out data/bigrams.txt --phrase-as-entry data/phrases.txt \
    --min-freq 2 --phrase-min 3 --phrase-cond 0.3  语料*.txt

# 2) 重编 lexicon.bin（data/phrases.txt、data/bigrams.txt 构建期自动编入）
cmake --build build

# 3) 验证：打首词看整串候选，打完一词看下一词联想
./build/bin/Debug/paiza_query.exe <首词的键>
```

#### 调参建议

- `--min-freq`：大语料调高(3–5)过滤噪声/生僻词。
- `--phrase-cond`：调高(0.3–0.5)→ 只留最铁的固定搭配,候选更干净;调低 → 覆盖更多但更杂。
- `--phrase-min`：短语最低共现,大语料可调高。
- 输出是**原始出现次数**;引擎按对数域打分,数值大小无碍。

#### 注意事项

- 输入必须是 **UTF-8**(不是 GBK/GB2312)。
- 整串短语候选默认排在同频单词**之下**(`w_phrase` 惩罚,默认 4),每次查询最多 3 条。
- 附加成分**从词里切出、不带前导连接符**;末词的附加成分由用户输入时补,合成层
  自动插 MVS(见 [core-engine-spec.md](core-engine-spec.md) 的"输入规范化")。

### corpus_normalize — 语料清洗(第 1 步)

在 `build_dictionary` 之前清洗语料。当前实现 **NNBSP→MVS 后缀标准化**:词形
`词干⟨NNBSP⟩后缀` 若其后缀正好是 **D.2 标准 MVS 后缀形**,就把 NNBSP(U+202F)换成
MVS(U+180E)并按词干阴阳性**谐元**;不是标准后缀的 NNBSP(真助词)保留。

```
corpus_normalize --out standard.txt [--fixed data/fixed_sequences.txt] corpus1.txt ...
```
末尾打印报告(处理词数、NNBSP→MVS 转换数)。产物 `standard.txt` 再喂
`build_dictionary`。**后续(第 2 步)**将加入同形疑难清单(`--review`)与人工纠正
应用(`--map`);见 [core-engine-spec.md](core-engine-spec.md) 的"输入规范化"。

#### 其余工具编译示例

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
