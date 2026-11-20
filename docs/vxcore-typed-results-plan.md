# Typed vxcore results for VNote

Status: proposed implementation plan; no runtime changes implemented or benchmark results claimed.

## Goal and scope

Add read-only typed C ABI results for folder children, search results, and tag structures,
then migrate VNote's consumers end to end. Remove structured-result JSON construction,
serialization, copying, parsing, and reconstruction where it is only an in-process transport.
Do not expose live notebook objects, STL containers, nlohmann JSON objects, or writable memory.

Planning baseline: VNote `f7c9edbd`; vxcore `f9b19e66b9698e636931c5867e8c2655eacc11c6`.
The current planning change creates only `docs/vxcore-typed-results-plan.md`. `docs/` exists;
`docs/plans/` does not. All source/test paths below are prospective implementation scope.

Included operations:

- Indexed folder children, for bundled and raw notebooks: `vxcore_folder_list_children`.
- Filename search, content search (ordinary, cancellable, streaming), and generic tag search.
- Tag list/hierarchy records, direct association counts, and database-backed tag-file results.
- Every VNote caller of the changed service interfaces, including model, controller, sorting,
  tag-file signal, export, and HTTP adapter consumers.

Deliberately unchanged:

- JSON query inputs, tag mutation inputs, configuration/persistence, events, hooks, and HTTP wire
  formats. No saved configuration/default/schema change: no migration is needed.
- External/unindexed folder enumeration (`listFolderExternal`), node/get-config operations,
  attachment operations, database/index design, filesystem checks, scheduling, and threading policy.
- Search matching algorithms, cap policy, cancellation mechanism, backend selection, and replacement
  authorization. No new cache, thread pool, retries, or live-memory borrowing protocol.
- Existing public JSON C APIs remain supported representations for non-Qt consumers. They share
  native producers with typed APIs; they are not deprecated aliases or duplicate implementations.
  VNote's migrated hot paths do not retain JSON-returning compatibility overloads.

## Findings that constrain the design

- `libs/vxcore/src/api/vxcore_folder_api.cpp:289-365` already receives an owning
  `FolderManager::FolderContents`, then builds JSON and duplicates `dump()` output.
  Bundled listings perform a parent existence guard and per-child existence checks here.
- `libs/vxcore/src/core/folder_manager.h` owns file/folder vectors in `FolderContents`.
  Bundled listings preserve config order; raw listings synchronize filesystem/metadata on read.
  This is not permission to promise a transactionally atomic filesystem snapshot.
- `libs/vxcore/src/search/search_manager.cpp:108-265` constructs result JSON inside the manager,
  including each streaming batch. Adding a different C wrapper alone cannot remove that work.
- `src/core/services/searchservice.cpp:75-193` retains occurrence-level JSON by batch index,
  applies a deterministic occurrence cap, then groups same-line matches. Grouping before capping
  changes behavior. Callbacks can arrive concurrently and out of order.
- `libs/vxcore/src/core/notebook.cpp:707-770` serializes tags, tag-file matches, and counts before
  the C API sees them. Native `TagNode`, `StoreTagQueryResult`, and count vectors already exist.
- `src/models/tagmodel.cpp:295-380` owns hierarchy construction, direct counts, lazy refresh,
  and nonstructural role updates. `TagController` needs each matched file's complete tag set.
- `src/models/notebooknodemodel.cpp:235-277` uses metadata for encryption and per-node colors;
  `src/controllers/exportcontroller.cpp:324-342` uses it to exclude protected notes.
  Tag metadata is exposed through `TagMetadataRole`. Arbitrary metadata cannot simply disappear.
- HTTP raw-search candidates and revalidation are batched by parent folder in
  `src/integrations/httpapi/vnoteapibackend.cpp:889-947,1024-1057`. Preserve this optimization,
  identity rechecks, and all protected/path/capability checks while changing representation.

Research used source reads and targeted callsite searches. clangd was available but unusable for
complete Qt references: it returned declaration-only results and reported `QJsonArray` missing.
Before changing exported signatures during implementation, retry LSP with the compatible build's
compilation database; never mistake an incomplete reference list for a complete migration map.

## Public ABI and ownership

### Representation

Add `libs/vxcore/include/vxcore/vxcore_results.h`, included by `vxcore.h` and installed with the
other public headers. Use one UTF-8 view (`const char *data`, `size_t size`), pointer/count arrays,
fixed-width integer scalar fields, and explicit integer flags rather than C++ types or bitfields.
Lengths are byte lengths; strings need not be NUL-terminated. Nonzero length/count requires a
non-null pointer. Empty arrays use count zero; metadata's empty-object shorthand is defined below.

Freeze the initial public record layouts. Adding fields to array elements changes their stride:
future incompatible layouts require a new versioned type/API, not appending fields silently.
This is an in-process ABI for the target architecture, not a portable serialized binary format.

Use distinct opaque owners and bulk views, not a generic object/reflection framework:

| Result family | Owned data/view fields |
|---|---|
| `VxCoreFolderListing` | Separate ordered folder/file arrays; each record has ID, name, created/modified milliseconds, metadata view, existence-known/existence flags; files also have tags and attachments |
| `VxCoreFileSearchResult` | Ordered file/folder records: kind, relative/absolute paths, ID, timestamps, tags; matched-file count and truncation fact |
| `VxCoreContentSearchResult` | Ordered matched files with path/ID and occurrence arrays; line number, column start/end, line text; matched-file count, truncation, encrypted-skipped count |
| `VxCoreTagList` | Flat ordered name/parent/metadata records; name is identity, empty parent means root |
| `VxCoreTagFileResult` | Ordered relative file path, file name, complete tags; no invented notebook ID |
| `VxCoreTagCounts` | Ordered tag/direct-association-count records; use an integer representation that preserves the current backend values |

Each family has `vxcore_<family>_view(const owner *)` returning an immutable bulk view and
`vxcore_<family>_free(owner *)`. Define exact spelling with the public header in P1, consistently
using `folder_listing`, `file_search_result`, `content_search_result`, `tag_list`,
`tag_file_result`, and `tag_counts`. A null owner yields a null view; free(NULL) is a no-op.
No per-item function calls, per-field allocation, or cross-library caller `delete/free`.

### Entry points

These are additive exported C entry points; use `_typed`, not `_raw` (already overloaded by raw
notebooks and buffer-content APIs):

| Proposed entry point | Result |
|---|---|
| `vxcore_folder_list_children_typed` | `VxCoreFolderListing **out_result` |
| `vxcore_search_files_typed` | `VxCoreFileSearchResult **out_result` |
| `vxcore_search_by_tags_typed` | `VxCoreFileSearchResult **out_result` |
| `vxcore_search_content_typed` | `VxCoreContentSearchResult **out_result` |
| `vxcore_search_content_ex_typed` | Same result, existing optional cancellation input |
| `vxcore_search_content_streaming_typed` | Borrowed typed content-batch callback |
| `vxcore_tag_list_typed` | `VxCoreTagList **out_result` |
| `vxcore_tag_find_files_typed` | `VxCoreTagFileResult **out_result` |
| `vxcore_tag_count_files_by_tag_typed` | `VxCoreTagCounts **out_result` |

Retain the original context/notebook/path/query/operator inputs and validation semantics. The tag
find input remains a JSON tag array and the existing AND/OR operator; typed inputs are not needed
to remove result serialization and would unnecessarily widen this change.

### Lifetime and errors

- Clear a valid output pointer before work. Invalid input, allocation failure, cancellation, or
  another reported failure publishes no owned result; success publishes a valid owner, including
  for an empty result. Preserve existing error classifications; catch C++ exceptions at the C ABI.
- Owners contain all backing data. After successful creation, views survive another API call,
  notebook mutation/closure, and context destruction until their own free call. They contain no
  notebook/SQLite/config pointers. The caller must not free while another thread reads the view.
- Move existing owning native result vectors into result owners where possible. Finalize backing
  storage before projecting contiguous C view arrays, so vector/string moves cannot invalidate
  published pointers. Allocate projection arrays by batch/result size, not per accessor.
- Result ownership does not add thread safety to acquisition: keep existing GUI/worker/gate
  restrictions. Do not retain notebook locks while a consumer inspects a result or callback runs.
- Free is context-independent and releases the entire owner graph through vxcore's allocator.
  VNote wraps owners in `std::unique_ptr` with the corresponding free function, converts once to
  Qt-owned value records, then releases the C owner.
- Streaming callbacks borrow their view, nested arrays, and text only until callback return.
  VNote must copy/convert to Qt-owned records before queueing signals or retaining a batch.
  Callbacks may overlap and arrive out of order; must not throw or reenter vxcore synchronously.
  The blocking call does not return until all callbacks/work it owns have settled, including
  cancellation/error paths. No borrowed pointer crosses the Qt queued-connection boundary.
- Preserve errors the current native interfaces actually report. Do not expand this change into
  a database-error redesign: `TagDb::CountFilesByTag`, for example, can currently return an empty
  vector after SQLite prepare failure. The typed transport must not claim to detect hidden errors.

### Arbitrary metadata exception

Keep arbitrary per-node/tag metadata as a UTF-8 JSON value field. Preserve its value, rather than
inventing a second general-purpose JSON ABI or dropping unknown keys. A zero-length metadata view
means an empty object, allowing empty metadata to avoid serialization/allocation entirely; encode
other values explicitly. In particular, JSON `null` must be encoded as the four bytes `null`,
not treated as the empty-object shorthand. Qt preserves the current object/value interpretation
at the service edge, including the distinction between null metadata and `{}` in tag model roles.

Thus the guarantee is **no result-envelope/record-array JSON on migrated in-process paths**, not
"no JSON anywhere" or "zero-copy." Metadata-only JSON, query inputs, persistence, and real wire
serialization remain intentional. Benchmarks must separately include empty and nonempty metadata.

## Native producers and VNote cutover

### Folder children

1. Extract the current native acquisition plus existence checks into one helper in
   `vxcore_folder_api.cpp`. Both JSON and typed entry points consume it; JSON serialization stays
   at the JSON entry point. Do not route the JSON adapter through C view allocation unnecessarily.
2. Reuse `FolderContents`; do not change backend traversal or add a new tree representation.
   Preserve independent folder/file vector order, IDs, timestamps, tags, attachments, and metadata.
3. Bundled missing parent still returns `VXCORE_ERR_NODE_NOT_EXISTS`. A missing child stays listed
   with known `exists=false`; the fact is transient and never persisted. Raw listing still
   reconciles filesystem state; existence-unknown maps to the current not-missing Qt behavior.
4. Add Qt folder/tag records in `src/core/notebookresulttypes.h`, following the existing
   `searchresulttypes.h` value-type convention. Replace `NotebookCoreService::listFolderChildren`
   with the typed value return while retaining its explicit error outparameter.
5. Migrate internal reorder/create flows, model fetch/prefetch/reload, new-name checks, export,
   TODO folder checks, and sorting seam. Preserve folder-first rendering, external-node handling,
   source-order sorting, per-node colors/protection, missing-node notifications, and no-op reorder
   hook payloads. Sort-dialog nonpositive timestamps remain unknown, not Unix epoch dates.
6. Migrate HTTP `listNodes` (`vnoteapibackend.cpp:561`) as well as typed search candidates and
   per-parent revalidation caches. Preserve its empty-ID `getFileInfo` recheck, display-only
   capabilities, scan limits, and filters. Factor the small identity/name/type/top-level-encrypted
   input consumed by `resolveWithMetadata` into a typed local input shared with the existing
   get-info JSON adapter. Its encrypted bit must mean exactly today's
   `value(kJsonKeyEncrypted).toBool()` on the source listing/get-info object: current producers
   do not emit that top-level key, so it is false. Retain the independent `.vne` suffix check.
   Do NOT substitute `metadata.encrypted`: export and `NotebookNodeModel` already use that
   different predicate and must retain it. Do not conflate these policies during a transport
   refactor or silently add/remove authorization gates. Do not reconstruct listing JSON merely
   to call the old helper; serialize only the final authorized response. Keep raw checks batched.
   Cover ordinary non-`.vne`, metadata-marked non-`.vne`, and `.vne` fixtures at the relevant
   metadata/listing boundaries; retain protected-content exclusion end to end.

### Search results

1. Refactor `SearchManager` to return native filename/tag results and `ContentSearchResult` plus
   encrypted-skipped facts; expose a native batch sink before JSON encoding. Keep one enumeration,
   filtering, matching, cancellation, and result-ordering implementation. JSON APIs serialize it;
   typed APIs project it. Backend matching code is unchanged unless a concrete contract need is
   demonstrated and the plan scope is updated.
2. Preserve all file-result fields, including timestamps/tags needed by the generic tag-search
   wrapper. `SearchCoreService` returns Qt-owned typed values and typed occurrence batches.
3. Extend `searchresulttypes.h/.cpp` with an owning ungrouped content representation and typed
   conversion/grouping helpers. `SearchWorker` stores those batches by index, applies the existing
   occurrence-level cap, and only then builds grouped `SearchResult` values. Previews and the
   authoritative final result remain distinct. Avoid an intermediate QJsonArray/QJsonObject.
4. Keep public `SearchService` signals and final `SearchResult` consumer contracts unchanged,
   including request tokens, cancellation, progress, and drain ownership. The HTTP search route
   and ordinary search controller therefore need no search-signal migration.
5. Preserve backend-native column units: Simple replacement addresses are one-based lines and
   zero-based, end-exclusive UTF-16 columns; do not reinterpret rg byte offsets as replacement
   addresses. Replacement permission is granted only by the existing successful-completion
   checks, never by typed conversion or a streaming preview.
6. Preserve `maxResults` behavior (omitted=100 for content, nonpositive=uncapped, cap may cut within
   a file), first-match-per-file support/rejection, protected-note exclusions, and encrypted-skipped
   counts. Plain empty scans emit no callbacks; an all-encrypted scan retains the existing synthetic
   empty batch that reports skipped counts. Preserve zero-match chunk progress and batch-index order.
7. Remove obsolete Qt JSON response parsers/from-JSON result constructors once every caller/test is
   migrated. Keep query parsing and backend-config capability checks, which are separate concerns.
   Migrate `TagCoreService::searchByTags` and `TagService::searchByTags` to the typed generic-search
   result too; do not leave that duplicate entry point on the JSON result path. Explicitly migrate
   `SearchCoreService::searchContentCancellable` to the typed blob result. Retain the independent
   backend-blob-cap versus streaming-worker-cap comparison in `test_searchservice` and
   `test_search_service_drain`; do not replace both sides with the same worker/converter.
8. Resolve the existing file/tag wrapper mismatch deliberately: `SearchCoreService` currently drops
   the C result's truncation flag and `SearchWorker` recreates `truncated=false`. Preserve current
   user-visible behavior in this representation-only change; do not silently bundle a truncation
   policy fix. Typed C/service results retain the native fact, while the existing async policy is
   explicit. Record a separate bug only if requested, not as an implementation dependency.

### Tags

1. Replace notebook-internal JSON-producing read methods with native result-producing methods;
   preserve virtual dispatch in `Notebook` and `RawNotebook`. JSON serializers move to the C API
   boundary. Use `TagNode`, `StoreTagQueryResult`, and native count vectors rather than duplicating
   SQL, hierarchy algorithms, or metadata-store interfaces.
2. Preserve raw-notebook semantics: tag list succeeds empty; direct tag-file search and counts
   return `VXCORE_ERR_UNSUPPORTED`. Empty AND/OR input returns no matches for supported notebooks.
   Keep config order for tags and backend order for matches/counts; counts remain direct, not
   descendant sums (backend currently sorts counts descending, then tag name ascending).
3. Change `TagCoreService` and `TagService` read returns to Qt records. Preserve observable errors
   and unknown-count behavior rather than manufacturing zeros. Mutating hooks and events stay put.
4. `TagModel` keeps its sole hierarchy-building algorithm, orphan-parent handling, lazy invalidation,
   and selective role updates. Use one tag-list request plus one batched count request; never add a
   count query per node or synchronous queries inside vxcore event callbacks.
5. Migrate `TagController::matchingNodesChanged`, its cached matches, `TagExplorer2`'s slot, and
   `TagFileModel::setNodes` to a typed match vector. Preserve notebook attribution, filename fallback,
   complete tags for compatibility calculation, selection, and activation behavior. Register the
   owning Qt metatype if used by queued connections; no borrowed C records in signals.
6. Migrate `TagViewer2` and test/migration readers. Retain tag metadata role values and behavior;
   do not convert the typed vector back into QJsonArray as an intermediate UI representation.

## Implementation sequence

- **P0 — Baseline and contract checkpoint.** Recheck current refs/worktree and callsites; capture
  real JSON-path timings and semantic fixture outputs for all three domains. Finalize the field
  mapping/owner contracts above and run a plan checkpoint before source edits. Baseline measurement
  is not permission to silently drop tags or another agreed slice if its gain is small.
- **P1 — Shared ABI.** Add the public result header, frozen layouts, owners/views/free contracts,
  and Qt value header. Implement ownership in each existing API translation unit; no generic owner
  registry. Prove a C consumer can include/link/use/free the API, not merely a C++ consumer.
- **P2 — Folder cutover.** Implement shared acquisition, typed C result, Qt conversion, and every
  listing consumer/fixture migration together. Preserve HTTP batching and security checks.
- **P3 — Search cutover.** Refactor native manager results, add typed C operations/callbacks,
  migrate services and occurrence-preserving worker aggregation, and remove obsolete Qt parsers.
- **P4 — Tag cutover.** Refactor notebook native reads/raw overrides, add typed results, migrate
  services/model/controller/widget signal path, and retain arbitrary metadata semantics.
- **P5 — Verification and acceptance.** Build final affected libraries/application/test targets,
  run behavior suites, actual native UI/API smoke scenarios, and paired Release benchmarks.
  Update owning module docs and the changelog with measured claims only, then obtain the
  implementation checkpoint before commit. If review requires changes, rerun affected checks.

P2/P3/P4 can use separate implementation owners after P1 if explicitly parallelized, but shared
`vxcore.h`, `notebookresulttypes.h`, `TagCoreService`, tests, and CMake edits need one integration
owner. They are implementation phases, not permission to deliver a partial migration.

## Exact prospective file scope

Only the new plan document is created now. Existing paths below were confirmed in this checkout;
new implementation paths are explicitly labeled. Reconfirm before editing and update this list at
review if a newly discovered caller requires additional files. Do not touch unrelated user edits.

### New implementation files

- `libs/vxcore/include/vxcore/vxcore_results.h` — C records, opaque owner declarations, bulk views.
- `src/core/notebookresulttypes.h` — owning Qt folder/tag/match/count values.
- `tests/core/benchmark_vxcore_typed_results.cpp` — opt-in paired native/Qt result benchmark, not a
  timing-sensitive CTest. Register an `EXCLUDE_FROM_ALL` executable in the existing core test CMake.

### Existing production files

Shared registration and public exports:

- `libs/vxcore/include/vxcore/vxcore.h`
- `libs/vxcore/src/CMakeLists.txt`
- `src/core/CMakeLists.txt`
- `src/core/services/CMakeLists.txt`
- `tests/core/CMakeLists.txt` — benchmark registration; adjust standalone converter linkage only
  if necessary, without adding a second copy of shared conversion implementations.

Folder slice:

- `libs/vxcore/src/api/vxcore_folder_api.cpp`
- `src/core/services/notebookcoreservice.h`
- `src/core/services/notebookcoreservice.cpp`
- `src/models/notebooknodemodel.h`
- `src/models/notebooknodemodel.cpp`
- `src/widgets/notebookexplorer2.cpp`
- `src/widgets/notebookexplorer2_sortseam.h`
- `src/widgets/notebookexplorer2_sortseam.cpp`
- `src/controllers/exportcontroller.cpp`
- `src/controllers/newfoldercontroller.cpp`
- `src/controllers/newnotecontroller.cpp`
- `src/controllers/todostickercontroller.cpp`
- `src/integrations/httpapi/vnoteapibackend.cpp`

Search slice:

- `libs/vxcore/src/api/vxcore_search_api.cpp`
- `libs/vxcore/src/search/search_manager.h`
- `libs/vxcore/src/search/search_manager.cpp`
- `src/core/services/searchcoreservice.h`
- `src/core/services/searchcoreservice.cpp`
- `src/core/services/searchservice.cpp`
- `src/core/searchresulttypes.h`
- `src/core/searchresulttypes.cpp`

Tag slice (also consumes typed generic search results):

- `libs/vxcore/src/api/vxcore_tag_api.cpp`
- `libs/vxcore/src/core/notebook.h`
- `libs/vxcore/src/core/notebook.cpp`
- `libs/vxcore/src/core/raw_notebook.h`
- `libs/vxcore/src/core/raw_notebook.cpp`
- `src/core/services/tagcoreservice.h`
- `src/core/services/tagcoreservice.cpp`
- `src/core/services/tagservice.h`
- `src/core/services/tagservice.cpp`
- `src/models/tagmodel.cpp`
- `src/models/tagfilemodel.h`
- `src/models/tagfilemodel.cpp`
- `src/controllers/tagcontroller.h`
- `src/controllers/tagcontroller.cpp`
- `src/widgets/tagexplorer2.h`
- `src/widgets/tagexplorer2.cpp`
- `src/widgets/tagviewer2.cpp`

### Existing behavior tests to extend or migrate

Core C ABI behavior/ownership cases go in existing suites, not a new suite for forwarding:

- `libs/vxcore/tests/test_folder.cpp`
- `libs/vxcore/tests/test_missing_nodes.cpp`
- `libs/vxcore/tests/test_folder_child_order.cpp`
- `libs/vxcore/tests/test_search.cpp`
- `libs/vxcore/tests/test_notebook.cpp`
- `libs/vxcore/tests/test_tag_sync.cpp`

Qt signature migrations and behavior coverage:

- `tests/core/test_notebookservice.cpp`
- `tests/core/test_notebook_core_service_reorder.cpp`
- `tests/core/test_tagservice.cpp`
- `tests/core/test_tagviewer2.cpp`
- `tests/core/test_vnote3migrationservice.cpp`
- `tests/core/test_webdav_sync_service.cpp`
- `tests/core/test_searchservice.cpp`
- `tests/core/test_search_service_drain.cpp`
- `tests/core/test_searchresulttypes.cpp`
- `tests/integration/test_missing_nodes_qt.cpp`
- `tests/integration/test_reorder_edge_cases.cpp`
- `tests/integration/test_reorder_integration.cpp`
- `tests/gui/test_tag_view_drag_drop.cpp`
- `tests/models/test_tagfilemodel.cpp`
- `tests/models/test_notebooknodemodel.cpp`
- `tests/widgets/test_notebook_explorer2_sort.cpp`
- `tests/widgets/test_tagexplorer2.cpp`
- `tests/api/test_vnoteapibackend.cpp`

Keep consumer-visible grouping/capping, ownership/invalidation, protection, order, identity, and
transition assertions. Remove incidental JSON-shape/default/copy tests rendered obsolete by the
cutover; do not re-pin them against the new structs. Behavioral parity should compare concrete
results against fixtures, not only compare two adapters that might share the same bug.

Documentation after implementation: `libs/vxcore/AGENTS.md`, `src/core/services/AGENTS.md`,
`src/models/AGENTS.md`, `src/integrations/httpapi/AGENTS.md`, and `changes.md`.
No root rule expansion, on-disk schema changes, translation churn, or changes to other submodules.
Commit the vxcore implementation first and update the parent `libs/vxcore` gitlink in the parent
integration commit. If a push is later requested, build all affected targets from all unpushed
changes, push vxcore first, then the parent. This planning task does not push anything.

## Verification and measurable acceptance

### Behavior and ownership

- Empty/null/error outputs; UTF-8 names, paths, tags and snippets; empty-object, JSON-null,
  and nonempty metadata;
  old/new JSON-value parity where APIs intentionally have the same contract.
- Retain a snapshot across another read and a real rename/tag mutation/close, then inspect its
  original values. Retain it across context destruction and free safely. Exercise repeated
  create/free and failure cleanup under available sanitizers or heap diagnostics.
- Folder ordering/reordering, raw filesystem reconciliation, missing parent/child cases, existence
  never persisted, colors/encryption metadata, protected export exclusion, and sorting timestamps.
- Search: many occurrences on one line, nonadjacent same-line records, cap inside a file, omitted
  and nonpositive caps, no matches, all-encrypted candidates, exact skipped count, first-match mode,
  Simple/rg contracts, Unicode replacement offsets, cancelled scans, interleaved chunk completion,
  no external drainers versus multiple drainers, and teardown with work in flight.
- Tag hierarchy/orphan handling and metadata roles; direct counts versus subtree counts; unknown
  counts on reported failure; AND/OR/empty inputs and complete match tags; raw unsupported reads;
  hidden-tab invalidation and selection-preserving count refresh; typed tag-file activation.
- HTTP unchanged JSON response/schema/capabilities and protected/path checks; UUID identity and
  fresh raw-parent revalidation; no new per-file listing/metadata queries.

Run full affected test targets, not only newly added functions. Initial core set:
`test_folder`, `test_missing_nodes`, `test_folder_child_order`, `test_search`, `test_notebook`,
`test_tag_sync`; also run unchanged backend coverage `test_simple_search_backend`,
`test_rg_search_backend`, and `test_db` because shared search/tag producers are exercised.

Qt set: the basename targets of all Qt files listed above, plus unchanged consumer regressions
`test_searchresultmodel`, `test_searchcontroller`, `test_exportcontroller`,
`test_newnotecontroller`, and `test_notebook_node_proxy_model_order`.

Use separate fresh Qt and vxcore build directories with a compatible Qt/MSVC kit (or supported
native Linux/macOS compiler). Example configuration/build commands from the repository root:

```powershell
cmake -S . -B build-typed-results-qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DVNOTE_BUILD_TESTS=ON
cmake -S libs/vxcore -B build-typed-results-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DVXCORE_BUILD_TESTS=ON
cmake --build build-typed-results-qt --target vnote core_services vnote_httpapi benchmark_vxcore_typed_results
cmake --build build-typed-results-core --target vxcore test_folder test_missing_nodes test_folder_child_order test_search test_notebook test_tag_sync test_simple_search_backend test_rg_search_backend test_db
ctest --test-dir build-typed-results-core -C Release -R '^(test_folder|test_missing_nodes|test_folder_child_order|test_search|test_notebook|test_tag_sync|test_simple_search_backend|test_rg_search_backend|test_db)$' --output-on-failure
```

Build the full enumerated Qt test-target set explicitly, then run its anchored CTest alternation.
Do not interpret the example app build as building those tests. Run shared-global vxcore fixtures
serially. Also compile/link/run a disposable C-language consumer against the exported/installed
headers and shared library. Check static-library use where supported; report unavailable platform
or sanitizer coverage rather than claiming it passed. Never repair a stale build cache in place.

### Actual surface smoke

Use an isolated temporary profile/notebooks and the real VNote executable with
`--no-single-instance`; that flag alone does not isolate user data. Exercise wide-tree expansion,
reload/missing-node recovery, sort/reorder, export protection, search streaming/cancel/replacement,
and tag selection/count refresh/activation. Capture screenshots and observed row/count/order state.
Exercise opt-in loopback HTTP list/search against the fixture, including raw folders and protected
notes, with authorized and denied requests. Tests do not substitute for this surface smoke.

### Performance experiment

The new benchmark is an explicit, non-default executable using isolated fixtures and real APIs,
not production instrumentation or a timing gate in CTest. It links `core_services`, vxcore, and
Qt Core; the benchmark alone may retain a reference JSON-to-Qt conversion path. No production shim.
Configure its runtime output directory explicitly so the run command is stable across kits.

For each domain compare the retained JSON C API plus reference Qt conversion against the typed
C API plus production Qt conversion on the same fixtures, kit, build mode, and backend. Also run
pre-cutover and final end-to-end application measurements, so common-producer refactors cannot
hide a regression in the JSON comparator.

| Domain | Proposed fixtures | Required observations |
|---|---|---|
| Folder children | 100, 1,000, 10,000 siblings; a 50-level tree; bundled and raw; empty versus populated metadata | Acquisition, conversion, total listing/expansion time, peak memory |
| Search | 100, 1,000, 10,000 matched files; repeated occurrences within lines; bounded and uncapped results; Simple and available rg | Time to first batch, completion, conversion CPU/time, peak memory, cancellation completion |
| Tags | 100, 1,000, 10,000 tags; wide/deep hierarchy; sparse/dense associations; large AND/OR match sets | List/count/find and model-refresh time, conversion cost, peak memory |

Exclude fixture creation from timing. Use at least 3 warm-ups and 30 paired/interleaved measured
runs; report median and p95 with run-to-run variability. Separate cold-ish filesystem runs from
warm-cache conversion runs; never label a run cold without controlling/explaining cache state.
Measure allocations/bytes using an available native profiler, and report tool/measurement limits.
Time native acquisition separately from result transport and Qt construction: filesystem stat,
SQL, UTF-8 conversion, metadata parsing, and rendering may still dominate.

Acceptance gates:

1. Every included operation has complete typed C and VNote consumer coverage; no intermediate
   structured-result JSON remains except the documented metadata/query/wire boundaries.
2. All listed semantics and ownership regressions pass; actual UI and HTTP smoke succeeds.
3. Large-result conversion has a repeatable improvement beyond observed noise; target at least
   20% lower median transport-plus-Qt-conversion time per domain, not an asserted speedup.
4. No repeatable >5% regression in end-to-end latency or peak memory on small/large fixtures.
   Investigate threshold breaches with repeated paired runs rather than hiding them in averages.
5. If performance gates cannot be met, present the measurements and revise the plan with user
   approval. Do not silently drop a domain, weaken a behavior test, or add unrelated optimization.

No speedup, build, test, or UI-smoke result is asserted by this planning document.
