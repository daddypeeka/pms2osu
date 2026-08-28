# pms2osu-v2 部署教程（GitHub + Actions 自动构建）

本项目已经做好以下准备：

- Git 仓库已初始化（默认分支 `main`），并已提交初始提交；
- 根目录有 `.gitignore`（忽略 `build/`、`dist/`、编译产物、`imgui.ini` 等）；
- 已提供 GitHub Actions 工作流 `.github/workflows/build.yml`，可在 Windows 上自动编译出 `pms2osu-v2.exe` 并发布。

---

## 1. 创建 GitHub 仓库并推送

1. 在 GitHub 网页上 **New repository**，例如命名为 `pms2osu-v2`。
2. **不要**勾选 “Add a README / .gitignore / license”（本地已有），保持空仓库即可。
3. 在本地打开终端，进入项目目录：

   ```bash
   cd D:\document\code\rmstZ\pms2osu-v2
   ```

4. 把本地仓库关联到远程并推送：

   ```bash
   git remote add origin https://github.com/<你的用户名>/pms2osu-v2.git
   git branch -M main
   git push -u origin main
   ```

推送成功后，GitHub Actions 会自动开始第一次构建。

---

## 2. 自动构建是怎么触发的

`.github/workflows/build.yml` 会在以下情况自动运行：

| 触发条件 | 说明 |
| --- | --- |
| `push` 到 `main` / `master` | 每次提交自动编译 |
| 推送 `v*` 标签（如 `v1.0.0`） | 编译并自动发布 GitHub Release |
| 创建 Pull Request | 合并前自动验证能否编译 |
| 手动运行 | 在 Actions 页面点 **Run workflow** |

构建环境：`windows-latest`，使用 MSYS2 的 MinGW-w64 (`gcc` + `make`)，
与你本地 w64devkit 工具链兼容，链接仓库自带的预编译 GLFW（`third_party/glfw/lib-mingw-w64`）。

---

## 3. 查看构建结果并下载程序

1. 打开仓库页面 → 顶部 **Actions** 标签。
2. 点击最新一次运行，进入详情。
3. 构建成功后，页面底部 **Artifacts** 会有一个 `pms2osu-v2-windows` 压缩包。
4. 点击下载解压，得到 `pms2osu-v2.exe`，双击即可运行（无需额外 DLL）。

---

## 4. 一键发布版本（打 Tag 自动出 Release）

每次想发布一个正式版本：

```bash
git tag v1.0.0
git push origin v1.0.0
```

推送 `v*` 标签后，工作流会自动：

- 编译出 `pms2osu-v2.exe`；
- 用 `softprops/action-gh-release` 在 GitHub 上创建 Release；
- 自动把 exe 附到 Release 里，并生成更新日志。

之后可以在仓库 **Releases** 页面看到版本和可下载的 exe，直接把链接分享给别人即可。

---

## 5. 本地手动构建（可选）

没有 GitHub 也可以本地编译，需要 w64devkit（MinGW-w64）：

```bash
build.bat
# 或
make
```

产物输出到 `dist/pms2osu-v2.exe`，自带中文/日文字体渲染（加载系统 CJK 字体），
不需要额外的 `glfw3.dll`。

---

## 6. 常见问题

**Q: Actions 构建失败？**
- 确认 `.github/workflows/build.yml` 里的 MSYS2 包名正确（`mingw-w64-x86_64-gcc`、`make`）。
- 确认 `third_party/` 下的 GLFW 预编译库、imgui、libogg、libvorbis 都已提交到仓库
  （本仓库已提交，`git ls-files third_party` 可核对）。

**Q: 想改默认分支名？**
- 在 GitHub 仓库 Settings → Branches 里把默认分支设为 `main` 即可，工作流同时监听 `main` 和 `master`。

**Q: 中文/日文文件夹显示乱码？**
- 代码已在 `main.cpp` 中加载系统 CJK 字体（微软雅黑 / MS Gothic 等），无需再配置。

**Q: 不想每次都自动构建 PR？**
- 删掉 `build.yml` 中 `pull_request` 段落即可。
