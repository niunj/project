/**
 * 配置文件 - 抖音小游戏配置
 * 在这里修改你的 AppID、广告 ID 等信息
 */

const CONFIG = {
  // ==================== 基础配置 ====================

  // 小游戏 AppID（从抖音开放平台获取）
  // 替换 'touristappid' 为你的真实 AppID
  appId: 'touristappid',

  // 游戏名称
  gameName: '我的抖音小游戏',

  // 游戏版本
  version: '1.0.0',

  // ==================== 广告配置 ====================

  // 广告开关（开发时可关闭）
  adEnabled: true,

  // 激励视频广告 ID
  // 替换为你的真实广告 ID
  rewardedVideoAdId: 'adunit-xxxxxxxx',

  // 插屏广告 ID
  // 替换为你的真实广告 ID
  interstitialAdId: 'adunit-xxxxxxxx',

  // Banner 广告 ID（可选）
  bannerAdId: 'adunit-xxxxxxxx',

  // ==================== 广告策略配置 ====================

  // 激励视频广告策略
  rewardedVideoStrategy: {
    // 游戏失败时是否显示复活选项
    showReviveOnGameOver: true,

    // 复活需要的最低分数
    minScoreForRevive: 50,

    // 观看广告获得的奖励
    reward: {
      lives: 1,      // 增加生命值
      score: 100     // 增加分数
    }
  },

  // 插屏广告策略
  interstitialStrategy: {
    // 游戏结束时是否显示插屏广告
    showOnGameOver: true,

    // 关卡切换时是否显示插屏广告
    showOnLevelChange: false,

    // 展示间隔（秒）
    minInterval: 60
  },

  // ==================== 分享配置 ====================

  // 分享开关
  shareEnabled: true,

  // 分享配置
  shareConfig: {
    // 默认分享标题
    title: '快来玩这个有趣的游戏！',

    // 分享图片路径（需要提供真实图片）
    imageUrl: '',

    // 分享路径
    path: '/pages/index/index'
  },

  // ==================== 游戏配置 ====================

  // 游戏难度配置
  difficulty: {
    // 初始生命值
    initialLives: 3,

    // 敌人生成速度
    enemySpawnRate: 0.02,

    // 道具生成速度
    itemSpawnRate: 0.01,

    // 敌人速度
    enemySpeed: 2,

    // 道具价值
    itemValue: 10
  },

  // ==================== 音效配置 ====================

  // 音效开关
  soundEnabled: true,

  // 音效文件路径
  sounds: {
    bgMusic: 'audio/background.mp3',
    click: 'audio/click.mp3',
    score: 'audio/score.mp3',
    gameOver: 'audio/gameover.mp3'
  },

  // ==================== 数据上报配置 ====================

  // 数据上报开关
  analyticsEnabled: true,

  // 上报间隔（秒）
  analyticsInterval: 60
};

// ==================== 辅助函数 ====================

/**
 * 获取配置
 */
function getConfig(key) {
  return key ? CONFIG[key] : CONFIG;
}

/**
 * 设置配置
 */
function setConfig(key, value) {
  if (CONFIG.hasOwnProperty(key)) {
    CONFIG[key] = value;
    console.log(`配置已更新: ${key} =`, value);
  } else {
    console.warn(`未知的配置项: ${key}`);
  }
}

/**
 * 验证配置
 */
function validateConfig() {
  const errors = [];

  // 检查 AppID
  if (CONFIG.appId === 'touristappid') {
    errors.push('AppID 未配置，请替换为真实 AppID');
  }

  // 检查广告 ID
  if (CONFIG.adEnabled) {
    if (CONFIG.rewardedVideoAdId === 'adunit-xxxxxxxx') {
      errors.push('激励视频广告 ID 未配置');
    }
    if (CONFIG.interstitialAdId === 'adunit-xxxxxxxx') {
      errors.push('插屏广告 ID 未配置');
    }
  }

  // 检查分享图片
  if (CONFIG.shareEnabled && !CONFIG.shareConfig.imageUrl) {
    errors.push('分享图片未配置');
  }

  if (errors.length > 0) {
    console.warn('配置验证失败：');
    errors.forEach((error, index) => {
      console.warn(`${index + 1}. ${error}`);
    });
    return false;
  }

  console.log('✅ 配置验证通过');
  return true;
}

// ==================== 导出 ====================

// 如果在 Node.js 环境
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    CONFIG,
    getConfig,
    setConfig,
    validateConfig
  };
}

// 如果在浏览器或抖音小游戏环境
if (typeof window !== 'undefined') {
  window.GameConfig = CONFIG;
  window.getConfig = getConfig;
  window.setConfig = setConfig;
  window.validateConfig = validateConfig;
}

// 如果在抖音小游戏环境
if (typeof GameGlobal !== 'undefined') {
  GameGlobal.GameConfig = CONFIG;
  GameGlobal.getConfig = getConfig;
  GameGlobal.setConfig = setConfig;
  GameGlobal.validateConfig = validateConfig;
}
