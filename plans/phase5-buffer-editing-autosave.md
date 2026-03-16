# Plan: Phase 5 — Buffer Editing & Auto-Save

**Status:** In Progress
**Parent Spec:** specs/buffer-editing-autosave-architecture.md
**Author:** Orchestrator

## Goal

Implement end-to-end buffer editing with revision-based multi-ViewWindow sync, auto-save timer with three policies, and dirty flag optimization for large files.

## Context

### Current State

The new architecture has `Buffer2` (lightweight handle), `BufferCoreService` (vxcore wrapper), `BufferService` (hook-aware wrapper), and `ViewWindow2` (abstract view). These pieces exist but lack:
- Content sync between editor and vxcore buffer
- Revision tracking at the Qt layer
- Auto-save timer and policy execution
- Multi-ViewWindow coordination

### Dependencies

- **Phase 1 (vxcore session sync)** — session persistence after buffer mutations. This plan adds buffer content sync, which is orthogonal but coexists.
- **EditorConfig** — already has `AutoSavePolicy` enum with `None`, `AutoSave`, `BackupFile` values.
- **vxcore buffer** — already has `revision_` counter, `SetContent()`, `SaveContent()`, `WriteBackup()`.

### File Layout for Changes

| File | Changes |
|------|---------|
| `libs/vxcore/include/vxcore/vxcore.h` | Add `vxcore_buffer_get_revision()` |
| `libs/vxcore/src/api/vxcore_buffer_api.cpp` | Implement `vxcore_buffer_get_revision()` |
| `libs/vxcore/tests/test_buffer.cpp` | Add revision API test |
| `src/core/services/buffercoreservice.h` | Add `getRevision()` |
| `src/core/services/buffercoreservice.cpp` | Implement `getRevision()` |
| `src/core/services/buffer2.h` | Add `getRevision()` |
| `src/core/services/buffer2.cpp` | Implement `getRevision()` |
| `src/core/services/bufferservice.h` | Add dirty tracking, timer, active writer, signals |
| `src/core/services/bufferservice.cpp` | Implement auto-save timer and sync logic |
| `src/widgets/viewwindow2.h` | Add `m_editorDirty`, `m_lastKnownRevision`, focus handlers |
| `src/widgets/viewwindow2.cpp` | Implement focus-loss sync, focus-gain revision check |
| `tests/core/test_bufferservice.cpp` | Add auto-save and dirty tracking tests |
| `tests/core/test_buffer.cpp` | Add revision wrapper test |

## Tasks

- [x] **Task 1: vxcore `vxcore_buffer_get_revision()` API** (vxcore layer)

  Add lightweight revision query to vxcore C API.

  **`libs/vxcore/include/vxcore/vxcore.h`** — Add after `vxcore_buffer_is_modified()`:
  ```c
  // Get buffer content revision number.
  // Lightweight — avoids full JSON serialization of vxcore_buffer_get.
  // out_revision: receives current revision counter.
  VXCORE_API VxCoreError vxcore_buffer_get_revision(VxCoreContextHandle context,
                                                     const char *id,
                                                     int *out_revision);
  ```

  **`libs/vxcore/src/api/vxcore_buffer_api.cpp`** — Implement following the `vxcore_buffer_is_modified()` pattern:
  ```cpp
  VxCoreError vxcore_buffer_get_revision(VxCoreContextHandle context, const char *id,
                                          int *out_revision) {
    if (!context || !id || !out_revision) return VXCORE_ERR_NULL_POINTER;
    auto *ctx = static_cast<VxCoreContext *>(context);
    auto *buffer = ctx->buffer_manager->GetBuffer(id);
    if (!buffer) return VXCORE_ERR_NOT_FOUND;
    *out_revision = buffer->GetRevision();
    return VXCORE_OK;
  }
  ```

  **`libs/vxcore/tests/test_buffer.cpp`** — Add test:
  - Open a buffer, check initial revision is 0
  - Set content, check revision incremented
  - Save, check revision incremented again
  - Verify `vxcore_buffer_get_revision()` matches the JSON `"revision"` field from `vxcore_buffer_get()`

  **Validation:**
  - `ctest --test-dir build -R test_buffer` passes
  - New API returns correct revision values

- [x] **Task 2: Qt-layer revision wrappers** (BufferCoreService + Buffer2)

  Add `getRevision()` to both BufferCoreService and Buffer2.

  **`src/core/services/buffercoreservice.h`** — Add in Buffer State section:
  ```cpp
  // Get buffer content revision number.
  int getRevision(const QString &p_bufferId) const;
  ```

  **`src/core/services/buffercoreservice.cpp`** — Implement:
  ```cpp
  int BufferCoreService::getRevision(const QString &p_bufferId) const {
    if (!checkContext()) return 0;
    int revision = 0;
    auto err = vxcore_buffer_get_revision(m_context, p_bufferId.toUtf8().constData(), &revision);
    if (err != VXCORE_OK) {
      qWarning() << "Failed to get revision for buffer" << p_bufferId;
      return 0;
    }
    return revision;
  }
  ```

  **`src/core/services/buffer2.h`** — Add in Buffer State section:
  ```cpp
  // Get buffer content revision number.
  int getRevision() const;
  ```

  **`src/core/services/buffer2.cpp`** — Implement:
  ```cpp
  int Buffer2::getRevision() const {
    if (!isValid()) return 0;
    return m_bufferCoreService->getRevision(m_bufferId);
  }
  ```

  **`tests/core/test_buffer.cpp`** (VNote-level) — Add test verifying Buffer2::getRevision() returns correct values after setContentRaw().

  **Validation:**
  - `ctest --test-dir build -R test_buffer` passes (both vxcore and VNote tests)
  - Buffer2::getRevision() matches vxcore revision after mutations

- [x] **Task 3: BufferService auto-save timer and dirty tracking** (service layer)

  Add the core auto-save machinery to BufferService.

  **`src/core/services/bufferservice.h`** — Add:
  ```cpp
  class QTimer;

  // In public section:

  // Mark a buffer as having pending editor changes.
  // No-op if buffer is read-only.
  void markDirty(const QString &p_bufferId);

  // Immediately sync a dirty buffer's content from its active writer.
  void syncNow(const QString &p_bufferId);

  // Register/unregister the ViewWindow that holds latest content for a buffer.
  void registerActiveWriter(const QString &p_bufferId, ViewWindow2 *p_writer);
  void unregisterActiveWriter(const QString &p_bufferId, ViewWindow2 *p_writer);

  signals:
    void bufferContentSynced(const QString &p_bufferId);
    void bufferAutoSaved(const QString &p_bufferId);
    void bufferAutoSaveFailed(const QString &p_bufferId);
    void bufferAutoSaveAborted(const QString &p_bufferId);

  // In private section:

  void onAutoSaveTimerTick();
  void executeSyncForBuffer(const QString &p_bufferId);

  QTimer *m_autoSaveTimer = nullptr;
  QSet<QString> m_dirtyBuffers;
  QHash<QString, ViewWindow2 *> m_activeWriters;
  QHash<QString, int> m_saveFailureCounts; // For bounded retry
  ```

  **`src/core/services/bufferservice.cpp`** — Implement:
  - Constructor: create `m_autoSaveTimer` (1000ms, periodic), connect to `onAutoSaveTimerTick()`, don't start yet.
  - `markDirty()`: guard read-only, insert to `m_dirtyBuffers`, start timer if not running.
  - `syncNow()`: call `executeSyncForBuffer()`, remove from dirty set, stop timer if empty.
  - `registerActiveWriter()` / `unregisterActiveWriter()`: manage `m_activeWriters` hash.
  - `onAutoSaveTimerTick()`: iterate `m_dirtyBuffers` copy, call `executeSyncForBuffer()` for each, clear set, stop timer.
  - `executeSyncForBuffer()`:
    1. Get active writer; if null, return (already synced)
    2. `writer->getLatestContent()` → `buffer.setContentRaw(content.toUtf8())`
    3. Read `AutoSavePolicy` from `EditorConfig` (via constructor-injected reference or ServiceLocator)
    4. Execute policy: None (skip), AutoSave (`buffer.save()`), BackupFile (`vxcore_buffer_write_backup()`)
    5. On save success: emit `bufferAutoSaved`, reset failure count
    6. On save failure: increment failure count, emit `bufferAutoSaveFailed`; if count >= 3, emit `bufferAutoSaveAborted`, remove from dirty set
    7. Emit `bufferContentSynced()`

  **Note:** BufferService constructor needs access to `EditorConfig`. Since `BufferService` is created in `main.cpp` where `ConfigMgr2` is available, pass `EditorConfig&` as constructor parameter alongside `VxCoreContextHandle` and `HookManager*`.

  **`tests/core/test_bufferservice.cpp`** — Add tests:
  - `markDirty` adds buffer to dirty set and starts timer
  - `syncNow` clears dirty flag and calls getLatestContent on registered writer
  - Read-only buffer: `markDirty` is no-op
  - No active writer: timer tick skips sync
  - Save failure: 3 failures → abort signal emitted

  **Validation:**
  - `ctest --test-dir build -R test_bufferservice` passes
  - Timer starts/stops correctly based on dirty set state

- [x] **Task 4: ViewWindow2 focus handlers and editor integration** (view layer)

  Wire ViewWindow2 to BufferService for dirty tracking and revision-based sync.

  **`src/widgets/viewwindow2.h`** — Add:
  ```cpp
  protected:
    // Track whether editor content has changed since last sync to buffer.
    bool m_editorDirty = false;

    // Last known buffer revision (for detecting external changes on focus gain).
    int m_lastKnownRevision = 0;

    // Called by subclasses when editor content changes.
    void onEditorContentsChanged();

  private:
    // Focus event handlers.
    void onFocusGained();
    void onFocusLost();
  ```

  **`src/widgets/viewwindow2.cpp`** — Implement:

  In constructor — connect to `QApplication::focusChanged` (same pattern as legacy `ViewWindow`):
  ```cpp
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
    bool hasFocusNow = now && (now == this || isAncestorOf(now));
    bool hadFocusBefore = old && (old == this || isAncestorOf(old));
    if (hasFocusNow && !hadFocusBefore) {
      onFocusGained();
    } else if (!hasFocusNow && hadFocusBefore) {
      onFocusLost();
    }
  });

  // Initialize revision from buffer
  m_lastKnownRevision = m_buffer.getRevision();
  ```

  `onEditorContentsChanged()`:
  ```cpp
  void ViewWindow2::onEditorContentsChanged() {
    if (m_buffer.isReadOnly()) return;
    m_editorDirty = true;
    auto &bufferService = m_services.get<BufferService>();
    bufferService.markDirty(m_buffer.id());
  }
  ```

  `onFocusGained()`:
  ```cpp
  void ViewWindow2::onFocusGained() {
    // Register as active writer
    auto &bufferService = m_services.get<BufferService>();
    bufferService.registerActiveWriter(m_buffer.id(), this);

    // Check for external changes (from other ViewWindows)
    int currentRev = m_buffer.getRevision();
    if (currentRev != m_lastKnownRevision) {
      syncEditorFromBuffer();
      m_lastKnownRevision = currentRev;
      m_editorDirty = false; // Content just loaded fresh
    }

    emit focused(this);
  }
  ```

  `onFocusLost()`:
  ```cpp
  void ViewWindow2::onFocusLost() {
    if (m_editorDirty) {
      auto &bufferService = m_services.get<BufferService>();
      bufferService.syncNow(m_buffer.id());
      m_lastKnownRevision = m_buffer.getRevision();
      m_editorDirty = false;
    }
  }
  ```

  Update `aboutToClose()`:
  ```cpp
  bool ViewWindow2::aboutToClose(bool p_force) {
    // Sync pending changes before close
    if (m_editorDirty) {
      auto &bufferService = m_services.get<BufferService>();
      bufferService.syncNow(m_buffer.id());
      m_editorDirty = false;
    }

    // Unregister as active writer to prevent dangling pointer
    auto &bufferService = m_services.get<BufferService>();
    bufferService.unregisterActiveWriter(m_buffer.id(), this);

    return true; // Base implementation; subclasses override for unsaved-changes prompt
  }
  ```

  **Note for subclass implementors:** Concrete ViewWindow2 subclasses (e.g., MarkdownViewWindow2) must call `onEditorContentsChanged()` when their editor widget's content changes. This is typically done by connecting the editor's `contentsChanged` or `textChanged` signal in the subclass constructor.

  **Validation:**
  - ViewWindow2 focus-gain checks revision and refreshes if stale
  - ViewWindow2 focus-loss syncs dirty content immediately
  - ViewWindow2 close cleans up BufferService registrations
  - No dangling pointers after ViewWindow2 destruction

- [ ] **Task 5: Integration test — multi-ViewWindow sync** (end-to-end validation)

  Verify the complete flow works across all layers.

  **Manual test scenario (until automated GUI test infrastructure exists):**
  1. Open a Markdown file → ViewWindow A created
  2. Split view → ViewWindow B created for same buffer
  3. Type in ViewWindow A → wait 1s (or click away) → verify content appears in ViewWindow B when clicked
  4. Type in ViewWindow B → close ViewWindow B → verify ViewWindow A shows B's changes
  5. Change auto-save policy to each value (None, AutoSave, BackupFile) and verify:
     - None: buffer modified indicator stays on, file not saved to disk
     - AutoSave: file saved to disk after timer tick
     - BackupFile: `.vswp` file appears after timer tick
  6. Open a 10MB file, switch to Read mode, switch back — verify no unnecessary delays (dirty flag optimization)

  **Automated test (`tests/core/test_bufferservice.cpp`):**
  - Simulate: register writer A → markDirty → syncNow → register writer B → check revision differs → verify sync needed
  - Simulate: markDirty → close writer (unregister) → timer tick → verify no crash (no active writer, skip)

  **Validation:**
  - All CTest tests pass
  - Manual multi-ViewWindow scenario works for all 3 policies

## Validation

- [ ] Pre-change: `ctest --test-dir build --output-on-failure` passes (baseline)
- [ ] Post-change: all existing tests still pass
- [ ] New vxcore test (`test_buffer` revision tests) passes
- [ ] New VNote tests (`test_buffer`, `test_bufferservice`) pass
- [ ] Manual multi-ViewWindow test passes
- [ ] Review completed (if significant)
- [ ] Changes committed
