# 跨平台蒙古文智能输入法核心引擎（Core Engine）架构与实现规格说明书

版本：v1.1（工程化修订版）
状态：Phase 1 已实现（见 §7 分期路线）；Phase 2/3 为设计定稿待实现。

---

## 1. 系统概述 (System Overview)

本项目旨在开发一个支持百万级词库、毫秒级响应、高度容错并具备上下文预测
能力的跨平台蒙古文输入法核心引擎。

* **开发语言**：纯 C++（**C++20**，与本仓库既有代码一致），核心逻辑零平台
  依赖；对外暴露 **C-style FFI 接口**，供上层跨平台 UI 宿主（C# Avalonia、
  C++ Qt 等）调用。本仓库内置的三个原生前端（Windows TSF / macOS IMK /
  Linux IBus）**继续通过 `paiza::InputSession` 直接调用 C++ 层**，FFI 是
  给外部宿主的封装，不是内部前端的必经之路。
* **核心架构**：SQLite（离线数据管理与持久化）＋ 预编译双数组字典树
  DAT/FST（静态底库检索）＋ Levenshtein DFA（容错纠正）＋ CSR 稀疏矩阵
  （N-gram 联想）＋ 内存有序映射（动态用户层）——**双轨重载（Overlay）**。
* **与既有架构的关系**：新引擎作为 `paiza::CandidateSource` 体系内的实现层
  演进（静态层替换 `LocalDictionarySource`，动态层为新增的
  `UserDictionary`），`InputSession`、按键策略与三端前端**保持不变**——
  这是本仓库既定的三端行为一致性契约。

```
 前端 (TSF / IMK / IBus / 外部宿主 via FFI)
        │ 按键                     ▲ 候选/上屏
        ▼                          │
  InputSession（按键策略，平台无关）
        ▼
  CandidateEngine（合并/去重/排序）
   ├─ UserDictionary        动态用户层（内存 + SQLite 异步落盘）  [Phase 1 ✓]
   ├─ StaticLexicon         静态层（Phase 1: 文本词库；Phase 2: FST+mmap）
   ├─ BigramPredictor       CSR 联想（上屏后预测）                [Phase 2]
   └─ CloudCandidateSource  云端（既有占位）
```

## 2. 数据持久化层设计 (SQLite Schema)

SQLite 仅用于**离线词库管理、TXT 导入清洗、用户习惯的异步持久化**，
不参与打字路径上的实时检索。库文件位于用户配置目录
（`UserConfigDirectory()`，三平台路径见 `common/paths.h`）。

```sql
-- 1. 核心静态词表 (Unigram)。由离线编译器消费，生成 FST/CSR 二进制。
CREATE TABLE IF NOT EXISTS Lexicon (
    word_id         INTEGER PRIMARY KEY,
    latin_key       TEXT NOT NULL,      -- 归一化后的拉丁编码
    mongolian_word  TEXT NOT NULL,      -- 蒙古文正字实体（UTF-8）
    frequency       INTEGER NOT NULL    -- 全局绝对词频
);
CREATE INDEX IF NOT EXISTS idx_latin_key ON Lexicon(latin_key);

-- 2. 上下文接续表 (Bigram)。离线压缩为 CSR。
CREATE TABLE IF NOT EXISTS Bigram (
    word_id_1     INTEGER NOT NULL,
    word_id_2     INTEGER NOT NULL,
    co_frequency  INTEGER NOT NULL,
    PRIMARY KEY (word_id_1, word_id_2)
);
CREATE INDEX IF NOT EXISTS idx_bigram_freq
    ON Bigram(word_id_1, co_frequency DESC);

-- 3. 动态用户词库表 (UserHistory)。运行时唯一被写入的表。
CREATE TABLE IF NOT EXISTS UserHistory (
    latin_key           TEXT NOT NULL,
    mongolian_word      TEXT NOT NULL,
    user_frequency      INTEGER DEFAULT 1,
    last_used_timestamp INTEGER DEFAULT 0,   -- Unix 秒；便于时间衰减
    PRIMARY KEY (latin_key, mongolian_word)
);
```

修订说明（相对 v1.0）：

* `last_used_timestamp` 用 **INTEGER Unix 秒**而非 `DATETIME` 文本——
  跨平台可比较、可参与衰减计算，且避免 SQLite 文本日期的隐式转换陷阱。
* 全部 DDL 加 `IF NOT EXISTS`：引擎首次启动自建 schema，无需安装步骤。
* SQLite 以 **amalgamation 源码内置**（`third_party/sqlite/`，公有领域
  许可），三平台零外部依赖；连接打开 `PRAGMA journal_mode=WAL` 以保证
  异步写入不阻塞潜在的并发读。

## 3. 核心内存检索引擎 (In-Memory Engine)

运行时采用「**静态只读大词库 + 动态读写用户小词库**」双轨重载架构。

### 3.1 静态只读层 (Static Layer) — Phase 2

离线编译器读取 SQLite 生成紧凑二进制，启动时 `mmap` 直接映射：

* **字典树 (FST/DAT)**：百万级 `latin_key → word_id` 映射，查询 O(k)，
  k 为输入长度。
* **联想矩阵 (CSR)**：Bigram 压缩为两个数组——
  * `Index_Array[word_id_1]` → 该行在数据区的偏移；
  * `Data_Array`：按 `co_frequency` 降序连续存放 `(word_id_2, weight)`。
  * 复杂度修订：O(1) 是**定位行**；取前 k 个联想词为 O(k)（行内已按
    权重排序，无需再排）。
* **二进制格式要求**（v1.0 缺失，此处补齐）：文件头含 magic、格式版本、
  字节序标记、各段偏移/长度与 CRC32 校验；版本不匹配或校验失败时引擎
  **拒绝加载并回退**到文本词库路径，绝不半加载。
* mmap 在 Windows 上对应 `CreateFileMapping/MapViewOfFile`，封装为
  `common/` 下的跨平台工具。

Phase 1 过渡实现：静态层暂由既有 `LocalDictionarySource`（有序表 +
二分前缀检索）承担，词库仍为 TAB 分隔文本。数万词内毫秒级响应无压力，
接口与 FST 版一致，届时原地替换。

### 3.2 动态用户层 (Dynamic User Layer) — Phase 1 ✓

C++ 内存态轻量结构，实现动态调频与自造词记忆，打字路径零 I/O：

* **`UserDictionary`**（`core/user_dictionary.h`）：
  `std::map<std::string, std::vector<UserWord>>`。
  * 修订：v1.0 的 `unordered_map` 改为**有序 `std::map`**——用户词
    同样要参与**前缀检索**（`lower_bound` 区间扫描），哈希表做不到；
    用户词库量级（数千～数万）下红黑树开销可忽略。
  * `UserWord { mongolian, user_frequency, last_used }`。
* **生命周期**：候选上屏 → `RecordCommit()` 实时更新内存 Map 并标脏 →
  **专用落盘线程**（条件变量驱动，批量 UPSERT，WAL 模式）在写入间隔
  ≥3s 时增量落盘；引擎销毁时同步 Flush。打字线程永不等待磁盘。
* **TXT 导入**（本次新增需求）：`ImportFromFile(path)`——格式与主词库
  一致：`latin_key<TAB>蒙古文<TAB>词频(可选)`，UTF-8，`#` 行为注释。
  **每条记录可以是单个字或词组**：蒙古文字段按 TAB 界定、允许包含
  空格与 NNBSP，故词组天然支持。逐行清洗：非法行跳过并计数，返回
  `{imported, skipped}` 供 UI 呈现；导入立即生效（写内存 + 异步落盘）。
* **上屏通知路径**：
  * macOS / Linux：`InputSession` 持有 `UserDictionary*`，`Commit`
    内记录（进程内，天然实时）。
  * Windows：引擎在服务进程，TSF 端上屏时发送 fire-and-forget 的
    `kCommitCandidate` IPC 消息（协议 v1 追加消息类型，向后兼容）。

## 4. 算法与解码流水线 (Decoding Pipeline)

### 4.1 核心步骤

1. **预处理与等价类映射 (Equivalence Class)** — Phase 2
   * 输入拉丁串先经等价类表展开为归一化检索键。等价类是**数据而非
     代码**：`equivalences.txt`（与 keymap.txt 同目录、同热重载机制），
     每行 `变体字符 → 规范字符`。v1.0 中「`w` 等价 `0x1827`」之类的
     具体规则**由该表配置**，不硬编码——键位布局已数次调整（如近期
     o/q/u/v 元音修订），硬编码必然过期。
2. **容错空间裁剪 (Levenshtein DFA × FST 交集)** — Phase 2/3
   * 输入动态构建编辑距离 ≤1 的 Levenshtein 自动机，与静态 FST 做
     交集遍历剪枝，提取允许 1 个多输/漏输/错位的候选 `word_id` 集。
   * 分期修订：词库达十万级前，**Phase 2 先用「Trie 上带容错预算的
     DFS + 束剪枝」**（实现简单、同样毫秒级）；DFA×FST 交集留待
     百万级词库与 FST 落地后（Phase 3）。两者对上层暴露同一接口。
3. **双轨检索合并 (Merge)** — Phase 1 ✓
   * 静态层容错结果集 ∪ `UserDictionary` 前缀命中集，同键去重
     （`CandidateEngine` 既有逻辑：先到先得，按源优先级排列）。
4. **统一打分与重排 (Scoring & Ranking)** — Phase 1 部分 ✓
   * 修订为**对数域线性模型**（v1.0 的公式混合了概率与对数，量纲
     不一致）：

   ```
   Score = w1·log P_err(输入 | 候选)          — 按键错误模型
         + w2·log P_lm (候选 | 上文)          — Bigram 语言模型
         + w3·log(freq_sys  + 1)              — 系统词频（+1 平滑）
         + w4·log(freq_user + 1)              — 用户词频（+1 平滑）
   ```

### 4.2 权重策略说明

* **P_err（按键惩罚）**：精确匹配 log P_err = 0；每个编辑操作扣固定
  代价；命中等价类**免罚**（等价即同一输入意图）。
* **P_lm（语境增益）**：候选存在于上一上屏词的 CSR 联想行中时给予
  强正向得分；行外回退到 unigram（w3 项已覆盖，不重复计）。
* **w4（用户态高增益）**：Phase 1 以**源优先级**实现语义等价的强提升
  （UserDictionary priority 200 > 静态层 100，用户词恒排前）；
  Phase 2 打分器落地后改为公式内权重，并叠加 `last_used` 时间衰减
  （半衰期建议 30 天），避免一次误选永久霸榜。
* 四个权重与衰减参数入配置文件，不硬编码。

## 5. C++ 核心接口定义 (C-API for FFI)

`core/engine_c_api.h`，`extern "C"` 导出。**内存与线程约定**（v1.0
缺失，FFI 边界必须明确）：

* 所有接口**非线程安全**，宿主须从单一线程调用（输入法场景天然满足）；
  落盘线程为引擎内部实现细节，对宿主透明。
* `Engine_Search` 返回的字符串由引擎持有，**下一次任何 Engine_ 调用
  或 Destroy 后失效**；宿主需要留存必须自行拷贝。UTF-8 编码。
* 所有路径参数为 UTF-8。

```c
extern "C" {
  // 引擎初始化。db_path: 用户词库 SQLite（不存在则自建 schema）。
  // fst_path/csr_path: 静态二进制，Phase 1 传词库 TXT 路径 / NULL。
  bool Engine_Initialize(const char* db_path,
                         const char* lexicon_path,
                         const char* csr_path);

  // 按键检索。latin_input: 归一化前拉丁串；context_word_id: 上一
  // 上屏词 id（无上下文传 0，Phase 2 生效）。返回 JSON：
  // {"candidates":[{"text":"…","source":"user|lexicon","freq":N},…]}
  const char* Engine_Search(const char* latin_input,
                            uint32_t context_word_id);

  // 上屏确认：更新用户动态词库（内存实时 + 异步落盘）。
  void Engine_CommitCandidate(const char* latin_input,
                              const char* mongolian_word_utf8);

  // 导入用户词库 TXT（格式见 §3.2）。返回成功导入条数，-1 = 打不开。
  int Engine_ImportUserDictionary(const char* txt_path_utf8);

  // 资源释放（内部先同步 Flush 用户词库）。
  void Engine_Destroy();
}
```

修订说明：`Engine_CommitCandidate` 去掉了 v1.0 的 `word_id` 参数——
用户自造词没有 word_id，动态层主键就是 `(latin_key, word)`；新增
`Engine_ImportUserDictionary` 支撑设置页导入功能。

## 6. 设置页「导入用户词库」(本次新增，Phase 1 ✓)

* 三端设置页新增 **导入用户词库…** 按钮：文件选择器（*.txt）→
  `UserDictionary::ImportFromFile` → 弹出结果（成功 N 条 / 跳过 M 行）。
* Windows：设置窗口与引擎同在服务进程，进程内直调；导入即刻参与检索。
* macOS / Linux：设置窗口与引擎同进程，路径相同。
* 文案入 `core/localization`（en / 中文 / Монгол кирилл 三语）。

## 7. 分期落地路线 (Milestones)

| 阶段 | 内容 | 状态 |
|---|---|---|
| **Phase 1** | SQLite 内置；`UserDictionary` 双轨动态层（内存 + 异步落盘 + 调频 + TXT 导入）；上屏通知三端打通（进程内 / IPC）；C FFI 骨架（含 Search/Commit/Import）；设置页导入 UI；单测全覆盖 | **本次实现** |
| **Phase 2** | 离线编译器（SQLite→二进制）；mmap 静态层替换文本词库；等价类表；Trie 容错检索（编辑距离≤1）；对数域打分器 + 时间衰减；Bigram/CSR 联想与 `context_word_id` 生效 | 设计定稿 |
| **Phase 3** | FST/DAT 替换 Trie；Levenshtein DFA × FST 交集遍历；百万级词库性能验收（P99 < 5ms）；打分权重调参工具 | 规划 |

验收基线：每阶段保持既有 GoogleTest 全绿 + 三平台 CI 通过；
`InputSession` 行为契约测试不变即前端零改动。
