# 墨羽输入法

墨羽输入法提供 SailfishOS 的 Rime 拼音输入、用户词典管理、词典文件导入导出和自定义键盘布局，项目目录为 `harbour-moyu`。

## 实现与来源

0.5.9 使用 librime 1.13.1 和 2026 年 11 月 11 日的雾凇拼音词库，包含 8105 字表、基础、扩展、腾讯和杂项词库。键盘在进程内调用共享 Rime 引擎，词典管理应用通过 D-Bus 访问同一个实例。键盘启动时从独立服务接管引擎，首屏候选按需读取，展开时再加载后续候选。

本项目从搜鸟输入法的 Rime 实现分离。Maliit/QML 前端基于搜鸟及早期开源拼音键盘修改，默认采用四行布局，包含两层符号和长按声调字符。图标使用“霓虹羽墨输入法图标.png”，保留透明边缘并等比缩放为 SailfishOS 使用的 86×86 尺寸。[搜鸟输入法](https://github.com/0312birdzhang/harbour-soumaoime) · [拼音输入法](https://openrepos.net/content/mumudada/pin-yin-shu-ru-fa-shan-zhai-gu-ge) · [Pinyin Prime](https://openrepos.net/content/gexc/pinyin-prime-pin-yin-jian-pan)

## 标识与数据

| 项目 | 墨羽标识 |
| --- | --- |
| RPM 包名 | `harbour-moyu` |
| QML 模块和原生插件 | `xyz.birdzhang.moyu.ime` |
| QML 类型 | `MoyuPinyin` |
| 应用程序 | `harbour-moyu` |
| Sailjail 应用标识 | `xyz.birdzhang.moyu.harbour-moyu` |
| Rime 后端 | `harbour-moyu-rime` |
| D-Bus 服务和接口 | `xyz.birdzhang.moyu.Rime` |
| systemd 用户服务 | `harbour-moyu-rime.service` |
| 设置根路径 | `/app/xyz.birdzhang.moyu.ime` |
| Rime 系统数据 | `/usr/share/harbour-moyu/rime` |
| Rime 用户数据 | `~/.config/harbour-moyu/rime` |
| 拼音方案和扩展词典 | `moyu_pinyin`、`moyu_extra` |
| 键盘注册 | `layouts_moyu.conf`、`zh_cn_moyu.qml`、`MoyuPinyinHandler.qml` |

以上标识及“墨羽词典”导出目录为墨羽独立使用，支持与搜鸟输入法并存。

0.5.5 的 RPM 声明替换先前的 `xyz.birdzhang.moyu.ime` 包，支持直接升级到 `harbour-moyu`。

## 用户词典

在设置中打开“Rime 用户词典”，可以添加 2—8 个汉字的词组，并修改、删除现有词条。已迁移的单字词条也可编辑。拼音支持空格、单引号分隔和连续输入，ü 可写为 v 或 u:。

手动添加的词条立即收录。自动学习按词条及其拼音累计实际提交次数，第 5 次输入时收录。未达标的计数持久保存，升级时已有不足 5 次的自动学习词条转为待学习计数。清空用户词典会同时清除这些计数。

初始输入或光标前没有文字时，候选栏按顺序显示“我、你、不、是、这、有、那、可以、还、一、在、好、这个、要、现在、没”。

常用候选和中文联想随拼音模式启用，独立于应用的通用文本预测开关。光标移动、焦点切换和键盘重新显示时，根据光标前的文字刷新联想。

旧词典迁移范围限定为 `~/.config/harbour-moyu/pinyin/usr_dict.dat`。迁移记录和词条在同一事务内保存，清空用户词典后使用已有的迁移记录。

Rime 用户词典位于 `~/.config/harbour-moyu/rime/moyu_pinyin.userdb`。旧拼写 lve、nve 会转换为 lue、nue，nun 读音由补充词典提供。

## 词典文件管理

设置下拉菜单中的“Rime 词典文件”支持选择本机文件或填写绝对路径导入词典。支持 UTF-8 的 `.dict.yaml`、`.txt` 和 `.tsv`，单个文件最大 32 MiB。词条、完整拼音和可选非负词频使用 Tab 分隔，拼音音节使用空格分隔。含 `import_tables` 的 Rime 主词典需要分别导入它引用的子词典。

长按已导入词典可启用、停用、导出或删除。导出文件保存在用户文档目录的“墨羽词典”文件夹，同名导出自动增加编号。下拉菜单还可以将学习和手动添加的用户词条导出为 Rime 词典。

扩展词典单独编译。编译成功后通过原子切换启用新文件组，并重新加载输入会话。失败时恢复原文件组。文件组位于 `~/.config/harbour-moyu/rime/dictionary-files`，当前导入文件在 `current/files` 下。

## 自定义键盘布局

设置中的“自定义键盘布局”提供字母顺序快捷编辑与完整 JSON 编辑。布局支持 1—6 行、字符键和功能键、两层符号、长按字符、分割位置以及按键宽度。保存后自动应用，空配置或无效配置使用默认键盘。

配置键为 `/app/xyz.birdzhang.moyu.ime/keyboardLayout`。默认定义及格式校验由 `librime/plugin/MoyuLayout.js` 提供。

## 构建

依赖源码归档与 SHA-256 校验表位于 `librime/vendor`。构建过程静态链接 librime、LevelDB、OpenCC 和 Marisa，并预编译 Rime 词典。Sailfish SDK 需要 Boost 和 yaml-cpp 开发包。

在 Sailfish SDK 中执行以下命令。

```bash
mb2 -t "$TARGET" build
```

Windows 工作区可以使用 WSL 构建脚本。

```bash
SDK_USER=pc SFOS_TARGET="$TARGET" ./tools/sailfish-wsl-build.sh
```

详细环境配置见 [WSL 构建说明](docs/sailfish-wsl-build.md)。生成的 RPM 位于 `RPMS`。

## 回归测试

测试构建需要 `qt5-qttest-devel`。在同一 SDK target 中使用 `tests/rime-smoke.pro` 编译测试程序，将程序、`tests/keyboard-harness.qml` 和 `tools/test-rime.sh` 复制到已安装 RPM 的手机后运行。

```bash
QT_QPA_PLATFORM=minimal \
MOYU_RIME_TEST_HARNESS=/path/to/keyboard-harness.qml \
    bash /path/to/test-rime.sh /path/to/moyu-rime-smoke
```

测试使用私有 D-Bus 会话和临时配置目录，涵盖词典路径隔离、旧词典迁移、第五次输入学习、候选选择、部分选词、退格、原始拼音提交、联想、词典事务、并发编辑，以及已安装键盘的默认和自定义布局。临时配置在测试结束后清理。
