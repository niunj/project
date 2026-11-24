/**
 * 主入口文件 - 抖音小游戏版本
 * 用于在抖音小游戏平台运行
 */

// 引入适配器、游戏核心和广告管理器
// 注意：在抖音小游戏中需要通过 require 或 script 标签引入

class Game {
  constructor() {
    // 创建适配器
    this.adapter = new GameAdapter();

    // 创建游戏核心
    this.gameCore = new GameCore(this.adapter);

    // 创建广告管理器
    this.adManager = new AdManager(this.adapter);

    // 设置广告 ID（替换为真实的广告 ID）
    this.adManager.setAdIds({
      rewardedVideoId: 'your-rewarded-video-ad-id',
      interstitialId: 'your-interstitial-ad-id'
    });

    // 扩展游戏功能
    this.extendGame();

    // 启动游戏
    this.gameCore.start();
  }

  /**
   * 扩展游戏功能
   */
  extendGame() {
    // 在游戏结束时显示插屏广告
    const originalGameOver = this.gameCore.gameOver.bind(this.gameCore);
    this.gameCore.gameOver = () => {
      originalGameOver();

      // 延迟显示插屏广告
      setTimeout(() => {
        this.adManager.showInterstitial();
      }, 1000);
    };

    // 添加复活功能（通过观看广告）
    this.addReviveFeature();
  }

  /**
   * 添加复活功能
   */
  addReviveFeature() {
    // 在游戏结束界面添加复活按钮
    const originalRenderMenu = this.gameCore.renderMenu.bind(this.gameCore);
    this.gameCore.renderMenu = () => {
      originalRenderMenu();

      // 如果是游戏结束状态，显示复活按钮
      if (!this.gameCore.state.isPlaying && this.gameCore.state.score > 0) {
        const ctx = this.gameCore.ctx;
        const width = this.gameCore.canvas.width;
        const height = this.gameCore.canvas.height;

        ctx.fillStyle = '#4CAF50';
        ctx.fillRect(width / 2 - 80, height * 0.7, 160, 40);

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
          title: '复活失败',
          icon: 'none'
        });
      }
    );
  }
}

// 启动游戏
new Game();
