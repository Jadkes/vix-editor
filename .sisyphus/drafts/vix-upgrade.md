# Draft: Vix Editor Upgrade Plan

## Current Project State

### Technology Stack
- **Core**: C++ with ncurses (terminal UI)
- **AI Layer**: Python 3.14 embedding (vix_brain.py)
- **Build**: g++ with Python and ncurses linking

### Current Features
- File editing with syntax highlighting (C++, Python, basic others)
- Sidebar file explorer with colors by file type
- AI Ghost code suggestions (rule-based)
- C++ linter (basic brace/semicolon checking)
- Auto-close brackets/braces/quotes
- Mouse support (scroll, click to position)
- Find (Ctrl+F) and Go-to-line (Ctrl+G)
- Compile and Run (C++, Python)
- Help menu

### Code Quality Observations
- Uses `using namespace std` (not modern C++)
- No RAII patterns, manual resource management
- No unit tests
- Single large class (400+ lines)
- Basic error handling

---

## Upgrade Scope (TO BE DETERMINED)

### Potential Upgrade Areas
1. **Modern C++ Refactoring** - Move to C++20, smart pointers, proper patterns
2. **Enhanced AI/ML** - LLM integration instead of rule-based suggestions
3. **Extended Language Support** - More syntax highlighters
4. **UI/UX Improvements** - Theming, split panes, tabs
5. **Core Editor Features** - Undo/Redo, multi-file tabs, search & replace
6. **Plugin System** - Extensibility framework
7. **Configuration** - Customizable settings file
8. **Performance** - Async operations, incremental parsing
9. **Testing** - Add test infrastructure

---

## Open Questions - ANSWERS RECEIVED

1. **Upgrade Direction**: 
   - ✅ Modern C++ Refactoring (C++20, RAII, smart pointers)
   - ✅ Add Missing Editor Features (Undo/Redo)
   - ✅ Basic AI improvements (better local AI, NOT LLM)

2. **Feature Priority**: 
   - ✅ Undo/Redo system is priority #1

3. **AI Evolution**: 
   - ✅ Keep rule-based, improve locally (more languages, better context)

4. **Backward Compat**: 
   - ✅ Stay compatible - keep existing keybinds working

---

## Test Strategy Decision
- **Infrastructure exists**: NO (this is a C++ project without tests)
- **Add test infrastructure**: C++ testing with GoogleTest or doctest
- **Agent-Executed QA**: ALWAYS - terminal UI requires hands-on verification

---

## Scope Boundaries
- INCLUDE: Modern C++ refactoring, Undo/Redo, improved AI, backward compatibility
- EXCLUDE: LLM integration, tabs/split panes (future), plugin system
