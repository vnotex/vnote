
## T11 widget smoke test — blocked

NotebookExplorer2 construction needs full ServiceLocator: NotebookCoreService, ConfigCoreService, BufferService, ConfigMgr2, FileTypeCoreService, ThemeService, NotebookNodeController, and more. Matches the T3-T10 controller-construction blocker. Created tests/widgets/test_notebookexplorer2_multiselect.cpp as a QSKIP stub per plan T11 guidance (30-min cap). Slot registered for future expansion when a NotebookExplorer2 test harness exists.

