/**
 * 广告管理器 - 预留广告接口
 * 在浏览器环境中模拟，在抖音环境中真实调用
 */

class AdManager {
  constructor(adapter) {
    this.adapter = adapter;
    this.isDouyin = typeof tt !== 'undefined';

    // 广告实例
    this.rewardedVideoAd = null;
    this.interstitialAd = null;
    this.bannerAd = null;

    // 广告配置
    this.config = {
      rewardedVideoId: 'your-rewarded-video-ad-id', // 激励视频广告 ID
      interstitialId: 'your-interstitial-ad-id',    // 插屏广告 ID
      bannerId: 'your-banner-ad-id'                 // Banner 广告 ID
    };

    // 初始化
    this.init();
  }

  /**
   * 初始化广告
   */
  init() {
    if (!this.isDouyin) {
      console.log('广告管理器初始化（浏览器环境模拟）');
      return;
    }

    try {
      // 创建激励视频广告
      if (this.config.rewardedVideoId !== 'your-rewarded-video-ad-id') {
        this.rewardedVideoAd = tt.createRewardedVideoAd({
          adUnitId: this.config.rewardedVideoId
        });

        this.rewardedVideoAd.onError((err) => {
          console.error('激励视频广告错误:', err);
        });
      }

      // 创建插屏广告
      if (this.config.interstitialId !== 'your-interstitial-ad-id') {
        this.interstitialAd = tt.createInterstitialAd({
          adUnitId: this.config.interstitialId
        });

        this.interstitialAd.onError((err) => {
          console.error('插屏广告错误:', err);
        });
      }

      console.log('广告管理器初始化成功');
    } catch (err) {
      console.error('广告初始化失败:', err);
    }
  }

  /**
   * 设置广告 ID
   */
  setAdIds(config) {
    this.config = { ...this.config, ...config };
    this.init(); // 重新初始化
  }

  /**
   * 显示激励视频广告
   * @param {Function} onRewarded - 用户完成观看后的回调
   * @param {Function} onError - 广告失败的回调
   */
  showRewardedVideo(onRewarded, onError) {
    if (!this.isDouyin) {
      // 浏览器环境模拟
      console.log('显示激励视频广告（模拟）');
      const confirmed = confirm('观看广告获得奖励？（模拟）');
      if (confirmed) {
        setTimeout(() => {
          onRewarded && onRewarded();
        }, 1000);
      } else {
        onError && onError({ errMsg: '用户取消' });
      }
      return;
    }

    if (!this.rewardedVideoAd) {
      console.error('激励视频广告未初始化');
      onError && onError({ errMsg: '广告未初始化' });
      return;
    }

    // 监听广告关闭
    this.rewardedVideoAd.onClose((res) => {
      if (res && res.isEnded) {
        // 用户完整观看了广告
        onRewarded && onRewarded();
      } else {
        // 用户未完整观看
        onError && onError({ errMsg: '用户未完整观看' });
      }
    });

    // 显示广告
    this.rewardedVideoAd.show().catch(err => {
      console.error('激励视频广告显示失败:', err);
      onError && onError(err);

      // 广告未准备好，可以预加载
      this.rewardedVideoAd.load().then(() => {
        console.log('激励视频广告预加载成功');
      });
    });
  }

  /**
   * 显示插屏广告
   * @param {Function} onClose - 广告关闭回调
   * @param {Function} onError - 广告失败回调
   */
  showInterstitial(onClose, onError) {
    if (!this.isDouyin) {
      // 浏览器环境模拟
      console.log('显示插屏广告（模拟）');
      setTimeout(() => {
        alert('插屏广告（模拟）');
        onClose && onClose();
      }, 500);
      return;
    }

    if (!this.interstitialAd) {
      console.error('插屏广告未初始化');
      onError && onError({ errMsg: '广告未初始化' });
      return;
    }

    // 监听广告关闭
    this.interstitialAd.onClose(() => {
      onClose && onClose();
    });

    // 显示广告
    this.interstitialAd.show().catch(err => {
      console.error('插屏广告显示失败:', err);
      onError && onError(err);

      // 预加载
      this.interstitialAd.load().then(() => {
        console.log('插屏广告预加载成功');
      });
    });
  }

  /**
   * 预加载广告
   */
  preloadAds() {
    if (!this.isDouyin) {
      console.log('预加载广告（模拟）');
      return;
    }

    if (this.rewardedVideoAd) {
      this.rewardedVideoAd.load().then(() => {
        console.log('激励视频广告预加载成功');
      }).catch(err => {
        console.error('激励视频广告预加载失败:', err);
      });
    }

    if (this.interstitialAd) {
      this.interstitialAd.load().then(() => {
        console.log('插屏广告预加载成功');
      }).catch(err => {
        console.error('插屏广告预加载失败:', err);
      });
    }
  }

  /**
   * 销毁广告
   */
  destroy() {
    if (this.rewardedVideoAd) {
      this.rewardedVideoAd.destroy();
    }
    if (this.bannerAd) {
      this.bannerAd.destroy();
    }
  }
}

// 导出
if (typeof module !== 'undefined') {
  module.exports = AdManager;
}
