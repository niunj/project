'use strict';

const DUEL_TIERS = [
  { key: 'light', label: '普攻', cost: 0, damage: 8, windup: 0.09 },
  { key: 'small', label: '小招', cost: 17, damage: 13, windup: 0.19 },
  { key: 'medium', label: '中招', cost: 34, damage: 22, windup: 0.30 },
  { key: 'large', label: '大招', cost: 61, damage: 34, windup: 0.45 },
  { key: 'super', label: '超大招', cost: 92, damage: 47, windup: 0.61 }
];

// Motifs define geometry and particles separately from a move's collision pattern.
const DUEL_HEROES = [
  ['岚', '疾风游击', '#6af2ed', '#285668', '#c9fff7', 'kite', 298, [
    ['掠拳', 'jab'], ['风刃', 'shot'], ['旋流步', 'dash'], ['断潮', 'beam'], ['千岚回廊', 'fan']]],
  ['烬', '爆炎压制', '#ff965c', '#6b3c38', '#ffe2a8', 'flame', 271, [
    ['灼肘', 'slam'], ['焰轮', 'return'], ['焦土环', 'burst'], ['赤域', 'trap'], ['烈城坠火', 'rain']]],
  ['霆', '电轨突袭', '#eed75f', '#615238', '#fffac5', 'bolt', 290, [
    ['闪掌', 'jab'], ['电钉', 'shot'], ['反相弧', 'return'], ['雷网', 'cross'], ['金雷贯轨', 'beam']]],
  ['霁', '寒区控制', '#89c9ff', '#3c4f82', '#d9f8ff', 'shard', 266, [
    ['冰肘', 'swipe'], ['凝霜', 'fan'], ['冰棱台', 'burst'], ['极冻井', 'vortex'], ['白昼冰原', 'trap']]],
  ['砾', '岩甲重击', '#dab084', '#594639', '#fff0c8', 'block', 245, [
    ['崩山掌', 'slam'], ['滚石', 'shot'], ['岩门', 'sweep'], ['断层', 'beam'], ['三峰倒悬', 'rain']]],
  ['萝', '藤蔓封路', '#8eeb8f', '#315f4b', '#e2ffaf', 'leaf', 276, [
    ['青藤刺', 'kick'], ['蔓鞭', 'sweep'], ['荆笼', 'trap'], ['绿潮井', 'vortex'], ['森罗开界', 'cross']]],
  ['弦', '声波牵制', '#eea9e8', '#6b396c', '#ffe5fa', 'wave', 282, [
    ['拨弦手', 'swipe'], ['和声波', 'fan'], ['回音壁', 'return'], ['断音线', 'beam'], ['八面回响', 'burst']]],
  ['泊', '水流绕袭', '#6ad4f5', '#286380', '#bff6ff', 'drop', 288, [
    ['穿浪掌', 'jab'], ['飞瀑滴', 'shot'], ['归流水', 'vortex'], ['涌浪踏', 'dash'], ['倒悬海幕', 'rain']]],
  ['曦', '日蚀狙击', '#ffce80', '#795344', '#ffffbb', 'sun', 268, [
    ['朝晖拳', 'slam'], ['日珥环', 'return'], ['破晓带', 'beam'], ['午时烈斑', 'burst'], ['九曜交辉', 'cross']]],
  ['朔', '月刃游走', '#c4b5ff', '#48416d', '#edf1ff', 'moon', 299, [
    ['新月掌', 'swipe'], ['残月轮', 'return'], ['月影切', 'dash'], ['半月幕', 'fan'], ['月落无声', 'vortex']]],
  ['烁', '棱镜散射', '#f5a9aa', '#62425f', '#fff1b8', 'prism', 280, [
    ['折光刺', 'jab'], ['分光矢', 'fan'], ['镜像格', 'cross'], ['虹折域', 'trap'], ['棱镜天光', 'beam']]],
  ['昼', '流云步法', '#bfeae3', '#316465', '#f4ffda', 'mist', 302, [
    ['云手', 'swipe'], ['流岫', 'dash'], ['重云', 'burst'], ['风压旋', 'vortex'], ['万里云涛', 'fan']]],
  ['漠', '沙幕围猎', '#ecb86f', '#654b36', '#ffeab3', 'dust', 263, [
    ['沙锤', 'slam'], ['沙刺', 'shot'], ['孤柱', 'trap'], ['沙葬', 'rain'], ['千沙沉井', 'vortex']]],
  ['枢', '机械轨道', '#9dbad0', '#3c5366', '#eefbff', 'gear', 262, [
    ['液压拳', 'slam'], ['飞齿', 'return'], ['拘束齿阵', 'trap'], ['交叉轨架', 'cross'], ['零号脉冲', 'beam']]],
  ['翎', '羽刃突进', '#adeb9f', '#4e7147', '#f5ffd4', 'feather', 306, [
    ['啄影', 'kick'], ['燕掠', 'dash'], ['百羽针', 'fan'], ['折翼扫', 'sweep'], ['天羽穿云', 'rain']]],
  ['绯', '丝线反手', '#ff9ab7', '#713d5a', '#fff2df', 'silk', 281, [
    ['牵丝手', 'jab'], ['朱丝', 'shot'], ['千结', 'cross'], ['缚影弧', 'return'], ['织命绯网', 'trap']]],
  ['墨', '影域伏击', '#bba3e2', '#413955', '#f7d8ff', 'ink', 274, [
    ['泼墨肘', 'swipe'], ['墨滴', 'burst'], ['黑潮卷', 'vortex'], ['砚影踏', 'dash'], ['墨幕压城', 'rain']]],
  ['岫', '星阵定点', '#a4aaff', '#484d79', '#fef1d3', 'star', 260, [
    ['落星踢', 'kick'], ['飞星', 'return'], ['双星落', 'trap'], ['交星阵', 'cross'], ['天穹星雨', 'rain']]],
  ['冥', '引力反打', '#c086ea', '#50416c', '#eed7ff', 'void', 255, [
    ['坠拳', 'slam'], ['暗核', 'shot'], ['引力门', 'vortex'], ['暗面横切', 'beam'], ['重力坍缩', 'burst']]],
  ['祈', '光环压阵', '#e4f4dc', '#526d65', '#fff9c6', 'halo', 277, [
    ['裁光掌', 'kick'], ['辉轮', 'return'], ['巡光带', 'sweep'], ['圣幕交界', 'cross'], ['七重天环', 'vortex']]]
].map(([name, role, color, dark, highlight, motif, speed, moves], id) => ({
  id, name, role, color, dark, highlight, motif, speed,
  moves: moves.map(([title, pattern], tier) => ({ ...DUEL_TIERS[tier], title, pattern, tier }))
}));

const DuelRoster = { heroes: DUEL_HEROES, tiers: DUEL_TIERS };
if (typeof module !== 'undefined') module.exports = DuelRoster;
