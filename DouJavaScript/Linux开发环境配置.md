# Linux 环境下抖音小游戏开发环境配置指南

## 重要说明

**抖音开发者工具目前不支持 Linux 系统**，官方只提供以下版本：
- Windows 版本
- Mac 版本（支持 Intel 和 Apple Silicon）

---

## 替代方案

### 方案一：使用 Wine 运行 Windows 版本（推荐）

#### 1. 安装 Wine

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install wine64 wine32

# Fedora
sudo dnf install wine

# Arch Linux
sudo pacman -S wine
```

#### 2. 下载抖音开发者工具（Windows 版本）

```bash
cd /home/mte/FOLD/DouJavaScript

# 创建工具目录
mkdir -p tools
cd tools

# 下载最新版本（4.2.1）
wget -O douyin-ide-4.2.1.exe "https://sf1-cdn-tos.douyinstatic.com/obj/microapp/frontend/ide/package/obj/developer/ide/11636095/win32/%E6%8A%96%E9%9F%B3%E5%BC%80%E5%8F%91%E8%80%85%E5%B7%A5%E5%85%B7-4.2.1.exe"
```

#### 3. 使用 Wine 运行

```bash
# 安装
wine douyin-ide-4.2.1.exe

# 运行（安装后）
wine ~/.wine/drive_c/Program\ Files/抖音开发者工具/抖音开发者工具.exe
```

**注意事项**：
- Wine 兼容性可能不完美，某些功能可能无法使用
- 建议使用 Wine 5.0 或更高版本
- 可能需要安装额外的 Windows 依赖库

---

### 方案二：使用虚拟机（稳定可靠）

#### 1. 安装 VirtualBox

```bash
# Ubuntu/Debian
sudo apt install virtualbox

# Fedora
sudo dnf install virtualbox

# Arch Linux
sudo pacman -S virtualbox
```

#### 2. 创建 Windows 虚拟机

1. 下载 Windows 10/11 ISO 镜像
2. 在 VirtualBox 中创建新的虚拟机
3. 分配资源：
   - 内存：至少 4GB
   - 硬盘：至少 40GB
   - 显卡：启用 3D 加速

#### 3. 在虚拟机中安装抖音开发者工具

- 在 Windows 虚拟机中打开浏览器
- 访问抖音开放平台下载页面
- 下载并安装 Windows 版本

**优点**：
- 完全兼容，功能完整
- 性能稳定

**缺点**：
- 需要较多系统资源
- 需要安装 Windows 系统

---

### 方案三：使用 Docker + Wine（高级用户）

#### 1. 创建 Dockerfile

```dockerfile
FROM ubuntu:20.04

# 安装 Wine 和依赖
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    wine64 \
    wine32 \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 下载抖音开发者工具
RUN wget -O douyin-ide.exe "https://sf1-cdn-tos.douyinstatic.com/obj/microapp/frontend/ide/package/obj/developer/ide/11636095/win32/%E6%8A%96%E9%9F%B3%E5%BC%80%E5%8F%91%E8%80%85%E5%B7%A5%E5%85%B7-4.2.1.exe"

# 设置环境变量
ENV WINEDEBUG=-all
ENV DISPLAY=:0

CMD ["wine", "douyin-ide.exe"]
```

#### 2. 构建和运行

```bash
# 构建镜像
docker build -t douyin-ide .

# 运行容器（需要 X11 转发）
docker run -it \
    --env="DISPLAY" \
    --env="QT_X11_NO_MITSHM=1" \
    --volume="/tmp/.X11-unix:/tmp/.X11-unix:rw" \
    douyin-ide
```

---

### 方案四：远程开发（云端方案）

#### 1. 使用云服务器

- 购买一台 Windows 云服务器（阿里云、腾讯云等）
- 通过远程桌面连接
- 在云服务器上安装抖音开发者工具

#### 2. 使用 GitHub Codespaces

- 创建 GitHub Codespaces 环境
- 选择 Windows 镜像
- 在 Codespaces 中安装抖音开发者工具

---

## 开发流程建议

### 在 Linux 下开发，在 Windows/Mac 下调试

#### 1. 在 Linux 下编写代码

```bash
# 创建项目目录
cd /home/mte/FOLD/DouJavaScript
mkdir my-douyin-game
cd my-douyin-game

# 创建基本文件结构
touch game.js game.json project.config.json
mkdir -p images audio
```

#### 2. 使用 VS Code 开发

```bash
# 安装 VS Code 扩展
code --install-extension dbaeumer.vscode-eslint
code --install-extension esbenp.prettier-vscode
```

#### 3. 同步到 Windows/Mac

**方法 A：使用 Git**
```bash
# 初始化 Git 仓库
git init
git add .
git commit -m "Initial commit"
git remote add origin <your-repo-url>
git push -u origin master

# 在 Windows/Mac 上
git clone <your-repo-url>
```

**方法 B：使用 Samba 共享**
```bash
# 安装 Samba
sudo apt install samba

# 配置共享
sudo nano /etc/samba/smb.conf
```

添加以下内容：
```
[DouJavaScript]
path = /home/mte/FOLD/DouJavaScript
browseable = yes
read only = no
```

---

## 辅助开发工具

### 1. 代码编辑器配置

#### VS Code 配置（推荐）

创建 `.vscode/settings.json`：
```json
{
    "editor.formatOnSave": true,
    "editor.tabSize": 2,
    "files.associations": {
        "game.js": "javascript",
        "*.json": "json"
    },
    "emmet.includeLanguages": {
        "javascript": "javascriptreact"
    }
}
```

创建 `.vscode/launch.json`（调试配置）：
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "node",
            "request": "launch",
            "name": "Launch Program",
            "program": "${workspaceFolder}/game.js"
        }
    ]
}
```

### 2. 代码检查工具

#### ESLint 配置

安装：
```bash
npm init -y
npm install --save-dev eslint
```

创建 `.eslintrc.json`：
```json
{
    "env": {
        "browser": true,
        "es6": true
    },
    "extends": "eslint:recommended",
    "globals": {
        "tt": "readonly",
        "GameGlobal": "readonly"
    },
    "parserOptions": {
        "ecmaVersion": 2018,
        "sourceType": "module"
    }
}
```

### 3. 代码格式化

#### Prettier 配置

安装：
```bash
npm install --save-dev prettier
```

创建 `.prettierrc`：
```json
{
    "semi": true,
    "singleQuote": true,
    "tabWidth": 2,
    "trailingComma": "es5"
}
```

---

## 项目模板

### 创建基础项目结构

```bash
cd /home/mte/FOLD/DouJavaScript

# 创建项目
mkdir -p my-douyin-game/{src/{core,utils,scenes},images,audio}
cd my-douyin-game

# 创建文件
touch game.js game.json project.config.json
touch src/core/{game-loop,renderer,input}.js
touch src/utils/{collision,audio-manager}.js
touch src/scenes/{main-menu,game-play,game-over}.js
```

### 基础代码模板

#### game.js
```javascript
// 游戏主入口
class Game {
  constructor() {
    this.canvas = tt.createCanvas();
    this.ctx = this.canvas.getContext('2d');
    this.init();
  }

  init() {
    // 初始化游戏
    console.log('游戏初始化');
    this.gameLoop();
  }

  update() {
    // 更新游戏逻辑
  }

  render() {
    // 渲染画面
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    this.ctx.fillStyle = '#000';
    this.ctx.fillText('Hello Douyin!', 10, 50);
  }

  gameLoop() {
    this.update();
    this.render();
    requestAnimationFrame(() => this.gameLoop());
  }
}

// 启动游戏
new Game();
```

#### game.json
```json
{
  "deviceOrientation": "portrait",
  "showStatusBar": false,
  "networkTimeout": {
    "request": 5000,
    "connectSocket": 5000,
    "uploadFile": 5000,
    "downloadFile": 5000
  }
}
```

#### project.config.json
```json
{
  "miniprogramRoot": "./",
  "compileType": "game",
  "projectname": "my-douyin-game",
  "description": "我的抖音小游戏",
  "appid": "touristappid",
  "setting": {
    "urlCheck": true,
    "es6": true,
    "enhance": true,
    "postcss": true,
    "minified": true,
    "newFeature": false
  },
  "simulatorType": "wechat",
  "simulatorPluginLibVersion": {},
  "condition": {}
}
```

---

## 测试和预览

### 本地测试服务器

创建一个简单的 HTTP 服务器用于本地测试：

```bash
# 安装 http-server
npm install -g http-server

# 启动服务器
cd my-douyin-game
http-server -p 8080 --cors
```

然后在浏览器中访问 `http://localhost:8080`

**注意**：这只是用于测试基本逻辑，抖音 API（如 `tt.createCanvas()`）需要在真实环境中运行。

---

## 调试技巧

### 1. 使用 console.log

在代码中添加日志：
```javascript
console.log('调试信息:', variable);
```

### 2. 使用 Chrome DevTools

如果使用 Wine 或虚拟机，可以通过远程调试连接 Chrome DevTools。

### 3. 模拟抖音 API

创建一个模拟文件用于本地测试：

```javascript
// mock-tt.js
if (typeof tt === 'undefined') {
  global.tt = {
    createCanvas: () => {
      const canvas = document.createElement('canvas');
      canvas.width = 375;
      canvas.height = 667;
      document.body.appendChild(canvas);
      return canvas;
    },
    onTouchStart: (callback) => {
      document.addEventListener('touchstart', callback);
    },
    // 添加其他需要的 API
  };
}
```

---

## 推荐开发流程

### 最佳实践

1. **在 Linux 下编写代码**
   - 使用 VS Code 或其他编辑器
   - 使用 Git 进行版本控制
   - 使用 ESLint/Prettier 保证代码质量

2. **在 Windows/Mac 下测试**
   - 使用虚拟机或 Wine
   - 在抖音开发者工具中打开项目
   - 进行真机调试

3. **持续集成**
   - 使用 GitHub Actions 自动检查代码
   - 自动运行 ESLint 和单元测试

4. **部署上线**
   - 在抖音开发者工具中上传代码
   - 提交审核
   - 发布上线

---

## 常见问题

### Q1: Wine 运行时出现错误怎么办？

**A**:
- 尝试安装 `winetricks` 并安装常用依赖
- 使用 `WINEPREFIX` 创建干净的 Wine 环境
- 查看 Wine AppDB 寻找解决方案

### Q2: 虚拟机性能太慢怎么办？

**A**:
- 启用 3D 加速
- 增加虚拟机内存和 CPU
- 使用轻量级 Windows 版本（如 Windows 10 LTSC）

### Q3: 能否完全在 Linux 下开发？

**A**:
- 可以编写代码，但无法使用抖音开发者工具的完整功能
- 建议至少有一台 Windows/Mac 设备用于测试和调试

---

## 总结

虽然抖音开发者工具不支持 Linux，但通过以下方式可以正常开发：

1. **推荐方案**：使用虚拟机（VirtualBox + Windows）
2. **轻量方案**：使用 Wine 运行 Windows 版本
3. **混合方案**：Linux 编写代码 + Windows/Mac 调试

选择最适合你的方案，开始抖音小游戏开发之旅！
