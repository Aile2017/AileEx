# Code Quality Audit Report

**Date:** 2026-05-10  
**Scope:** Comprehensive source code review for redundancy, unused code, and refactoring opportunities

## Summary

This audit identified 9 major categories of code duplication and quality issues, with recommendations for refactoring. Prior refactoring efforts have already addressed some areas (Settings initialization, FillCombo/FillThreadCombo unification). This document captures remaining opportunities.

---

## Completed Refactorings (Already Done)

1. **SettingsDlg OnBrowseFile() Helper** ✅
   - Extracted duplicate file dialog code from 3 case blocks (50 lines → 8 lines)
   - Impact: 84% reduction in duplication

2. **DialogUtils.h Consolidation** ✅
   - Centralized FormatFileTime(), FormatSize(), AddRow()
   - Used by InfoDlg, PropertiesDlg, and future dialogs
   - Impact: 30+ lines of duplicate code eliminated

3. **ComboEntry & FillCombo Unification** ✅
   - Merged ComboEntry/RarComboEntry into single shared struct
   - Unified FillCombo/FillRarCombo into single function in DialogUtils
   - Files: AdvancedCompressDlg.cpp, RarAdvancedDlg.cpp
   - Impact: 39 lines of redundant code eliminated

4. **Settings Member Initialization Deduplication** ✅
   - Removed member initializers immediately overwritten by Load()
   - All initialization now consolidated in Load() method
   - Settings.h: 30+ lines reduced
   - Impact: Single source of truth for default values

5. **FillThreadCombo Helper Creation** ✅
   - Extracted duplicate thread combo initialization from AdvancedCompressDlg and RarAdvancedDlg
   - Includes CPU-aware limiting logic
   - Impact: 18 + 28 = 46 lines of duplicate code eliminated

6. **File Selection Dialog Helper** ✅
   - `BrowseMultipleFiles()` added to `DialogUtils.h`
   - Used by `OnAddFiles()` and `OnAddFilesToCurrentArchive()`
   - Impact: ~20 lines of duplicate OPENFILENAMEW + multi-select parsing code eliminated

7. **Folder Browse Dialog Helper** ✅
   - `BrowseFolderDialog()` added to `DialogUtils.h`
   - Used in `MainWindow.cpp` × 2 and `SettingsDlg.cpp`
   - Impact: ~40 lines of duplicate IFileOpenDialog code eliminated

8. **Dialog DlgProc Boilerplate** ✅
   - `StandardDlgProc<T>()` template function added to `DialogUtils.h`
   - All 7 dialog classes (SettingsDlg, CompressDlg, AdvancedCompressDlg, RarAdvancedDlg, CommentDlg, PropertiesDlg, InfoDlg) use it
   - Impact: ~56 lines of repeated dispatch code eliminated

---

## Identified Remaining Issues

### 1. FILE SELECTION DIALOG DUPLICATION ✅ COMPLETED

See "Completed Refactorings" section above.

---

### 2. FOLDER BROWSE DIALOG DUPLICATION ✅ COMPLETED

See "Completed Refactorings" section above.

---

### 3. FILE BROWSE DIALOG DUPLICATION (MEDIUM PRIORITY)

**Locations:**
- `SettingsDlg.cpp::OnBrowseFile()` (lines 136-154)
- `CompressDlg.cpp` (lines 376-385)

**Issue:** Nearly identical OPENFILENAMEW patterns with only flag variations (10+ lines):
```cpp
OPENFILENAMEW ofn = {};
ofn.lStructSize = sizeof(ofn);
ofn.hwndOwner   = hwnd;
ofn.lpstrFilter = filter.c_str();
ofn.lpstrFile   = path;
ofn.nMaxFile    = MAX_PATH;
ofn.Flags       = OFN_FILEMUSTEXIST;  // or OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST
ofn.lpstrTitle  = title.c_str();
if (GetOpenFileNameW(&ofn))  // or GetSaveFileNameW
    SetDlgItemTextW(hwnd, pathCtrlId, path);
```

**Impact:** Minor flag differences are repeated; consolidation would be cleaner.

**Recommended Fix:** Create parameterized helper:
```cpp
bool BrowseForFile(HWND hwnd, UINT titleId, UINT filterId, DWORD flags,
                   wchar_t* outPath, size_t maxPath);
```

---

### 4. DIALOG PROCEDURE BOILERPLATE ✅ COMPLETED

See "Completed Refactorings" section above.

---

### 5. EXTRACT OPERATION DUPLICATION ✅ COMPLETED

**Location:** `MainWindow.cpp`

**Resolution:** Extracted common logic into `RunExtraction(indices, rarTargetPaths)` private method.
- `OnExtract()` calls `RunExtraction({}, {})` (extract all)
- `OnExtractSelected()` collects indices/paths, then calls `RunExtraction(indices, rarTargetPaths)`
- Impact: ~70 lines of duplicate code eliminated

---

### 6. DUPLICATE 7Z_NOT_LOADED VALIDATION ✅ PARTIALLY COMPLETED

**Location:** `MainWindow.cpp`

**Resolution:** `Ensure7zLoaded(bool useUnrar = false)` private method added to `MainWindow`.
Applied to 3 early-return guard sites (OnOpenAssoc, RunExtraction, OnTest).

**Remaining:** 3 late-check sites (AddFilesToCurrentArchive, OnDelete, OnCompress) check after
ProgressDlg is already open, requiring `progDlg.Dismiss()` + `delete sink` cleanup before returning.
These cannot use the simple helper without additional refactoring.

---

### 7. UNUSED RESOURCE DEFINITIONS ✅ INVESTIGATED — NO ACTION NEEDED

**Location:** `resource.h`

**Findings:**
- **IDC_ABOUT_URL (8102)**: Used in `res/AileEx.rc` (About dialog URL text control) — not dead code.
- **IDD_PASSWORD (207)**: Already removed — not found in resource.h.

---

### 8. COMBO INITIALIZATION PATTERN (MEDIUM PRIORITY - PARTIALLY ADDRESSED)

**Locations:** AdvancedCompressDlg, RarAdvancedDlg

**Status:** FillThreadCombo helper created ✅

**Note:** While thread combo initialization is now unified, both dialogs still have similar patterns for populating multiple combos. Future refactoring could create a more generalized ComboBox initialization framework if more dialogs are added.

---

## Refactoring Priority Roadmap

### Phase 1 ✅ COMPLETED (High Impact, High Frequency)
1. **Extract file/folder dialog helpers** — `BrowseMultipleFiles()`, `BrowseFolderDialog()` in `DialogUtils.h`
2. **Eliminate Dialog DlgProc boilerplate** — `StandardDlgProc<T>()` template in `DialogUtils.h`

### Phase 2 ✅ COMPLETED (High Impact, Moderate Refactoring)
3. **Consolidated extract operations** — `RunExtraction(indices, rarTargetPaths)` private method in `MainWindow`

### Phase 3 ✅ COMPLETED (Medium Impact)
4. **Validation helper** `Ensure7zLoaded(useUnrar)` — applied to 3 early-return guard sites
5. **Single file browse helper** `BrowseForFile()` in `DialogUtils.h` — used by SettingsDlg and CompressDlg
6. **Resource audit** — IDC_ABOUT_URL confirmed used in RC file; IDD_PASSWORD was already absent

---

## Code Metrics Summary

| Category | Duplicated Lines | Occurrences | Est. Reduction | Status |
|----------|-----------------|-------------|----------------|--------|
| File dialogs (multi) | 20+ | 2 | 20 lines | ✅ Done |
| Folder dialogs | 20+ | 3 | 40 lines | ✅ Done |
| File browse (single) | 10+ | 2 | 10 lines | ✅ Done |
| DlgProc boilerplate | 8+ | 7 | 56 lines | ✅ Done |
| Extract logic | 100+ | 2 | 70 lines | ✅ Done |
| 7Z validation (early-return) | 5+ | 3 of 6 | ~15 lines | ✅ Done |
| **Total Potential Reduction** | | | **~221 lines** | |
| **Achieved** | | | **~211 lines** | |

---

## Notes

- This audit was conducted using comprehensive code pattern analysis
- All refactorings (Settings, FillCombo, FillThreadCombo, DialogUtils, Phase 1-3) reduced code by ~300+ lines total
- Remaining duplication in MainWindow.cpp: 3 late-check 7Z validation sites (inside ProgressDlg scope) require separate cleanup — low priority

---

**Status: All planned phases complete.**
