# 抖音小游戏基础模板

## 项目结构

```
game-template/
├── game.js              # 游戏主入口
├── game.json            # 游戏配置
├── project.config.json  # 项目配置
└── README.md            # 说明文档
```

## 快速开始

### 1. 在抖音开发者工具中打开

1. 打开抖音开发者工具
2. 选择"导入项目"
3. 选择此目录
4. 点击"导入"

### 2. 项目说明

#### game.js
游戏主入口文件，包含：
- 游戏初始化
- 游戏循环（update + render）
- 触摸事件处理
- 游戏状态管理
- 分数系统

#### game.json
游戏配置文件：
- `deviceOrientation`: 屏幕方向（portrait 竖屏 / landscape 横屏）
- `showStatusBar`: 是否显示状态栏
- `networkTimeout`: 网络超时设置

#### project.config.json
项目配置文件：
- `appid`: 小游戏 AppID（开发阶段可使用 "touristappid"）
- `projectname`: 项目名称
- `setting`: 编译设置

## 功能特性

### 已实现功能

✅ 游戏状态管理（菜单、游戏中、游戏结束）
✅ 触摸事件处理
✅ 分数系统
✅ 最高分保存（使用本地存储）
✅ 基础渲染框架
✅ 游戏主循环

### 待扩展功能

- [ ] 游戏对象管理
- [ ] 碰撞检测
- [ ] 音效系统
- [ ] 粒子效果
- [ ] 动画系统
- [ ] 多场景管理
- [ ] UI 组件
- [ ] 数据持久化
- [ ] 网络请求
- [ ] 分享功能
- [ ] 广告接入

## 扩展开发

### 添加新的游戏场景

```javascript
// 在 game.js 中添加新场景
renderGame() {
  // 渲染游戏对象
  this.renderPlayer();
  this.renderEnemies();
  this.renderItems();
}

update(deltaTime) {
  // 更新游戏对象
  this.updatePlayer(deltaTime);
  this.updateEnemies(deltaTime);
  this.checkCollisions();
}
```

### 添加音效

```javascript
// 创建音频上下文
const audio = tt.createInnerAudioContext();
audio.src = 'audio/background.mp3';
audio.loop = true;
audio.play();
```

### 添加分享功能

```javascript
tt.shareAppMessage({
  title: '快来玩这个游戏！',
  imageUrl: 'images/share.png',
  query: 'score=' + this.score
});
```

### 添加激励视频广告

```javascript
// 创建激励视频广告
let rewardedVideoAd = tt.createRewardedVideoAd({
  adUnitId: 'your-ad-unit-id'
});

// 显示广告
rewardedVideoAd.show().then(() => {
  console.log('广告显示成功');
}).catch(err => {
  console.log('广告显示失败', err);
});

// 监听广告关闭
rewardedVideoAd.onClose((res) => {
  if (res && res.isEnded) {
    // 用户完整观看了广告，给予奖励
    this.giveReward();
  }
});
```

## 调试技巧

### 使用 console.log

```javascript
console.log('调试信息:', variable);
```

### 使用调试器

在代码中添加断点：
```javascript
debugger; // 程序会在此处暂停
```

### 查看性能

```javascript
console.time('update');
this.update(deltaTime);
console.timeEnd('update');
```

## 发布流程

### 1. 完善游戏信息

在抖音开放平台填写：
- 游戏名称
- 游戏简介
- 游戏图标
- 游戏截图
- 游戏类别

### 2. 上传代码

在开发者工具中：
1. 点击"上传"
2. 填写版本号（如 1.0.0）
3. 填写更新日志
4. 点击"上传"

### 3. 提交审核

在抖音开放平台：
1. 进入"版本管理"
2. 选择刚上传的版本
3. 点击"提交审核"

### 4. 发布上线

审核通过后：
1. 点击"发布"
2. 游戏正式上线

## 常见问题

### Q: 如何获取用户信息？

```javascript
tt.getUserInfo({
  success(res) {
    console.log('用户信息:', res.userInfo);
  },
  fail(err) {
    console.log('获取用户信息失败:', err);
  }
});
```

### Q: 如何进行网络请求？

```javascript
tt.request({
  url: 'https://api.example.com/data',
  method: 'GET',
  success(res) {
    console.log('请求成功:', res.data);
  },
  fail(err) {
    console.log('请求失败:', err);
  }
});
```

### Q: 如何使用本地存储？

```javascript
// 保存数据
tt.setStorageSync('key', 'value');

// 读取数据
const data = tt.getStorageSync('key');

// 删除数据
tt.removeStorageSync('key');

// 清空所有数据
tt.clearStorageSync();
```

## 学习资源

- [抖音小游戏开发文档](https://developer.open-douyin.com/docs/resource/zh-CN/mini-game/develop/guide/dev-guide/bytedance-mini-game)
- [新手教程](https://partner.open-douyin.com/docs/resource/zh-CN/mini-game/develop/guide/dev-guide/newbie-tutorial)
- [API 文档](https://developer.open-douyin.com/docs/resource/zh-CN/mini-game/develop/api/overview)

## 许可证

MIT License
