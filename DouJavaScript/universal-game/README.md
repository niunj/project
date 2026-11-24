# 通用小游戏开发方案

## 项目概述

这是一个**跨平台小游戏开发方案**，可以在浏览器中开发和测试，然后无缝移植到抖音小游戏平台。

## 核心优势

✅ **先在浏览器开发**：使用熟悉的 Web 开发工具，快速迭代
✅ **一次编写，多处运行**：同一套代码在浏览器和抖音平台运行
✅ **预留广告接口**：广告逻辑已封装，只需替换广告 ID
✅ **平台适配层**：自动识别环境，调用对应 API
✅ **便于调试**：浏览器中可以使用 DevTools 调试

## 项目结构

```
universal-game/
├── src/                      # 源代码
│   ├── adapter.js            # 平台适配器（核心）
│   ├── ad-manager.js         # 广告管理器（预留接口）
│   ├── game-core.js          # 游戏核心逻辑（平台无关）
│   ├── main-browser.js       # 浏览器入口
│   └── main-douyin.js        # 抖音入口
├── index.html                # 浏览器测试页面
├── game-douyin.js            # 抖音小游戏入口（合并后）
├── game.json                 # 抖音小游戏配置
├── project.config.json       # 抖音项目配置
└── README.md                 # 说明文档
```

## 开发流程

### 第一步：在浏览器中开发

```bash
# 1. 打开浏览器测试页面
cd universal-game
# 使用 VS Code Live Server 或其他 HTTP 服务器
# 访问 http://localhost:5500/index.html

# 2. 开发游戏逻辑
# 编辑 src/game-core.js
# 浏览器中实时预览效果

# 3. 使用浏览器 DevTools 调试
# F12 打开开发者工具
# 可以断点调试、查看变量、性能分析
```

### 第二步：移植到抖音小游戏

```bash
# 1. 合并代码文件
# 将 adapter.js + ad-manager.js + game-core.js + game-douyin.js
# 合并成一个文件，重命名为 game.js

# 2. 在抖音开发者工具中打开项目
# 选择 universal-game 目录

# 3. 配置广告 ID
# 在 game-douyin.js 中替换广告 ID

# 4. 测试和上传
# 在抖音开发者工具中测试
# 上传代码，提交审核
```

## 核心文件说明

### 1. adapter.js - 平台适配器（核心）

这是整个方案的**核心**，负责桥接浏览器和抖音平台：

```javascript
class GameAdapter {
  constructor() {
    // 自动检测环境
    this.isDouyin = typeof tt !== 'undefined';
  }

  // 统一的 API 接口
  initCanvas() { /* ... */ }
  onTouchStart() { /* ... */ }
  setStorage() { /* ... */ }
  share() { /* ... */ }
  vibrateShort() { /* ... */ }
}
```

**工作原理**：
- 在抖音环境：调用 `tt.xxx` API
- 在浏览器环境：调用标准 Web API 或模拟

### 2. ad-manager.js - 广告管理器

预留了完整的广告接口：

```javascript
class AdManager {
  // 激励视频广告
  showRewardedVideo(onRewarded, onError) {
    // 浏览器：模拟广告
    // 抖音：调用真实广告 API
  }

  // 插屏广告
  showInterstitial(onClose, onError) { /* ... */ }

  // 预加载广告
  preloadAds() { /* ... */ }
}
```

**使用方式**：
```javascript
// 设置广告 ID
adManager.setAdIds({
  rewardedVideoId: 'adunit-xxxxxxxx',
  interstitialId: 'adunit-xxxxxxxx'
});

// 显示激励视频广告
adManager.showRewardedVideo(
  () => { console.log('获得奖励'); },
  (err) => { console.log('广告失败'); }
);
```

### 3. game-core.js - 游戏核心逻辑

**完全平台无关**的游戏逻辑：

```javascript
class GameCore {
  constructor(adapter) {
    this.adapter = adapter; // 注入适配器
  }

  // 游戏逻辑
  update() { /* ... */ }
  render() { /* ... */ }
  gameLoop() { /* ... */ }
}
```

**关键点**：
- 不直接调用任何平台 API
- 所有平台相关操作通过 `this.adapter` 调用
- 可以在浏览器中完整测试

## 功能特性

### 已实现功能

✅ 平台自动适配
✅ 触摸事件处理
✅ 本地存储
✅ 分享功能
✅ 震动反馈
✅ 音频播放
✅ 激励视频广告
✅ 插屏广告
✅ 游戏状态管理
✅ 粒子效果
✅ 碰撞检测

### 扩展建议

可以继续添加：
- [ ] 多场景管理
- [ ] UI 组件系统
- [ ] 动画系统
- [ ] 关卡系统
- [ ] 成就系统
- [ ] 排行榜
- [ ] 网络请求
- [ ] 数据上报

## 广告接入指南

### 1. 获取广告位 ID

在抖音开放平台创建广告位：
1. 进入"流量主" → "广告管理"
2. 创建激励视频广告位
3. 创建插屏广告位
4. 记录广告位 ID

### 2. 配置广告 ID

```javascript
// 在 game-douyin.js 中
this.adManager.setAdIds({
  rewardedVideoId: 'adunit-123456', // 你的激励视频广告 ID
  interstitialId: 'adunit-789012'   // 你的插屏广告 ID
});
```

### 3. 调用广告

```javascript
// 激励视频广告（用户观看后获得奖励）
adManager.showRewardedVideo(
  () => {
    // 用户完整观看，给予奖励
    player.lives++;
    player.score += 100;
  },
  (err) => {
    // 广告失败或用户取消
    console.log('广告失败:', err);
  }
);

// 插屏广告（游戏结束时展示）
adManager.showInterstitial();
```

### 4. 广告策略建议

**激励视频广告**：
- 游戏失败时提供复活机会
- 观看广告获得金币/道具
- 观看广告解锁关卡

**插屏广告**：
- 游戏结束时展示
- 关卡切换时展示
- 注意：不要频繁展示，影响体验

## 浏览器开发技巧

### 使用 VS Code 开发

推荐扩展：
- Live Server：自动刷新页面
- ESLint：代码检查
- Prettier：代码格式化

### 调试技巧

```javascript
// 1. 使用 console.log
console.log('玩家位置:', player.x, player.y);

// 2. 使用 debugger 断点
debugger;

// 3. 性能分析
console.time('update');
gameCore.update();
console.timeEnd('update');
```

### 模拟触摸事件

在浏览器中：
- 鼠标点击 = 触摸开始
- 鼠标移动 = 触摸移动
- 鼠标释放 = 触摸结束

## 移植到抖音小游戏

### 方法一：手动合并（推荐学习）

```bash
# 1. 创建合并文件
touch game.js

# 2. 按顺序复制代码
# adapter.js
# ad-manager.js
# game-core.js
# game-douyin.js

# 3. 在抖音开发者工具中打开
```

### 方法二：使用构建工具

```bash
# 使用 webpack/rollup 等工具打包
npm install -g rollup
rollup -c
```

### 注意事项

1. **广告 ID**：替换为真实 ID
2. **分享图片**：提供真实图片路径
3. **音频文件**：确保路径正确
4. **代码体积**：抖音小游戏有体积限制

## 性能优化

### 1. 对象池

```javascript
// 复用对象，减少 GC
class ObjectPool {
  create() { /* ... */ }
  release() { /* ... */ }
}
```

### 2. 碰撞检测优化

```javascript
// 使用空间分割
// 减少不必要的碰撞检测
```

### 3. 渲染优化

```javascript
// 使用离屏 Canvas
// 批量绘制
// 减少状态切换
```

## 常见问题

### Q: 浏览器中广告如何测试？

A: 广告管理器在浏览器中会模拟广告行为：
```javascript
// 显示确认框模拟广告
const confirmed = confirm('观看广告获得奖励？');
if (confirmed) {
  onRewarded(); // 调用奖励回调
}
```

### Q: 如何处理平台差异？

A: 使用适配器统一接口：
```javascript
// 不推荐：直接调用平台 API
tt.vibrateShort(); // 浏览器中会报错

// 推荐：通过适配器
this.adapter.vibrateShort(); // 自动适配
```

### Q: 代码体积太大怎么办？

A:
1. 使用代码压缩工具
2. 分包加载
3. 移除未使用的代码
4. 使用更小的游戏引擎

## 学习资源

- [抖音小游戏开发文档](https://developer.open-douyin.com/)
- [Canvas API 文档](https://developer.mozilla.org/zh-CN/docs/Web/API/Canvas_API)
- [JavaScript 游戏开发教程](https://developer.mozilla.org/zh-CN/docs/Games)

## 许可证

MIT License

---

## 快速开始

```bash
# 1. 浏览器开发
打开 index.html

# 2. 编辑游戏逻辑
编辑 src/game-core.js

# 3. 测试功能
在浏览器中测试所有功能

# 4. 移植到抖音
合并代码，配置广告 ID，上传
```

祝开发顺利！🎮
