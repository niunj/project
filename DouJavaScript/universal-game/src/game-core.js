/**
 * 通用游戏核心逻辑
 * 不依赖任何平台特定 API，可在浏览器和抖音小游戏中运行
 */

class GameCore {
  constructor(adapter) {
    this.adapter = adapter;
    this.canvas = null;
    this.ctx = null;

    // 游戏状态
    this.state = {
      score: 0,
      highScore: 0,
      lives: 3,
      level: 1,
      isPlaying: false,
      isPaused: false
    };

    // 游戏对象
    this.player = null;
    this.enemies = [];
    this.items = [];
    this.particles = [];

    // 配置
    this.config = {
      fps: 60,
      gravity: 0.5,
      friction: 0.99
    };

    // 时间管理
    this.lastTime = 0;
    this.deltaTime = 0;
  }

  /**
   * 初始化游戏
   */
  init() {
    // 初始化画布
    const { canvas, ctx } = this.adapter.initCanvas();
    this.canvas = canvas;
    this.ctx = ctx;

    // 加载存档
    this.loadProgress();

    // 绑定事件
    this.bindEvents();

    console.log('游戏初始化完成');
  }

  /**
   * 绑定事件
   */
  bindEvents() {
    this.adapter.onTouchStart((res) => {
      const touch = res.touches[0];
      this.onTouchStart(touch.clientX, touch.clientY);
    });

    this.adapter.onTouchMove((res) => {
      const touch = res.touches[0];
      this.onTouchMove(touch.clientX, touch.clientY);
    });

    this.adapter.onTouchEnd(() => {
      this.onTouchEnd();
    });
  }

  /**
   * 触摸开始
   */
  onTouchStart(x, y) {
    if (!this.state.isPlaying) {
      this.startGame();
      return;
    }

    // 游戏中的触摸处理
    this.player && (this.player.targetX = x);
  }

  /**
   * 触摸移动
   */
  onTouchMove(x, y) {
    if (this.state.isPlaying) {
      this.player && (this.player.targetX = x);
    }
  }

  /**
   * 触摸结束
   */
  onTouchEnd() {
    // 触摸结束处理
  }

  /**
   * 开始游戏
   */
  startGame() {
    this.state.isPlaying = true;
    this.state.score = 0;
    this.state.lives = 3;
    this.state.level = 1;

    // 初始化玩家
    this.player = {
      x: this.canvas.width / 2,
      y: this.canvas.height - 100,
      width: 60,
      height: 60,
      targetX: this.canvas.width / 2,
      speed: 8,
      color: '#4CAF50'
    };

    // 清空对象
    this.enemies = [];
    this.items = [];
    this.particles = [];

    console.log('游戏开始');
  }

  /**
   * 游戏结束
   */
  gameOver() {
    this.state.isPlaying = false;

    // 更新最高分
    if (this.state.score > this.state.highScore) {
      this.state.highScore = this.state.score;
      this.saveProgress();
    }

    // 震动反馈
    this.adapter.vibrateLong();

    console.log('游戏结束，得分:', this.state.score);
  }

  /**
   * 暂停游戏
   */
  pauseGame() {
    this.state.isPaused = true;
  }

  /**
   * 继续游戏
   */
  resumeGame() {
    this.state.isPaused = false;
  }

  /**
   * 更新游戏逻辑
   */
  update(deltaTime) {
    if (!this.state.isPlaying || this.state.isPaused) {
      return;
    }

    // 更新玩家
    this.updatePlayer();

    // 更新敌人
    this.updateEnemies();

    // 更新道具
    this.updateItems();

    // 更新粒子
    this.updateParticles();

    // 碰撞检测
    this.checkCollisions();

    // 生成新对象
    this.spawnObjects();
  }

  /**
   * 更新玩家
   */
  updatePlayer() {
    if (!this.player) return;

    // 平滑移动到目标位置
    const dx = this.player.targetX - this.player.x;
    this.player.x += dx * 0.1;

    // 边界检测
    if (this.player.x < this.player.width / 2) {
      this.player.x = this.player.width / 2;
    }
    if (this.player.x > this.canvas.width - this.player.width / 2) {
      this.player.x = this.canvas.width - this.player.width / 2;
    }
  }

  /**
   * 更新敌人
   */
  updateEnemies() {
    for (let i = this.enemies.length - 1; i >= 0; i--) {
      const enemy = this.enemies[i];
      enemy.y += enemy.speed;

      // 移除超出屏幕的敌人
      if (enemy.y > this.canvas.height + 50) {
        this.enemies.splice(i, 1);
      }
    }
  }

  /**
   * 更新道具
   */
  updateItems() {
    for (let i = this.items.length - 1; i >= 0; i--) {
      const item = this.items[i];
      item.y += item.speed;

      // 移除超出屏幕的道具
      if (item.y > this.canvas.height + 50) {
        this.items.splice(i, 1);
      }
    }
  }

  /**
   * 更新粒子效果
   */
  updateParticles() {
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];
      p.x += p.vx;
      p.y += p.vy;
      p.life -= 0.02;

      if (p.life <= 0) {
        this.particles.splice(i, 1);
      }
    }
  }

  /**
   * 碰撞检测
   */
  checkCollisions() {
    if (!this.player) return;

    // 检测与敌人的碰撞
    for (let i = this.enemies.length - 1; i >= 0; i--) {
      const enemy = this.enemies[i];
      if (this.isColliding(this.player, enemy)) {
        // 碰撞处理
        this.enemies.splice(i, 1);
        this.state.lives--;

        // 创建粒子效果
        this.createParticles(enemy.x, enemy.y, '#FF5722');

        // 震动
        this.adapter.vibrateShort();

        if (this.state.lives <= 0) {
          this.gameOver();
        }
      }
    }

    // 检测与道具的碰撞
    for (let i = this.items.length - 1; i >= 0; i--) {
      const item = this.items[i];
      if (this.isColliding(this.player, item)) {
        this.items.splice(i, 1);
        this.state.score += item.value;

        // 创建粒子效果
        this.createParticles(item.x, item.y, '#FFD700');

        // 震动
        this.adapter.vibrateShort();
      }
    }
  }

  /**
   * 碰撞检测辅助函数
   */
  isColliding(a, b) {
    const dx = a.x - b.x;
    const dy = a.y - b.y;
    const distance = Math.sqrt(dx * dx + dy * dy);
    const radiusA = a.width ? a.width / 2 : a.radius;
    const radiusB = b.width ? b.width / 2 : b.radius;
    return distance < radiusA + radiusB;
  }

  /**
   * 创建粒子效果
   */
  createParticles(x, y, color) {
    for (let i = 0; i < 10; i++) {
      this.particles.push({
        x: x,
        y: y,
        vx: (Math.random() - 0.5) * 10,
        vy: (Math.random() - 0.5) * 10,
        color: color,
        life: 1
      });
    }
  }

  /**
   * 生成游戏对象
   */
  spawnObjects() {
    // 随机生成敌人
    if (Math.random() < 0.02) {
      this.enemies.push({
        x: Math.random() * (this.canvas.width - 40) + 20,
        y: -30,
        radius: 20,
        speed: 2 + this.state.level * 0.5,
        color: '#FF5722'
      });
    }

    // 随机生成道具
    if (Math.random() < 0.01) {
      this.items.push({
        x: Math.random() * (this.canvas.width - 30) + 15,
        y: -20,
        radius: 15,
        speed: 1.5,
        value: 10,
        color: '#FFD700'
      });
    }
  }

  /**
   * 渲染游戏
   */
  render() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    // 清空画布
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, width, height);

    if (!this.state.isPlaying) {
      this.renderMenu();
      return;
    }

    // 渲染游戏对象
    this.renderPlayer();
    this.renderEnemies();
    this.renderItems();
    this.renderParticles();
    this.renderUI();
  }

  /**
   * 渲染菜单
   */
  renderMenu() {
    const ctx = this.ctx;
    const width = this.canvas.width;
    const height = this.canvas.height;

    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 36px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('躲避游戏', width / 2, height / 3);

    ctx.font = '20px Arial';
    ctx.fillText('点击开始', width / 2, height / 2);

    if (this.state.highScore > 0) {
      ctx.fillStyle = '#FFD700';
      ctx.fillText(`最高分: ${this.state.highScore}`, width / 2, height * 2 / 3);
    }
  }

  /**
   * 渲染玩家
   */
  renderPlayer() {
    if (!this.player) return;

    const ctx = this.ctx;
    ctx.fillStyle = this.player.color;
    ctx.beginPath();
    ctx.arc(this.player.x, this.player.y, this.player.width / 2, 0, Math.PI * 2);
    ctx.fill();
  }

  /**
   * 渲染敌人
   */
  renderEnemies() {
    const ctx = this.ctx;
    for (const enemy of this.enemies) {
      ctx.fillStyle = enemy.color;
      ctx.beginPath();
      ctx.arc(enemy.x, enemy.y, enemy.radius, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  /**
   * 渲染道具
   */
  renderItems() {
    const ctx = this.ctx;
    for (const item of this.items) {
      ctx.fillStyle = item.color;
      ctx.beginPath();
      ctx.arc(item.x, item.y, item.radius, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  /**
   * 渲染粒子
   */
  renderParticles() {
    const ctx = this.ctx;
    for (const p of this.particles) {
      ctx.fillStyle = p.color;
      ctx.globalAlpha = p.life;
      ctx.beginPath();
      ctx.arc(p.x, p.y, 5, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1;
  }

  /**
   * 渲染 UI
   */
  renderUI() {
    const ctx = this.ctx;

    // 分数
    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 24px Arial';
    ctx.textAlign = 'left';
    ctx.fillText(`分数: ${this.state.score}`, 20, 40);

    // 生命值
    ctx.fillText(`生命: ${this.state.lives}`, 20, 70);
  }

  /**
   * 游戏主循环
   */
  gameLoop(currentTime = 0) {
    // 计算时间差
    this.deltaTime = currentTime - this.lastTime;
    this.lastTime = currentTime;

    // 更新
    this.update(this.deltaTime);

    // 渲染
    this.render();

    // 下一帧
    if (this.adapter.isDouyin) {
      requestAnimationFrame((time) => this.gameLoop(time));
    } else {
      requestAnimationFrame((time) => this.gameLoop(time));
    }
  }

  /**
   * 启动游戏
   */
  start() {
    this.init();
    this.gameLoop();
  }

  /**
   * 保存进度
   */
  saveProgress() {
    this.adapter.setStorage('highScore', this.state.highScore.toString());
    this.adapter.setStorage('level', this.state.level.toString());
  }

  /**
   * 加载进度
   */
  loadProgress() {
    const highScore = this.adapter.getStorage('highScore');
    const level = this.adapter.getStorage('level');

    if (highScore) {
      this.state.highScore = parseInt(highScore);
    }
    if (level) {
      this.state.level = parseInt(level);
    }
  }
}

// 导出
if (typeof module !== 'undefined') {
  module.exports = GameCore;
}
