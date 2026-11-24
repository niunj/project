/**
 * 抖音小游戏 - 基础模板
 * 基于 JavaScript + Canvas 2D
 */

class DouyinGame {
  constructor() {
    // 创建画布
    this.canvas = tt.createCanvas();
    this.ctx = this.canvas.getContext('2d');

    // 游戏状态
    this.gameState = 'menu'; // menu, playing, paused, gameover
    this.score = 0;
    this.highScore = 0;

    // 游戏配置
    this.config = {
      fps: 60,
      backgroundColor: '#1a1a2e',
      textFillColor: '#ffffff',
      accentColor: '#ff6b6b'
    };

    // 初始化
    this.init();
  }

  /**
   * 初始化游戏
   */
  init() {
    console.log('游戏初始化');

    // 加载最高分
    this.loadHighScore();

    // 绑定事件
    this.bindEvents();

    // 开始游戏循环
    this.lastTime = Date.now();
    this.gameLoop();
  }

  /**
   * 加载最高分
   */
  loadHighScore() {
    try {
      const data = tt.getStorageSync('highScore');
      if (data) {
        this.highScore = parseInt(data);
      }
    } catch (e) {
      console.log('加载最高分失败:', e);
    }
  }

  /**
   * 保存最高分
   */
  saveHighScore() {
    try {
      tt.setStorageSync('highScore', this.highScore.toString());
    } catch (e) {
      console.log('保存最高分失败:', e);
    }
  }

  /**
   * 绑定触摸事件
   */
  bindEvents() {
    // 触摸开始
    tt.onTouchStart((res) => {
      const touch = res.touches[0];
      this.handleTouch(touch.clientX, touch.clientY);
    });

    // 触摸移动
    tt.onTouchMove((res) => {
      const touch = res.touches[0];
      this.handleTouchMove(touch.clientX, touch.clientY);
    });

    // 触摸结束
    tt.onTouchEnd((res) => {
      this.handleTouchEnd();
    });
  }

  /**
   * 处理触摸
   */
  handleTouch(x, y) {
    console.log('触摸位置:', x, y);

    switch (this.gameState) {
      case 'menu':
        this.startGame();
        break;
      case 'playing':
        // 游戏中的触摸处理
        this.onGameTouch(x, y);
        break;
      case 'gameover':
        this.restartGame();
        break;
    }
  }

  /**
   * 处理触摸移动
   */
  handleTouchMove(x, y) {
    if (this.gameState === 'playing') {
      // 游戏中的触摸移动处理
    }
  }

  /**
   * 处理触摸结束
   */
  handleTouchEnd() {
    // 触摸结束处理
  }

  /**
   * 开始游戏
   */
  startGame() {
    this.gameState = 'playing';
    this.score = 0;
    console.log('游戏开始');
  }

  /**
   * 重新开始游戏
   */
  restartGame() {
    this.startGame();
  }

  /**
   * 游戏结束
   */
  gameOver() {
    this.gameState = 'gameover';

    // 更新最高分
    if (this.score > this.highScore) {
      this.highScore = this.score;
      this.saveHighScore();
    }

    console.log('游戏结束，得分:', this.score);
  }

  /**
   * 游戏中的触摸处理
   */
  onGameTouch(x, y) {
    // 示例：点击增加分数
    this.score += 10;

    // 示例：达到一定分数后游戏结束
    if (this.score >= 100) {
      this.gameOver();
    }
  }

  /**
   * 更新游戏逻辑
   */
  update(deltaTime) {
    if (this.gameState !== 'playing') {
      return;
    }

    // 在这里添加游戏逻辑更新
    // 例如：移动对象、碰撞检测等
  }

  /**
   * 渲染画面
   */
  render() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    // 清空画布
    ctx.fillStyle = this.config.backgroundColor;
    ctx.fillRect(0, 0, width, height);

    // 根据游戏状态渲染不同界面
    switch (this.gameState) {
      case 'menu':
        this.renderMenu();
        break;
      case 'playing':
        this.renderGame();
        break;
      case 'gameover':
        this.renderGameOver();
        break;
    }
  }

  /**
   * 渲染菜单界面
   */
  renderMenu() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    // 标题
    ctx.fillStyle = this.config.textFillColor;
    ctx.font = 'bold 48px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('抖音小游戏', width / 2, height / 3);

    // 开始提示
    ctx.font = '24px Arial';
    ctx.fillText('点击屏幕开始', width / 2, height / 2);

    // 最高分
    if (this.highScore > 0) {
      ctx.font = '20px Arial';
      ctx.fillStyle = this.config.accentColor;
      ctx.fillText(`最高分: ${this.highScore}`, width / 2, height * 2 / 3);
    }
  }

  /**
   * 渲染游戏界面
   */
  renderGame() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    // 分数
    ctx.fillStyle = this.config.textFillColor;
    ctx.font = 'bold 32px Arial';
    ctx.textAlign = 'left';
    ctx.fillText(`分数: ${this.score}`, 20, 50);

    // 游戏内容（示例：显示一个彩色方块）
    const blockSize = 100;
    const blockX = (width - blockSize) / 2;
    const blockY = (height - blockSize) / 2;

    ctx.fillStyle = this.config.accentColor;
    ctx.fillRect(blockX, blockY, blockSize, blockSize);

    // 提示文字
    ctx.fillStyle = this.config.textFillColor;
    ctx.font = '20px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('点击方块得分', width / 2, blockY + blockSize + 50);
  }

  /**
   * 渲染游戏结束界面
   */
  renderGameOver() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    // 游戏结束文字
    ctx.fillStyle = this.config.accentColor;
    ctx.font = 'bold 48px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('游戏结束', width / 2, height / 3);

    // 得分
    ctx.fillStyle = this.config.textFillColor;
    ctx.font = '32px Arial';
    ctx.fillText(`得分: ${this.score}`, width / 2, height / 2);

    // 最高分
    ctx.font = '24px Arial';
    ctx.fillText(`最高分: ${this.highScore}`, width / 2, height / 2 + 50);

    // 重新开始提示
    ctx.font = '20px Arial';
    ctx.fillText('点击屏幕重新开始', width / 2, height * 2 / 3);
  }

  /**
   * 游戏主循环
   */
  gameLoop() {
    const currentTime = Date.now();
    const deltaTime = currentTime - this.lastTime;
    this.lastTime = currentTime;

    // 更新
    this.update(deltaTime);

    // 渲染
    this.render();

    // 下一帧
    requestAnimationFrame(() => this.gameLoop());
  }
}

// 启动游戏
new DouyinGame();
