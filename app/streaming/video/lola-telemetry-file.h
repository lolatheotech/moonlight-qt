#pragma once
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <QFile>
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include <cstring>
#include <qt_windows.h>

// Only ephemeral LoLa telemetry snapshots. QSaveFile's durable commit can block
// the decoder for seconds. Flush Qt's user-space buffer, then atomically replace
// in the same directory without FlushFileBuffers or MOVEFILE_WRITE_THROUGH.
// Crash durability is deliberately not promised for disposable telemetry.
// Settings, pairing material, and other persistent state still use QSaveFile.
class LoLaTelemetryFile
{
public:
    explicit LoLaTelemetryFile(const QString& path)
        : m_Target(path), m_File(path + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp") {}

    ~LoLaTelemetryFile() {
        if (m_Created) { m_File.close(); QFile::remove(m_File.fileName()); }
    }
    bool open(QIODevice::OpenMode mode) {
        if (m_Created || mode != (QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        m_Created = m_File.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        return m_Created;
    }
    qint64 write(const QByteArray& bytes) {
        const auto count = m_File.write(bytes);
        m_WriteOk = m_WriteOk && count == bytes.size();
        return count;
    }
    bool commit() {
        if (!m_WriteOk || !m_File.flush()) return false;
        m_File.close();
        // Windows 10+ NTFS POSIX replacement keeps the old file readable through
        // existing handles while atomically publishing the new name. Legacy
        // ReplaceFile/MoveFileEx failed our no-missing/no-partial-reader test.
        const HANDLE handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(m_File.fileName().utf16()), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            m_Error = QStringLiteral("Telemetry rename handle failed (%1)").arg(GetLastError());
            return false;
        }
        const QString destination = QDir::toNativeSeparators(QFileInfo(m_Target).absoluteFilePath());
        const DWORD nameBytes = DWORD(destination.size() * sizeof(WCHAR));
        QByteArray storage(int(sizeof(FILE_RENAME_INFO) + nameBytes), 0);
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        info->RootDirectory = nullptr;
        info->FileNameLength = nameBytes;
        std::memcpy(info->FileName, destination.utf16(), nameBytes);
        const bool renamed = SetFileInformationByHandle(handle, FileRenameInfoEx,
                                                        info, DWORD(storage.size())) != 0;
        const DWORD error = renamed ? ERROR_SUCCESS : GetLastError();
        CloseHandle(handle);
        if (!renamed) {
            m_Error = QStringLiteral("Atomic telemetry rename failed (%1)").arg(error);
            return false;
        }        m_Created = false; // now lives at m_Target, not the temporary name
        return true;
    }
    QString errorString() const { return m_Error.isEmpty() ? m_File.errorString() : m_Error; }

private:
    QString m_Target;
    QFile m_File;
    QString m_Error;
    bool m_WriteOk = true;
    bool m_Created = false;
};
#else
using LoLaTelemetryFile = QSaveFile;
#endif