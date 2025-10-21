# VNote Agent Development Guide

## Build/Lint/Test Commands

* **Build**: `mkdir build && cd build && cmake .. && cmake --build . --config Release`
* **Run Tests**: Build tests manually by enabling `add_subdirectory(tests)` in root CMakeLists.txt
* **Run Single Test**: `cd build && ctest -R test_task` (after building tests)

## C++ Code Style Guidelines

* **Standards**: C++14, Qt 5/6 framework with MOC/UIC/RCC enabled
* **Includes**: System includes first, then Qt includes, then local includes (see `main.cpp:1-29`)
* **Namespaces**: Use `vnotex` namespace for all core classes
* **Headers**: Use header guards (`#ifndef CLASSNAME_H`), forward declarations in headers
* **Classes**: CamelCase, private inheritance for utility classes (Noncopyable pattern)
* **Methods**: camelCase with getter prefixes (`getInst()`, `getThemeMgr()`)
* **Members**: Prefix private members with `m_` (e.g., `m_jobj`)
* **Singletons**: Use static instance pattern with `getInst()` method
* **Qt Integration**: Use signals/slots, QObject inheritance, Q_OBJECT macro
* **Memory**: Use Qt smart pointers (QScopedPointer, QSharedPointer)
* **Config**: Access via ConfigMgr singleton, JSON-based hierarchical config
* **Architecture**: Follow layered structure - core/ for logic, widgets/ for UI
* **Error Handling**: Use Qt patterns and custom Exception classes

## Architecture Notes
* Core singleton: VNoteX coordinates all managers (ThemeMgr, TaskMgr, NotebookMgr, BufferMgr)  
* Refer to .github/copilot-instructions.md for complete architecture overview