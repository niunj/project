/**
 * 主入口文件 - 浏览器版本
 * 用于在浏览器中测试游戏
 */

// 引入适配器和游戏核心
// 注意：在浏览器中需要通过 script 标签引入

class Game {
  constructor() {
    // 创建适配器
    this.adapter = new GameAdapter();

    // 创建游戏核心
    this.gameCore = new GameCore(this.adapter);

    // 创建广告管理器
    this.adManager = new AdManager(this.adapter);

    // 扩展游戏功能
    this.extendGame();

    // 启动游戏
    this.gameCore.start();
  }

  /**
   * 扩展游戏功能
   */
  extendGame() {
    // 添加广告按钮（示例）
    this.addAdButton();

    // 添加分享按钮（示例）
    this.addShareButton();
  }

  /**
   * 添加广告按钮
   */
  addAdButton() {
    const button = document.createElement('button');
    button.textContent = '观看广告获得奖励';
    button.style.cssText = `
      position: absolute;
      top: 10px;
      right: 10px;
      padding: 10px 20px;
      background: #4CAF50;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    `;

    button.addEventListener('click', () => {
      this.showRewardedAd();
    });

    document.body.appendChild(button);
  }

  /**
   * 添加分享按钮
   */
  addShareButton() {
    const button = document.createElement('button');
    button.textContent = '分享';
    button.style.cssText = `
      position: absolute;
      top: 50px;
      right: 10px;
      padding: 10px 20px;
      background: #2196F3;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    `;

    button.addEventListener('click', () => {
      this.share();
    });

    document.body.appendChild(button);
  }

  /**
   * 显示激励视频广告
   */
  showRewardedAd() {
    this.adManager.showRewardedVideo(
      () => {
        // 用户完成观看，给予奖励
        this.gameCore.state.lives++;
        this.gameCore.state.score += 50;
        this.adapter.showToast({
          title: '获得奖励：+1生命，+50分',
          icon: 'success'
        });
      },
      (err) => {
        // 广告失败
        console.log('广告失败:', err);
      }
    );
  }

  /**
   * 分享
   */
  share() {
    this.adapter.share({
      title: '我在玩这个游戏，得分：' + this.gameCore.state.score,
      imageUrl: '', // 需要提供分享图片路径
      query: 'score=' + this.gameCore.state.score
    });
  }
}

// 页面加载完成后启动游戏
window.addEventListener('DOMContentLoaded', () => {
  new Game();
});
