#pragma once
#include <windows.h>
#include <vector>
#include <map>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "7zip/Archive/IArchive.h"

typedef HRESULT (WINAPI *Func_GetNumberOfMethods)(UINT32* numMethods);
typedef HRESULT (WINAPI *Func_GetMethodProperty)(UINT32 index, PROPID propID, PROPVARIANT* value);
typedef HRESULT (WINAPI *Func_GetNumberOfFormats)(UINT32* numFormats);
typedef HRESULT (WINAPI *Func_GetHandlerProperty2)(UINT32 index, PROPID propID, PROPVARIANT* value);

// 圧縮ダイアログ用：書き込み可能なフォーマット情報
struct WritableFormat {
    std::wstring label;  // 表示名 e.g. "7-Zip (.7z)"
    std::wstring ext;    // 拡張子 e.g. "7z"
};

// Advanced compression options passed to SevenZip::Compress().
// Any empty string means "use default" (property is not sent to 7z.dll).
struct CompressAdvanced {
    std::wstring dictSize;    // "64k","1m","32m","512m","1g" — dictionary size
    std::wstring wordSize;    // "8","32","64","273" — fast bytes (fb)
    std::wstring solidBlock;  // "off","1m","4g" — solid block size (7z only)
    std::wstring threads;     // "1","4","8" — CPU threads (mt)
    std::wstring extra;       // free-form "key=value" pairs (e.g. "mf=bt4 mpass=2")
    // 分割ボリュームサイズ。"" = 単一ファイル / "10m","100m","1g" 等で指定。
    // 7z/zip 等のシーカブル出力でのみ有効 (gz/bz2/xz/tar の stream wrapping パスでは無視)。
    std::wstring volumeSize;
};

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }
    const std::wstring& GetLoadedName() const { return m_loadedName; }
    // ロード済み 7z.dll のフルパス。未ロード時は空。
    std::wstring GetLoadedPath() const;

    // Detect archive format by extension and open, filling items.
    // 分割アーカイブ (.001/.002/...) の場合は内部アーカイブを一時ファイルへ展開して
    // 再オープンし、中身のエントリを items に返す。effectivePath が non-null なら
    // 後続の Extract/Test 等で使うべきパス（通常は path、自動アンラップ時は一時パス）を書き戻す。
    // 一時ファイルの削除は呼出側の責任。
    HRESULT OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                        const wchar_t* password = nullptr,
                        std::wstring* effectivePath = nullptr);

    // Extract. indices empty = extract all.
    HRESULT Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const wchar_t* destDir,
                    const wchar_t* password,
                    IExtractProgressSink* sink);

    // 全エントリの整合性検証（IInArchive::Extract に testMode=1 を渡す）。
    // 1 件でも検証失敗があれば E_FAIL を返す。
    HRESULT Test(const wchar_t* archivePath,
                 const wchar_t* password,
                 IExtractProgressSink* sink);

    // 指定インデックスのエントリを削除（残すエントリだけ新アーカイブにコピー）。
    // 失敗時は元ファイルを変更しない。書き込みをサポートしないフォーマット
    // (rar/iso/cab 等) では IOutArchive 取得段階で失敗し E_NOTIMPL 等を返す。
    HRESULT DeleteItems(const wchar_t* archivePath,
                        const std::vector<UINT32>& deleteIndices,
                        const wchar_t* password,
                        IExtractProgressSink* sink);

    // Compress srcPaths into outPath.
    HRESULT Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     const wchar_t* format,   // "7z","zip","tar","gz","bz2","xz"
                     int level,               // 0-9
                     const wchar_t* method,   // "lzma","deflate","zstd", etc.
                     const wchar_t* password,
                     IExtractProgressSink* sink,
                     const CompressAdvanced* adv = nullptr,
                     bool encryptHeaders = false);

    // Auto-detect installed 7z.dll from registry or known paths.
    static std::wstring Find7zDll();

    // Returns lowercased encoder names supported by the loaded DLL.
    // Empty if DLL is not loaded or enumeration is unavailable.
    const std::vector<std::wstring>& GetEncoderNames() const { return m_encoderNames; }

    // ext は拡張子のみ（ドットなし, 例: L"7z"）。大文字小文字不問。
    bool IsArchiveExt(const wchar_t* ext) const;

    // 7z.dll が書き込みをサポートするフォーマット一覧（RAR は含まない）。
    const std::vector<WritableFormat>& GetWritableFormats() const { return m_writableFormats; }

private:
    HMODULE                      m_hDll               = nullptr;
    std::wstring                 m_loadedName;
    Func_CreateObject            m_pfnCreateObject    = nullptr;
    Func_GetNumberOfMethods      m_pfnGetNumMethods   = nullptr;
    Func_GetMethodProperty       m_pfnGetMethodProp   = nullptr;
    Func_GetNumberOfFormats      m_pfnGetNumFormats   = nullptr;
    Func_GetHandlerProperty2     m_pfnGetHandlerProp2 = nullptr;
    std::vector<std::wstring>    m_encoderNames;   // lowercased; populated by EnumerateCodecs()
    std::map<std::wstring, GUID> m_extToClsid;     // 拡張子(小文字) → CLSID
    std::vector<WritableFormat>  m_writableFormats; // 書き込み可能フォーマット（UI 用）

    void EnumerateCodecs();
    void EnumerateFormats();

    HRESULT CreateInArchive(const GUID& clsid, IInArchive** ppArc);
    HRESULT CreateOutArchive(const GUID& clsid, IOutArchive** ppArc);
    GUID FormatToInGuid(const wchar_t* path) const;
    GUID FormatToOutGuid(const wchar_t* format) const;
    static std::wstring ExtOfPath(const wchar_t* path);
};
