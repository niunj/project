/**
 * 游戏适配器 - 桥接浏览器环境和抖音小游戏环境
 * 在浏览器中使用标准 Canvas API，在抖音中使用 tt API
 */

class GameAdapter {
  constructor() {
    this.isDouyin = typeof tt !== 'undefined';
    this.canvas = null;
    this.ctx = null;
  }

  /**
   * 初始化画布
   */
  initCanvas() {
    if (this.isDouyin) {
      // 抖音小游戏环境
      this.canvas = tt.createCanvas();
      this.ctx = this.canvas.getContext('2d');
    } else {
      // 浏览器环境
      this.canvas = document.createElement('canvas');
      this.canvas.width = 375;
      this.canvas.height = 667;
      this.canvas.style.border = '1px solid #000';
      document.body.appendChild(this.canvas);
      this.ctx = this.canvas.getContext('2d');
    }

    return { canvas: this.canvas, ctx: this.ctx };
  }

  /**
   * 绑定触摸事件
   */
  onTouchStart(callback) {
    if (this.isDouyin) {
      tt.onTouchStart(callback);
    } else {
      this.canvas.addEventListener('touchstart', (e) => {
        e.preventDefault();
        const touch = e.touches[0];
        const rect = this.canvas.getBoundingClientRect();
        callback({
          touches: [{
            clientX: touch.clientX - rect.left,
            clientY: touch.clientY - rect.top
          }]
        });
      });

      // 同时支持鼠标点击（方便浏览器调试）
      this.canvas.addEventListener('mousedown', (e) => {
        const rect = this.canvas.getBoundingClientRect();
        callback({
          touches: [{
            clientX: e.clientX - rect.left,
            clientY: e.clientY - rect.top
          }]
        });
      });
    }
  }

  onTouchMove(callback) {
    if (this.isDouyin) {
      tt.onTouchMove(callback);
    } else {
      this.canvas.addEventListener('touchmove', (e) => {
        e.preventDefault();
        const touch = e.touches[0];
        const rect = this.canvas.getBoundingClientRect();
        callback({
          touches: [{
            clientX: touch.clientX - rect.left,
            clientY: touch.clientY - rect.top
          }]
        });
      });

      this.canvas.addEventListener('mousemove', (e) => {
        const rect = this.canvas.getBoundingClientRect();
        callback({
          touches: [{
            clientX: e.clientX - rect.left,
            clientY: e.clientY - rect.top
          }]
        });
      });
    }
  }

  onTouchEnd(callback) {
    if (this.isDouyin) {
      tt.onTouchEnd(callback);
    } else {
      this.canvas.addEventListener('touchend', callback);
      this.canvas.addEventListener('mouseup', callback);
    }
  }

  /**
   * 本地存储
   */
  setStorage(key, value) {
    if (this.isDouyin) {
      tt.setStorageSync(key, value);
    } else {
      localStorage.setItem(key, value);
    }
  }

  getStorage(key) {
    if (this.isDouyin) {
      return tt.getStorageSync(key);
    } else {
      return localStorage.getItem(key);
    }
  }

  /**
   * 分享功能
   */
  share(options) {
    if (this.isDouyin) {
      tt.shareAppMessage(options);
    } else {
      console.log('分享功能（浏览器环境模拟）:', options);
      alert(`分享：${options.title}`);
    }
  }

  /**
   * 震动
   */
  vibrateShort() {
    if (this.isDouyin) {
      tt.vibrateShort();
    } else {
      console.log('短震动（浏览器环境模拟）');
    }
  }

  vibrateLong() {
    if (this.isDouyin) {
      tt.vibrateLong();
    } else {
      console.log('长震动（浏览器环境模拟）');
    }
  }

  /**
   * 显示提示
   */
  showToast(options) {
    if (this.isDouyin) {
      tt.showToast(options);
    } else {
      console.log('提示:', options.title);
      alert(options.title);
    }
  }

  /**
   * 音频播放
   */
  createAudio(src) {
    if (this.isDouyin) {
      const audio = tt.createInnerAudioContext();
      audio.src = src;
      return audio;
    } else {
      const audio = new Audio(src);
      return {
        play: () => audio.play(),
        pause: () => audio.pause(),
        stop: () => {
          audio.pause();
          audio.currentTime = 0;
        },
        onEnded: (callback) => audio.onended = callback,
        loop: false
      };
    }
  }
}

// 导出适配器
if (typeof module !== 'undefined') {
  module.exports = GameAdapter;
}
