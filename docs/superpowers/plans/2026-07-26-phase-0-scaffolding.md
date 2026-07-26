# Phase 0: Scaffolding — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A CMake-built C++20 program that opens a black 3.3-core-profile OpenGL window, prints `GL_VERSION`/`GL_RENDERER`, and closes cleanly — building on Windows (local + CI) and Linux (CI).

**Architecture:** Single `main.cpp`, single-threaded (the thread split is Phase 3). GLFW fetched via CMake `FetchContent` pinned to tag 3.4. GLAD vendored as the single-header glad2 loader (`extern/glad/gl.h`) copied from GLFW's own `deps/` tree, so no Python/network tooling is needed at build time. A build-only GitHub Actions matrix (windows-latest, ubuntu-latest) satisfies the "two of three OSes" done-condition and pre-stages Phase 8c.

**Tech Stack:** C++20, OpenGL 3.3 core, GLFW 3.4, GLAD 2 (header-only), CMake ≥ 3.21, GitHub Actions.

## Context

The repo (`C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer`, remote `github.com/tiffany-mares/OpenGL-Renderer`) contains only `README.md` and `CLAUDE.md` — no code. This is Phase 0 of the 9-phase plan captured in `CLAUDE.md`: get a black window on screen with the exact context settings (`3.3 core`, `glfwSwapInterval(0)` immediately) that every later phase builds on. The user chose: detect/install the Windows toolchain during execution, and use GitHub Actions CI as the second platform.

## Global Constraints

- C++20 (`CMAKE_CXX_STANDARD 20`, required).
- OpenGL 3.3 **core** profile; `glfwSwapInterval(0)` called immediately after `glfwMakeContextCurrent`.
- GLFW for windowing/events only; GLAD for GL loading. No SDL, no SFML, no glm, no engine.
- GLFW pinned to `GIT_TAG 3.4` (fixed tag → `deps/glad/gl.h` layout is stable).
- All work happens in the git repo root: `C:\Users\tiffm\Desktop\OpenGL Renderer\OpenGL-Renderer` (note: parent folder has a space in its path — always quote paths).
- Commit after each task; push at the end.
- Phase 0 has no unit-testable logic (windowing + GPU init is inherently manual/integration); "test" per task = build/run with expected output. Unit tests start in Phase 2 (mat4).

---

### Task 0: Toolchain detection and setup

**Files:** none (environment only)

**Interfaces:**
- Produces: a working `cmake` + MSVC toolchain; the chosen CMake generator name used by every later build command.

- [ ] **Step 1: Detect existing tools**

```powershell
foreach ($t in 'cmake','ninja','git','gh') { $c = Get-Command $t -ErrorAction SilentlyContinue; if ($c) { "$t -> $($c.Source)" } else { "$t -> NOT FOUND" } }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) { & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object displayName, installationVersion, installationPath | Format-List } else { "No Visual Studio installer found" }
```

Expected: report of what exists. Decision table:
- MSVC (any VS 2019/2022 with C++ tools) + CMake present → use the Visual Studio generator (CMake default); done, skip Step 2.
- CMake missing → install: `winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements`, then reopen shell / refresh `PATH`.
- MSVC missing → Step 2.

- [ ] **Step 2 (only if no MSVC): Install VS 2022 Build Tools with C++ workload**

**Pause and confirm with the user first — this is a multi-GB install.**

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

- [ ] **Step 3: Verify**

```powershell
cmake --version
```

Expected: `cmake version 3.2x` or newer (≥ 3.21).

---

### Task 1: Repo hygiene — plan doc and .gitignore

**Files:**
- Create: `.gitignore`
- Create: `docs/superpowers/plans/2026-07-26-phase-0-scaffolding.md` (copy of this plan, per superpowers convention)

- [ ] **Step 1: Write `.gitignore`**

```gitignore
build/
out/
.vs/
.vscode/
.cache/
CMakeUserPresets.json
```

- [ ] **Step 2: Copy this plan file into the repo**

Copy this document to `docs/superpowers/plans/2026-07-26-phase-0-scaffolding.md`.

- [ ] **Step 3: Commit**

```powershell
git add .gitignore docs/
git commit -m "chore: add .gitignore and Phase 0 plan"
```

---

### Task 2: CMake project with GLFW via FetchContent

**Files:**
- Create: `CMakeLists.txt`

**Interfaces:**
- Produces: target `cube` linking `glfw`, include path `extern/` (used by Task 3's `glad/gl.h` and Task 4's `main.cpp`).

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.21)
project(cube LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(cube src/main.cpp)
target_include_directories(cube PRIVATE ${CMAKE_SOURCE_DIR}/extern)
target_link_libraries(cube PRIVATE glfw)
```

- [ ] **Step 2: Create a placeholder `src/main.cpp` so configure+build can be smoke-tested**

```cpp
int main() { return 0; }
```

- [ ] **Step 3: Configure and build (this also fetches GLFW, needed by Task 3)**

```powershell
cmake -B build
cmake --build build --config Release
```

Expected: GLFW clones/configures/builds; `build/Release/cube.exe` produced. First configure takes a few minutes.

- [ ] **Step 4: Commit**

```powershell
git add CMakeLists.txt src/main.cpp
git commit -m "build: CMake scaffold with GLFW 3.4 via FetchContent"
```

---

### Task 3: Vendor GLAD (glad2 single-header)

**Files:**
- Create: `extern/glad/gl.h` (copied, not hand-written)

**Interfaces:**
- Produces: `#include <glad/gl.h>`; one TU defines `GLAD_GL_IMPLEMENTATION` before the include; loader entry point `int gladLoadGL(GLADloadfunc)` returns 0 on failure.

- [ ] **Step 1: Copy the glad2 header out of the fetched GLFW source**

```powershell
New-Item -ItemType Directory -Force "extern\glad"
Copy-Item "build\_deps\glfw-src\deps\glad\gl.h" "extern\glad\gl.h"
```

Expected: file ~100–200 KB. Verify it is the glad2 header-only loader:

```powershell
Select-String -Path "extern\glad\gl.h" -Pattern "GLAD_GL_IMPLEMENTATION" -List
```

Expected: at least one match. **Fallback if the file is missing or lacks that symbol:** generate at https://gen.glad.sh (API: gl 3.3, profile core, options: header-only, loader) and save the produced `gl.h` to `extern/glad/gl.h`.

- [ ] **Step 2: Commit (note the provenance in the message)**

```powershell
git add extern/glad/gl.h
git commit -m "deps: vendor glad2 header-only GL loader (from GLFW 3.4 deps/)"
```

---

### Task 4: main.cpp — black window, version printout, clean close

**Files:**
- Modify: `src/main.cpp` (replace placeholder entirely)

**Interfaces:**
- Consumes: `glad/gl.h` (Task 3), target/include setup (Task 2).
- Produces: the Phase 1 starting point — a valid current 3.3 core context with swap interval 0.

- [ ] **Step 1: Write the real `main.cpp`**

```cpp
#include <cstdio>
#include <cstdlib>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "cube", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    // Uncapped loop: swap interval is 0 and the pacer doesn't exist until
    // Phase 5, so this spins as fast as the driver allows.
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Build**

```powershell
cmake --build build --config Release
```

Expected: clean build, no warnings about `gladLoadGL` redefinition (would indicate `GLAD_GL_IMPLEMENTATION` in more than one TU — it must appear exactly once).

- [ ] **Step 3: Run and verify by observation**

```powershell
.\build\Release\cube.exe
```

Expected: two lines printed, e.g. `GL_VERSION:  3.3.0 ...` and `GL_RENDERER: <GPU name>`; a black 1280×720 window; ESC or the close button exits; then `$LASTEXITCODE` is `0`.

- [ ] **Step 4: Commit**

```powershell
git add src/main.cpp
git commit -m "feat: black 3.3-core window with GLAD init and clean shutdown"
```

---

### Task 5: CI build matrix (Windows + Linux)

**Files:**
- Create: `.github/workflows/build.yml`

**Interfaces:**
- Produces: the workflow Phase 8c later extends with benchmark jobs.

- [ ] **Step 1: Write the workflow**

```yaml
name: build
on: [push, pull_request]

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [windows-latest, ubuntu-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Install GLFW build dependencies (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y xorg-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --config Release
```

(Build-only: runners are headless, so the program is not executed in CI.)

- [ ] **Step 2: Update `CLAUDE.md`**

Replace the "no code exists yet" sentence with a one-liner stating Phase 0 is complete (CMake scaffold, GLFW 3.4 FetchContent, vendored glad2 header, black-window `main.cpp`, build CI on Windows+Linux), leaving the rest of the file intact.

- [ ] **Step 3: Commit and push**

```powershell
git add .github/workflows/build.yml CLAUDE.md
git commit -m "ci: build on windows-latest and ubuntu-latest"
git push origin main
```

- [ ] **Step 4: Watch CI to green**

```powershell
gh run watch --repo tiffany-mares/OpenGL-Renderer
```

Expected: both matrix legs succeed. If the Linux leg fails on missing X11/Wayland packages, fix the apt list in the workflow and push again. (If `gh` is unavailable/unauthenticated, check the Actions tab on GitHub instead.)

---

## Verification (end-to-end)

1. `.\build\Release\cube.exe` prints `GL_VERSION` / `GL_RENDERER`, shows a black window, ESC exits, exit code 0 — run it a few times to confirm the close is reliably clean.
2. GitHub Actions shows green builds for **both** windows-latest and ubuntu-latest on `main` — this is the outline's "builds on at least two of Windows, Linux, macOS" done-condition.
3. `git log --oneline` shows one commit per task, all pushed.
