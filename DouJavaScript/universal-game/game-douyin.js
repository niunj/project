/**
 * 抖音小游戏入口文件
 * 将此文件重命名为 game.js 用于抖音小游戏平台
 */

// 引入依赖（在抖音小游戏中需要使用 require 或直接合并代码）
// 这里假设所有代码已经合并到一个文件中

// 如果代码分文件，需要这样引入：
// const GameAdapter = require('./src/adapter.js');
// const AdManager = require('./src/ad-manager.js');
// const GameCore = require('./src/game-core.js');

// 启动游戏
class Game {
  constructor() {
    // 创建适配器
    this.adapter = new GameAdapter();

    // 创建游戏核心
    this.gameCore = new GameCore(this.adapter);

    // 创建广告管理器
    this.adManager = new AdManager(this.adapter);

    // 设置广告 ID（替换为你的真实广告 ID）
    this.adManager.setAdIds({
      rewardedVideoId: 'adunit-xxxxxxxx', // 替换为你的激励视频广告 ID
      interstitialId: 'adunit-xxxxxxxx'   // 替换为你的插屏广告 ID
    });

    // 扩展游戏功能
    this.extendGame();

    // 启动游戏
    this.gameCore.start();

    console.log('抖音小游戏启动成功');
  }

  /**
   * 扩展游戏功能
   */
  extendGame() {
    // 1. 在游戏结束时显示插屏广告
    this.addGameOverAd();

    // 2. 添加复活功能
    this.addReviveFeature();

    // 3. 添加分享功能
    this.addShareFeature();

    // 4. 预加载广告
    this.adManager.preloadAds();
  }

  /**
   * 在游戏结束时显示插屏广告
   */
  addGameOverAd() {
    const originalGameOver = this.gameCore.gameOver.bind(this.gameCore);
    this.gameCore.gameOver = () => {
      originalGameOver();

      // 延迟显示插屏广告
      setTimeout(() => {
        this.adManager.showInterstitial(
          () => {
            console.log('插屏广告关闭');
          },
          (err) => {
            console.log('插屏广告失败:', err);
          }
        );
      }, 1000);
    };
  }

  /**
   * 添加复活功能
   */
  addReviveFeature() {
    // 修改渲染菜单，添加复活按钮
    const originalRenderMenu = this.gameCore.renderMenu.bind(this.gameCore);
    this.gameCore.renderMenu = () => {
      originalRenderMenu();

      // 如果是游戏结束状态，显示复活按钮
      if (!this.gameCore.state.isPlaying && this.gameCore.state.score > 0) {
        const ctx = this.gameCore.ctx;
        const width = this.gameCore.canvas.width;
        const height = this.gameCore.canvas.height;

        // 复活按钮背景
        ctx.fillStyle = '#4CAF50';
        this.roundRect(ctx, width / 2 - 80, height * 0.7, 160, 40, 5);

        // 复活按钮文字
        ctx.fillStyle = '#ffffff';
        ctx.font = '18px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('观看广告复活', width / 2, height * 0.7 + 25);
      }
    };

    // 处理复活按钮点击
    const originalOnTouchStart = this.gameCore.onTouchStart.bind(this.gameCore);
    this.gameCore.onTouchStart = (x, y) => {
      if (!this.gameCore.state.isPlaying && this.gameCore.state.score > 0) {
        const width = this.gameCore.canvas.width;
        const height = this.gameCore.canvas.height;

        // 检测是否点击复活按钮
        if (x > width / 2 - 80 && x < width / 2 + 80 &&
            y > height * 0.7 && y < height * 0.7 + 40) {
          this.showReviveAd();
          return;
        }
      }

      originalOnTouchStart(x, y);
    };
  }

  /**
   * 显示复活广告
   */
  showReviveAd() {
    this.adManager.showRewardedVideo(
      () => {
        // 用户完成观看，复活
        this.gameCore.state.lives = 3;
        this.gameCore.state.isPlaying = true;
        this.adapter.showToast({
          title: '复活成功！',
          icon: 'success'
        });
      },
      (err) => {
        // 广告失败
        this.adapter.showToast({
          title: '复活失败，请稍后再试',
          icon: 'none'
        });
      }
    );
  }

  /**
   * 添加分享功能
   */
  addShareFeature() {
    // 在游戏结束界面添加分享按钮
    const originalRenderMenu = this.gameCore.renderMenu.bind(this.gameCore);
    this.gameCore.renderMenu = () => {
      originalRenderMenu();

      if (!this.gameCore.state.isPlaying && this.gameCore.state.score > 0) {
        const ctx = this.gameCore.ctx;
        const width = this.gameCore.canvas.width;
        const height = this.gameCore.canvas.height;

        // 分享按钮背景
        ctx.fillStyle = '#2196F3';
        this.roundRect(ctx, width / 2 - 80, height * 0.8, 160, 40, 5);

        // 分享按钮文字
        ctx.fillStyle = '#ffffff';
        ctx.font = '18px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('分享成绩', width / 2, height * 0.8 + 25);
      }
    };

    // 处理分享按钮点击
    const originalOnTouchStart = this.gameCore.onTouchStart.bind(this.gameCore);
    this.gameCore.onTouchStart = (x, y) => {
      if (!this.gameCore.state.isPlaying && this.gameCore.state.score > 0) {
        const width = this.gameCore.canvas.width;
        const height = this.gameCore.canvas.height;

        // 检测是否点击分享按钮
        if (x > width / 2 - 80 && x < width / 2 + 80 &&
            y > height * 0.8 && y < height * 0.8 + 40) {
          this.shareScore();
          return;
        }
      }

      originalOnTouchStart(x, y);
    };
  }

  /**
   * 分享成绩
   */
  shareScore() {
    this.adapter.share({
      title: `我在躲避游戏中获得了 ${this.gameCore.state.score} 分！`,
      imageUrl: '', // 替换为你的分享图片路径
      query: 'score=' + this.gameCore.state.score
    });
  }

  /**
   * 绘制圆角矩形
   */
  roundRect(ctx, x, y, width, height, radius) {
    ctx.beginPath();
    ctx.moveTo(x + radius, y);
    ctx.lineTo(x + width - radius, y);
    ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
    ctx.lineTo(x + width, y + height - radius);
    ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
    ctx.lineTo(x + radius, y + height);
    ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
    ctx.lineTo(x, y + radius);
    ctx.quadraticCurveTo(x, y, x + radius, y);
    ctx.closePath();
    ctx.fill();
  }
}

// 启动游戏
new Game();
