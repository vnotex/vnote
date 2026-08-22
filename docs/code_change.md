# Code Change Log

## 2026-08-21 版本号统一为 4.5.0.1（区分定制版）

### 背景
fork 分支 `v4.5.0_zyx` 上的 Windows CI 报错：
`Extracted dir 'Vnote-4.5.0.1-win64' not found`。

根因：`CMakeLists.txt` 声明 `VERSION 4.5.0`，CPack 产物为
`VNote-4.5.0-win64`；而 CI 的 `VNOTE_VER: 4.5.0.1` 使 Verify 步骤去找
`VNote-4.5.0.1-win64`，目录名对不上。

### 变更
- 新增 `.github/workflows/ci-wisondows.yml`：独立 Windows CI 工作流
  （Qt6/Win64/MSVC2022），修复版本号漂移、仅监听 `v4.5.0_zyx` 分支、
  支持 workflow_dispatch、去掉 fork 上不可用的 Release/签名步骤。
- `CMakeLists.txt`：`project(VNote VERSION 4.5.0)` → `VERSION 4.5.0.1`
- `.github/workflows/ci-wisondows.yml`：`VNOTE_VER: 4.5.0` → `4.5.0.1`

### 一致性要求（SSOT）
`CMakeLists.txt` 的 `VERSION` 与 CI 的 `VNOTE_VER` 必须保持相等，
否则 CPack 产物目录名与 Verify 步骤查找名不一致导致 CI 失败
（官方 `update_version.py` 也是同时更新这两处）。
