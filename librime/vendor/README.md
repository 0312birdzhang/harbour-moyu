# Rime 构建依赖

源代码归档随仓库提供，构建前使用 `SHA256SUMS` 校验。LevelDB、Marisa、OpenCC 和 librime 静态链接进用户级后端。Boost Regex 和 yaml-cpp 使用 SailfishOS 提供的库。

构建脚本将 librime 词表的预留空间从 4 KiB 增加到 8 KiB，以容纳 Marisa 在单词条词典中的索引图像。此修补应用于展开后的源码，归档保持其上游校验值。

| 组件 | 版本或提交 | 来源 |
| --- | --- | --- |
| librime | 1.13.1 | https://github.com/rime/librime/tree/1.13.1 |
| LevelDB | 1.23 | https://github.com/google/leveldb/tree/1.23 |
| Marisa | 0.2.6，随 OpenCC 源码提供 | https://github.com/BYVoid/OpenCC/tree/ver.1.1.9/deps/marisa-0.2.6 |
| OpenCC | 1.1.9 | https://github.com/BYVoid/OpenCC/tree/ver.1.1.9 |
| 简体拼音词典 | 0c6861ef7420ee780270ca6d993d18d4101049d0 | https://github.com/rime/rime-pinyin-simp/tree/0c6861ef7420ee780270ca6d993d18d4101049d0 |
| 八股文词频 | 054920de4f54c9e5994276a96a4fc2a35cb51aa3 | https://github.com/rime/rime-essay/tree/054920de4f54c9e5994276a96a4fc2a35cb51aa3 |
| 雾凇拼音词库 | 927e1cf1d3eeb9bf4be85e3f684dba4dd485b6f6，2026-11-11 | https://github.com/iDvel/rime-ice/tree/927e1cf1d3eeb9bf4be85e3f684dba4dd485b6f6 |

`librime/data/legacy-spellings.txt` 保存旧 Android 拼音词典的拼音 ID 顺序，用于读取升级前的用户词典。该表来自本仓库原有的 Apache-2.0 Android 拼音数据。

`librime/data/moyu_pinyin.dict.yaml` 按雾凇的顺序导入 8105 字表、基础、扩展、腾讯和杂项词库，并通过简体拼音词典补充旧词汇。构建时将雾凇的 lve 和 nve 转换为已有用户词典采用的 lue 和 nue，并保留旧 Android 拼音词典中的“黁”（nun）。
