#include "format_help.hpp"

namespace labelmaster::service {

QString formatHelpHtml() {
    return QString::fromUtf8(R"HTML(
<h2>标签格式与字段顺序</h2>
<p>一个非空行表示一个装甲板，各字段使用空白字符分隔。下列字段顺序均为从左到右。</p>

<h3>通用字段含义</h3>
<table border="1" cellspacing="0" cellpadding="5">
<tr><th align="left">字段</th><th align="left">含义</th></tr>
<tr><td><code>color</code></td><td>颜色 ID：0=Blue（蓝），1=Red（红），2=Gray（灰），3=Purple（紫）。</td></tr>
<tr><td><code>size</code></td><td>装甲板尺寸：0=Small（小），1=Big（大）。</td></tr>
<tr><td><code>class</code></td><td>类别 ID：0=G（哨兵），1～5=对应数字，6=O（前哨站），7=B（基地）。</td></tr>
<tr><td><code>center_x center_y</code></td><td>bbox 中心点的归一化 x、y 坐标；在部分格式中记作 <code>x y</code> 或 <code>x_c y_c</code>。</td></tr>
<tr><td><code>width height</code></td><td>bbox 的归一化宽、高；在部分格式中记作 <code>w h</code>，两者必须非负。</td></tr>
<tr><td><code>x0 y0 … x3 y3</code></td><td>四个关键点的归一化坐标；点序固定为 0=左上（TL）、1=左下（BL）、2=右下（BR）、3=右上（TR）。</td></tr>
<tr><td><code>v0 … v3</code></td><td>对应关键点的可见性：0=不可见，1=不在范围内，2=可见。</td></tr>
<tr><td><code>class_id</code></td><td>把颜色、尺寸和类别组合成一个整数；具体编码由格式决定，见各格式说明。</td></tr>
</table>
<p>所有 bbox 和关键点坐标均为归一化值，并允许小于 0 或大于 1，以表示越过图像边界的标注。</p>

<h3>LabelMaster V1（10 字段）</h3>
<p><code>color legacy_class x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p><code>color</code> 和关键点含义见通用字段。<code>legacy_class</code>：0=Small G，1=Big 1，2/3/4=Small 2/3/4，5=Small O，6 或 7=Small Base。</p>

<h3>LabelMaster V2 / Points Only（11 字段）</h3>
<p><code>color size class x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p>全部字段使用上面的通用定义。</p>

<h3>LabelMaster V3 / Rect + Points（15 字段）</h3>
<p><code>color size class center_x center_y width height x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p>在 V2 的基础上增加 bbox；全部字段使用上面的通用定义。</p>

<h3>LabelMaster V4（13 字段，36 类）</h3>
<p><code>class_id center_x center_y width height x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p><code>class_id = color × 9 + tag</code>，其中 <code>color</code> 使用通用颜色 ID；每种颜色的 <code>tag</code> 依次为：0=Small G，1=Big 1，2/3/4/5=Small 2/3/4/5，6=Small O，7=Small Base，8=Big Base。</p>

<h3>LabelMaster V5 / legacy YOLO Pose（17 字段，14 类）</h3>
<p><code>class_id center_x center_y width height x0 y0 v0 x1 y1 v1 x2 y2 v2 x3 y3 v3</code></p>
<p><code>class_id</code> 范围为 0～13：0～6 表示 Blue，7～13 表示 Red；每组依次为 Small 2/3/4/5/Base/G/O。该格式没有 Purple、Gray 或 Big 编码。</p>

<h3>LabelMaster V6（19 字段，当前格式）</h3>
<p><code>color size class center_x center_y width height x0 y0 v0 x1 y1 v1 x2 y2 v2 x3 y3 v3</code></p>
<p>全部字段使用上面的通用定义；这是唯一直接打开且保留逐点可见性的格式。</p>

<h3>HITSZ（10 字段）</h3>
<p><code>x0 y0 x1 y1 x2 y2 x3 y3 legacy_class color</code></p>
<p><code>color</code> 和关键点含义见通用字段。<code>legacy_class</code>：0=Small G，1=Big 1，2～7=Small 2/3/4/5/O/Base，8=Big Base，9/10/11=Big 3/4/5。</p>

<h3>UPC（10 字段）</h3>
<p><code>color legacy_class x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p><code>legacy_class</code> 使用与 HITSZ 相同的编码。字段布局与 V1 相同；当类别只落在 0～7 时，需要手动选择 V1 或 UPC。</p>

<h3>UnionSecret（9 字段，39 类）</h3>
<p><code>class_id x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p><code>class_id = color_group × 13 + tag</code>，其中 <code>color_group</code>：0=Blue，1=Red，2=Gray；<code>tag</code>：0～7 依次为 Small G/1/2/3/4/5/O/Base，8～12 依次为 Big Base/G/3/4/5。</p>

<h3>NWPU（9 字段，64 类）</h3>
<p><code>class_id x0 y0 x1 y1 x2 y2 x3 y3</code></p>
<p><code>class_id = color × 16 + size × 8 + class</code>，<code>color</code>、<code>size</code>、<code>class</code> 均使用通用定义。若 9 字段数据的 <code>class_id</code> 全部位于 0～38，需手动选择 UnionSecret 或 NWPU；出现 39～63 时自动判为 NWPU。</p>

<h3>导入行为</h3>
<p>V6 会直接打开，其余支持的格式会统一转换为 V6。不含可见性字段的格式在转换后将四个关键点的可见性设为 2（可见）。</p>
)HTML");
}

} // namespace labelmaster::service
