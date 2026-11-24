'use strict';

// 填入与 project.config.json 中 AppID 对应的激励视频广告位 ID。
const REWARDED_VIDEO_AD_UNIT_ID = '';
const SIDEBAR_REWARD_KEY = 'rift-runner.sidebar-reward-day.v1';
const SIDEBAR_SCENES = ['021036', '21036', '101036', '181036', '261036'];

function rewardDay() {
  return new Date(Date.now() + 8 * 60 * 60 * 1000).toISOString().slice(0, 10);
}

class RiftDouyinIaa {
  constructor(api, redraw, adUnitId = REWARDED_VIDEO_AD_UNIT_ID) {
    this.api = api;
    this.redraw = redraw;
    this.adUnitId = adUnitId.trim();
    this.video = null;
    this.pendingAd = null;
    this.sidebarSupported = false;
    this.sidebarVisitDay = '';
    this.sidebarNavigating = false;
    try {
      this.sidebarRewardDay = api.getStorageSync(SIDEBAR_REWARD_KEY) || '';
    } catch (error) {
      this.sidebarRewardDay = '';
    }
  }

  get hasRewardedAd() {
    return Boolean(this.adUnitId) && typeof this.api.createRewardedVideoAd === 'function';
  }

  finishAd(result) {
    const request = this.pendingAd;
    if (!request) return;
    this.pendingAd = null;
    request.complete(result);
    this.redraw();
  }

  playAd(complete) {
    if (!this.hasRewardedAd || this.pendingAd) return false;
    const tt = this.api;
    const request = { complete, shown: false, completed: false };
    this.pendingAd = request;
    try {
      if (!this.video) {
        const video = tt.createRewardedVideoAd({ adUnitId: this.adUnitId });
        video.onClose((result) => {
          const active = this.pendingAd;
          if (!active) return;
          if (!result || result.isEnded !== true) {
            this.finishAd('cancelled');
            return;
          }
          active.completed = true;
          if (active.shown) this.finishAd('completed');
        });
        video.onError(() => this.finishAd('unavailable'));
        this.video = video;
      }
      Promise.resolve(this.video.show()).then(() => {
        if (this.pendingAd !== request) return;
        request.shown = true;
        if (request.completed) this.finishAd('completed');
      }, () => {
        if (this.pendingAd === request) this.finishAd('unavailable');
      });
    } catch (error) {
      if (this.pendingAd === request) this.finishAd('unavailable');
    }
    return true;
  }

  checkSidebar() {
    const tt = this.api;
    if (typeof tt.checkScene !== 'function' || typeof tt.navigateToScene !== 'function') return;
    try {
      tt.checkScene({
        scene: 'sidebar',
        success: (result) => {
          this.sidebarSupported = Boolean(result && result.isExist === true);
          this.redraw();
        }
      });
    } catch (error) {
      // Keep the shortcut hidden if the host cannot check sidebar support.
    }
  }

  onShow(options) {
    if (!options) return;
    if (SIDEBAR_SCENES.includes(String(options.scene)) ||
      (options.launch_from === 'homepage' && options.location === 'sidebar_card')) {
      this.sidebarVisitDay = rewardDay();
      this.redraw();
    }
  }

  get sidebarRewardActive() {
    return this.sidebarRewardDay === rewardDay();
  }

  get sidebarStatus() {
    const visited = this.sidebarVisitDay === rewardDay();
    if (!this.sidebarSupported && !visited) return '';
    if (this.sidebarRewardActive) return 'claimed';
    return visited ? 'claim' : 'visit';
  }

  claimSidebarReward() {
    if (this.sidebarStatus !== 'claim') return;
    this.sidebarRewardDay = this.sidebarVisitDay;
    try {
      this.api.setStorageSync(SIDEBAR_REWARD_KEY, this.sidebarRewardDay);
    } catch (error) {
      // Keep the reward active for this session if storage is unavailable.
    }
    this.redraw();
  }

  openSidebar(onFailure) {
    if (!this.sidebarSupported || this.sidebarNavigating) return;
    const tt = this.api;
    this.sidebarNavigating = true;
    const fail = () => {
      this.sidebarNavigating = false;
      onFailure();
    };
    try {
      tt.navigateToScene({
        scene: 'sidebar',
        success: () => { this.sidebarNavigating = false; },
        fail
      });
    } catch (error) {
      fail();
    }
  }
}

module.exports = RiftDouyinIaa;
