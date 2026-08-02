# AT LabelMaster

Actor Thinker 数据集标注工具

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)
![C++](https://img.shields.io/badge/C++-20-blue.svg)
![Qt](https://img.shields.io/badge/Qt-5/6-41cd52.svg)
![OpenVINO](https://img.shields.io/badge/OpenVINO-Toolkit-00285e.svg)
![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C.svg)

## 快捷键

所有键盘操作都可在“设置 → 键位设置”中查看和修改；重复键位会被标红并禁止保存。主要默认键位包括：

- `U` / `Ctrl+R`：撤销 / 重做标注编辑
- `0`：将选中 Detector 的 class 设为 `G`
- `F/G/C/V`：左上 / 右上 / 左下 / 右下角点；单按切换可见/不可见，连续双按设为不在范围内
- `R/Y/B/P`：将颜色设为 Red / Gray / Blue / Purple
- `W/A/S/D`：向上 / 左 / 下 / 右切换选中的 Detector

鼠标与拖拽修饰键：

- `Ctrl + 左键拖拽`：Mask 遮蔽某片区域
- `Alt + 拖拽角点`：梯形模式
- `Shift + 拖拽角点`：平行四边形模式
- 左键 : 新建一个目标
- 右键 : 删除一个目标
- 中键 : 移动视图
- 滚轮 : 缩放视图

十字准心会覆盖整个画布，光标位于图片外侧的黑色区域时仍然显示。
选中 Detector 的不可见和不在范围内角点显示为空心圆，只有可见角点显示为实心圆。

## AT 数据集格式

默认使用 19 字段 LabelMaster V6：

```text
color size class center_x center_y width height x0 y0 v0 x1 y1 v1 x2 y2 v2 x3 y3 v3
```

bbox 和关键点均采用归一化坐标，并允许小于 `0` 或大于 `1`，以表示位于图像外的标注；bbox 宽高仍必须非负。应用内部和文件点序均为 `TL -> BL -> BR -> TR`。关键点可见性定义为 `0=不可见`、`1=不在范围内`、`2=可见`。支持 bbox 的格式可在画布上拖动方形角柄编辑自动生成的 bbox；Mask 仍限制在图像内部。

原 17 字段 V6 已重命名为 V5，布局为 `class_id bbox (x y visibility)[4]`。V5 固定使用 14 类编码，`class_id` 允许范围为 `0～13`，打开后转换为新 V6。V4 固定使用 36 类编码。

打开目录时会扫描全部非空标签并自动识别格式，不再需要在设置中选择输入或输出格式。新 V6 直接打开；LabelMaster V1/V2/V3/V4/V5、HITSZ、UPC、NWPU 和 UnionSecret 格式全部自动转换为新 V6。V1/UPC 或 UnionSecret/NWPU 无法自动区分时会弹窗要求手动选择。没有可见性信息的格式在转换后默认四点全部可见（值为 `2`）。空数据集默认 V6。选定格式后，class 范围、字段数量或内容错误的样本会进入 `stage` 冲突处理流程，不影响其他合法标签完成导入。冲突对话框右上角的 `?` 可查看全部格式说明。

目录完成校验与转换后，如果标签目录中存在没有同名图片、当前数据集不会使用的 `.txt`，程序会提示是否清理，并在详细信息中列出文件；只有确认后才会删除。

### UnionSecret 格式导入

该格式每行有 9 个字段：`combined-class x0 y0 x1 y1 x2 y2 x3 y3`。B、R、G 每种颜色占 13 个连续编号：组内 `0～7` 为 Small `G/1/2/3/4/5/O/Base`，`8～12` 为 Big `Base/G/3/4/5`。因此 `7/20/33` 分别是 B/R/G Small Base，`8/21/34` 分别是 B/R/G Big Base。

UnionSecret 与 NWPU 都使用 9 字段。只出现 `class_id 0～38` 时，程序会弹窗要求选择“UnionSecret 格式”或“NWPU 格式”；出现 `39～63` 时自动识别为 NWPU。

### Stage 冲突处理

导入时发现某个标签不符合选定格式，会将整张图片和整个标签移入数据集根目录的 `stage/images` 与 `stage/labels`，合法标签继续转为 V6。之后程序自动打开 stage。每次进入 stage 都会先同时严格检查“本次导入格式”和 LabelMaster V6：已经是 V6 的标签原样回迁；符合导入格式的标签直接转换成 V6 后回迁；两者都不符合的才进入手动标注，可解析的原格式标注行会保留，错误行需要重标。手动保存会写入并重新读取为 V6，通过后样本回到原始 `images/labels`。右下角的“强制合并”可以跳过两种格式校验和转换，直接回迁原文件，但会显示覆盖与格式风险警告。应用启动或重新打开数据集时若发现未完成的 stage，会优先恢复冲突处理，全部处理完后自动返回原数据集。

### 筛选模式

工具菜单中的“筛选模式”可以按 `color / size / class` 的任意精确组合筛出不需要的样本。命中的图片和标签会成对移动到图片目录父级的 `filtering/images` 与 `filtering/labels`，并在独立复核窗口中显示检测框。复核窗口支持亮度提升、恢复到原路径和直接删除；队列清空后自动删除空的 `filtering` 目录并返回正常模式。中途关闭时可再次进入筛选模式继续处理。

经典的交龙数据集格式同样支持，直接打开图片目录即可自动识别并导入：
[交龙数据集](https://github.com/xinyang-go/LabelRoboMaster?tab=readme-ov-file#%E8%A3%85%E7%94%B2%E6%9D%BF%E7%B1%BB%E5%88%AB%E5%91%BD%E5%90%8D%E4%B8%8E%E7%B1%BB%E5%88%AB%E7%BC%96%E5%8F%B7)

我们拆分了检测头，让Label也可读一些。

不过在该标注工具中，我们完全移除了对5号的支持，并删除了3 4 5号大装甲。

新标注的数据集也不再希望支持五号，我们对所有五号装甲板都进行了Mask操作

后续标注时遇到轨道哨兵，3/4/5号进行mask操作即可。

这样做的理由是保护我们无辜的标注人员，不要陷入标注地狱啊()

### Color
| int | color |
| :---: | :---: |
| 0 | BLUE |
| 1 | RED |
| 2 | GRAY |
| 3 | PURPLE |

### Class
| int | class |
| :---: | :---: |
| 0 |  G |
| 1 |  1 |
| 2 |  2 |
| 3 |  3 |
| 4 |  4 |
| 5 |  5 |
| 6 | O（前哨站） |
| 7 | B（基地，大小由 `size` 区分） |

### Size
| int | size |
| :---: | :---: |
| 0 | 小装甲 |
| 1 | 大装甲 |

### Points
从左上角开始逆时针旋转
- 左上角点归一化坐标x
- 左上角点归一化坐标y
- 左下角点归一化坐标x
- 左下角点归一化坐标y
- 右下角点归一化坐标x
- 右下角点归一化坐标y
- 右上角点归一化坐标x
- 右上角点归一化坐标y

## 标注规范 & 小灯必读

### 目标
在 RoboMaster(以下简称RM) 中，我们需要对上述数据集格式中的所有装甲板进行识别，一共是 **4 * 8 = 32** 种装甲板。

拆分了检测头的情况下，参考其他学校的实现，每一个类别大概需要2000张左右，才有希望训练出足够鲁棒的模型。也就是说，总数据集大概需要2w张

神经网络识别的最好情况无非是和预先的标注完全重合，所以标注的精度很大程度决定了识别精度的上限，所以标注时需要非常认真。

蒙版的图样的绿线需要作为识别图案的黑白交界处的分界线。


### 典型的标注错误

强品硬凑蒙版，忽略灯条，应以灯条为主

![错误1](docs/zh_cn/error1.png)

![错误2](docs/zh_cn/error2.png)

强拼硬凑蒙版，香蕉线(标的什么玩意)

![错误3](docs/zh_cn/error3.png)

左右不分

![错误4](docs/zh_cn/error4.png)

### TBD


## 其他
更多标注标准，参照哈工深的标注工具。都是非常宝贵的标注经验。

同时，有意愿交换数据集的学校可以联系我: 3159890292@qq.com

## 鸣谢
我们hard fork了SJTU的标注工具，精简了功能，提高了可能的可维护性（其实我也不知道算不算，只不过我们队内维护起来比较顺手）

[上海交通大学 LabelRoboMaster](https://github.com/xinyang-go/LabelRoboMaster)

[哈尔滨工业大学深圳 LabelRoboMaster](https://github.com/MonthMoonBird/LabelRoboMaster)

## License
MIT License
