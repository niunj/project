# 裂环对决

原创横屏 2.5D 格斗小游戏。浏览器直接打开 [index.html](index.html) 即可离线试玩。对战与超大招试演都使用固定的 45° 斜俯视镜头。擂台、角色和技能沿用同一套透视投影，角色的脚下标记指示实际站位。

## 操作

进入人机或双人模式后，先选 1P 英雄，再选电脑对手或 2P 英雄。点按英雄卡可以查看招式和能量消耗。键盘用方向键选人，Enter 确认或进入下一回合，Esc 返回上一步或暂停。

选人时点「试演超大招」或按 P，可直接观看当前英雄的蓄力、释放和命中。试演中 Enter 重播，Esc 返回。点「音效开」或按 M 可静音或恢复声音。

| 模式 | 移动 | 普攻 | 小招 | 中招 | 大招 | 超大招 | 闪避 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 手机 | 左侧摇杆 | 普攻 | 小招 | 中招 | 大招 | 超大招 | 闪避 |
| 键盘 1P | WASD | J | K | U | I | O | L |
| 键盘 2P | 方向键 | 1 | 2 | 4 | 0 | 5 | 3 |

## 对战

- 每回合 45 秒，先赢两回合获胜。读秒结束后按剩余血量判定胜负。
- 普攻免费，可连按三段。小招、中招、大招、超大招分别消耗 17、34、61、92 点能量。能量随时间、命中和受击回复。高阶招式的蓄力、伤害和攻击规模更大。
- 利用纵深走位避开直线技能，观察地面预警躲开延迟落点。回旋弹折返时会前后偏移，引力场命中后会拉近对手。超大招「烈城坠火」依次轰炸五个落点。
- 闪避沿摇杆或方向键输入移动。无方向输入时向远离对手的方向移动。攻击触碰闪避无敌窗口时触发「极限闪避」，每次闪避最多返还 19 点能量。
- 超大招蓄力时，地面金色描边标示危险区，编号标示落点顺序。光束可往前后躲，十字可往斜角躲，扇射可穿过弹道缝隙。看到倒数后可以提前走位，也可以在快接触时闪避。
- 命中时有短暂停顿、受击后仰、震屏和血条损失提示，超大招的停顿约 92 毫秒。极限闪避有单独的音效和提示。
- 键盘和触控摇杆按屏幕方向控制移动。超大招从地面蓄力到空中发射，坠落物、光束和冲击波都使用场地坐标。金色预警限制在可走动区域，依照实际弹道和落点绘制。

## 20 名英雄与 100 个招式

| 英雄 | 定位 | 普攻 | 小招 | 中招 | 大招 | 超大招 |
| --- | --- | --- | --- | --- | --- | --- |
| 岚 | 疾风游击 | 掠拳 | 风刃 | 旋流步 | 断潮 | 千岚回廊 |
| 烬 | 爆炎压制 | 灼肘 | 焰轮 | 焦土环 | 赤域 | 烈城坠火 |
| 霆 | 电轨突袭 | 闪掌 | 电钉 | 反相弧 | 雷网 | 金雷贯轨 |
| 霁 | 寒区控制 | 冰肘 | 凝霜 | 冰棱台 | 极冻井 | 白昼冰原 |
| 砾 | 岩甲重击 | 崩山掌 | 滚石 | 岩门 | 断层 | 三峰倒悬 |
| 萝 | 藤蔓封路 | 青藤刺 | 蔓鞭 | 荆笼 | 绿潮井 | 森罗开界 |
| 弦 | 声波牵制 | 拨弦手 | 和声波 | 回音壁 | 断音线 | 八面回响 |
| 泊 | 水流绕袭 | 穿浪掌 | 飞瀑滴 | 归流水 | 涌浪踏 | 倒悬海幕 |
| 曦 | 日蚀狙击 | 朝晖拳 | 日珥环 | 破晓带 | 午时烈斑 | 九曜交辉 |
| 朔 | 月刃游走 | 新月掌 | 残月轮 | 月影切 | 半月幕 | 月落无声 |
| 烁 | 棱镜散射 | 折光刺 | 分光矢 | 镜像格 | 虹折域 | 棱镜天光 |
| 昼 | 流云步法 | 云手 | 流岫 | 重云 | 风压旋 | 万里云涛 |
| 漠 | 沙幕围猎 | 沙锤 | 沙刺 | 孤柱 | 沙葬 | 千沙沉井 |
| 枢 | 机械轨道 | 液压拳 | 飞齿 | 拘束齿阵 | 交叉轨架 | 零号脉冲 |
| 翎 | 羽刃突进 | 啄影 | 燕掠 | 百羽针 | 折翼扫 | 天羽穿云 |
| 绯 | 丝线反手 | 牵丝手 | 朱丝 | 千结 | 缚影弧 | 织命绯网 |
| 墨 | 影域伏击 | 泼墨肘 | 墨滴 | 黑潮卷 | 砚影踏 | 墨幕压城 |
| 岫 | 星阵定点 | 落星踢 | 飞星 | 双星落 | 交星阵 | 天穹星雨 |
| 冥 | 引力反打 | 坠拳 | 暗核 | 引力门 | 暗面横切 | 重力坍缩 |
| 祈 | 光环压阵 | 裁光掌 | 辉轮 | 巡光带 | 圣幕交界 | 七重天环 |

## 抖音工程

在抖音开发者工具选择「小游戏」并导入此目录。根目录已包含 `game.js`、`game.json` 和 `project.config.json`。使用有权限的 AppID 或测试号，编译后用「预览」扫码。原生横屏版采用人机对战和双指触控。当前交付为可预览的玩法版，正式发布按平台要求补充必接能力。

## 美术

俯视擂台由 Canvas 绘制，包含地砖、立体边缘、悬空轨道和随技能移动的地面光照。角色使用带透视、分面光照、纵深排序和地面阴影的 3D 网格，在 Canvas 上实时投影。选人预览会转动模型，战斗中模型跟随朝向、走位、出招与受击改变动作。选人页的背景素材是 [images/freight-deck.jpg](images/freight-deck.jpg)，由 ImageGen 生成。

20 名英雄分别有独立的比例和装备几何。每个超大招有独立的蓄力姿势、释放动作、特效轨迹和两段合成音效。比如砾的巨石伴随低频重砸，霁的冰柱有碎晶声，绯的丝网使用拨弦声。音效由 Web Audio 在本地合成。抖音小游戏端通过 `tt.getAudioContext()` 播放。

生成提示词

> Use case: stylized-concept. Asset type: wide horizontal 2.5D action game arena backdrop. Original cinematic anime-meets-brutalist science fiction environment, a secluded interplanetary freight deck with an enormous broken steel ring reactor centered in the distance and pale turquoise sunlight behind it. Charcoal steel, cool cyan fog, weathered metal structures, orange embers and warm rust side details. Strong symmetrical perspective, low camera tilted gently down, dramatic machinery and railings confined to far background and side edges. The lower half is a broad flat EMPTY dark blue steel fighting platform receding toward the central horizon at 46% image height, designed for two separately layered sprites. Detailed elevated exterior industrial architecture, hard-edged concept art, moody atmospheric depth, deep black margins, bright desaturated pale blue at distant center. Landscape 16:9 or wider. No characters, no HUD, no foreground objects on the central flat floor, no lettering, no signs, no logos.
