# 断层航线

竖屏飞机躲避小游戏。点「开始飞行」即开局，按住屏幕拖动来控制飞机。浏览器也支持鼠标拖动、方向键和 WASD，空格可开局、暂停和重开，X 可触发相位反击。

## 马上试玩

用浏览器直接打开 [index.html](index.html)。游戏代码和素材都在本地，无需安装依赖或启动服务。侧边栏和激励视频通过抖音宿主运行。

## 玩法

- 每存活 1 秒得 10 分。擦边基础得分为 30 分，1.7 秒内再次擦边可叠加倍率，最高 4 倍。受伤会打断连擦，每个障碍只计一次。
- 初始相位能量为 1 格，每次成功擦边充 1 格。攒满 3 格后，点底栏的「相位反击」或按 X，可加速并穿越火力约半秒。碰到蓝色护罩的敌机弹丸会反射回去，每击落一架敌机得 120 分。相位和受击保护期间擦边不计分，也不充能。
- 机体有 3 格血量，每次命中扣 1 格，随后约 0.66 秒内免伤。受损时场内会出现绿色医疗无人机，点一下补 1 格。补给约 3.4 秒后消失，可以一指拖动，另一指点补给或相位按钮。
- 首队敌机在开局约 4 秒来袭。后续升级为预判扇形弹、双机夹击、交叉弧线及短时追踪弹。准星会提前标出锁定位置，橙色菱形弹在光环消失前会转向。最高难度的敌机有两层装甲。
- 陨石、矿墙和激光在敌机交战间隙继续出现。危险等级越高，障碍越快，矿墙缺口越窄，后期缺口还会左右摆动。
- 血量归零后可直接重开。抖音广告位配置生效后，也可主动看完一条激励视频，在同一局续飞一次。续飞恢复 2 格血量，保留分数和难度，并清理场上火力。
- 本机保存最高分，切到后台时自动暂停。从首页侧边栏复访后可领取当天的青色尾焰，尾焰仅改变外观。

## 抖音开发者工具

1. 在开发者工具的「小游戏」中，导入本 README 所在的 `rift-runner` 目录。根目录已包含 `game.js`、`game.json` 和 `project.config.json`。
2. 在导入界面选择可用的小游戏测试号，或在 [project.config.json](project.config.json) 中填入有权限的小游戏 AppID。仓库使用原模板的 `touristappid` 占位。
3. 在对应 AppID 的控制台「商业化 → 流量主 → 广告管理」创建激励视频广告位，把 ID 填入 [src/douyin-iaa.js](src/douyin-iaa.js) 的 `REWARDED_VIDEO_AD_UNIT_ID`。该值为空时，结算页提供直接重开。
4. 编译后，点「侧边栏复访」再点「打开首页侧边栏」，从侧边栏点游戏图标返回，确认可领取今日尾焰。也可在开发者工具添加场景值 `021036` 的编译模式，模拟侧边栏启动。
5. 使用工具栏「预览」并用抖音扫码。检查拖动时用第二只手指点补给或相位、擦边充能和反弹击落、直接重开、看完整视频续飞、提前关闭、广告拉取失败和续飞次数限制。

工程使用原生 Canvas 和 `tt` 接口，无需构建。正式提审使用对应小游戏的正式 AppID。真实广告展示取决于该广告位的状态和当次广告填充。

[本地接入指南](../抖音接入完整指南.md) · [官方侧边栏指南](https://developer.open-douyin.com/docs/resource/zh-CN/mini-game/develop/guide/open-ability/Introduction-for-tech) · [官方激励视频指南](https://developer.open-douyin.com/docs/resource/zh-CN/mini-game/develop/guide/open-ability/ad/incentive-ads)

## 素材

开场背景使用内置 ImageGen 生成，压缩后的文件位于 [images/asteroid-corridor.jpg](images/asteroid-corridor.jpg)。飞机、障碍和界面由 Canvas 绘制。

生成提示词

> Use case: stylized-concept. Asset type: portrait arcade space game start-screen background behind separately drawn UI. Create an original 2:3 portrait cinematic illustration of a fragmented asteroid corridor in deep black-violet space. Frame a distant, vivid acid-lime light at the center at 48% height, with restrained streaks and subtle dust converging toward it. Jagged dark basalt asteroids with hints of violet and faint chartreuse edge light frame the sides in the middle third, creating depth. Very dark, nearly black and calm top 25% and bottom 28% so overlaid text can be read. Keep the central lane clear and the star field delicate. Hard-edged retro arcade-meets-modern film concept art, fine texture, premium, high contrast in the central band, black-violet and acid-lime palette. No spaceship, no glyphs, no letters, no typography, no logos, no interface, no border.
