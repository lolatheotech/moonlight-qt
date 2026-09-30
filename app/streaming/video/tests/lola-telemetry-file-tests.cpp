#include "../lola-telemetry-file.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <thread>
#include <cstdio>

static int checks = 0;
static bool check(bool value, const char* label) {
    ++checks;
    if (!value) std::fprintf(stderr, "FAIL %s\n", label);
    return value;
}
static bool publish(const QString& path, int sequence) {
    LoLaTelemetryFile out(path);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"sequence",sequence}}).toJson(QJsonDocument::Compact);
    if (!out.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
        std::fprintf(stderr,"publish open failed sequence=%d\n",sequence); return false;
    }
    if (out.write(bytes) != bytes.size()) {
        std::fprintf(stderr,"publish write failed sequence=%d\n",sequence); return false;
    }
    const bool committed=out.commit();
    if(!committed) std::fprintf(stderr,"publish commit failed sequence=%d error=%s\n",sequence,qPrintable(out.errorString()));
    return committed;
}
static QByteArray read(const QString& path) {
    QFile in(path); if (!in.open(QIODevice::ReadOnly)) return {};
    return in.readAll();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!check(dir.isValid(),"isolated temporary directory")) return 1;
    const QString path=dir.path()+"/status.engine.json";
    if (!check(publish(path,0),"initial snapshot")) return 1;

    // Match the supervisor's delete-sharing reader; every visible version must
    // be a complete JSON snapshot, never missing/partial during replacement.
    std::atomic<bool> done{false}, ready{false};
    std::atomic<int> reads{0}, bad{0};
    std::thread reader([&] {
        do {
            HANDLE h=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            if(h==INVALID_HANDLE_VALUE) { ++bad; std::fprintf(stderr,"reader open failed error=%lu\n",GetLastError()); }
            else {
                char buffer[256]; DWORD count=0;
                if(!ReadFile(h,buffer,sizeof(buffer),&count,nullptr)) { ++bad; std::fprintf(stderr,"reader read failed error=%lu\n",GetLastError()); }
                else {
                    QJsonParseError error;
                    const auto doc=QJsonDocument::fromJson(QByteArray(buffer,int(count)),&error);
                    if(error.error!=QJsonParseError::NoError || !doc.isObject() ||
                       !doc.object().value("sequence").isDouble()) { ++bad; std::fprintf(stderr,"reader partial or invalid JSON\n"); }
                    ++reads;
                }
                CloseHandle(h);
            }
            ready=true;
            std::this_thread::yield();
        } while(!done);
    });
    while(!ready) std::this_thread::yield();
    int writes=0;
    for(int i=1;i<=200;i++) { if(publish(path,i)) ++writes; else break; }
    done=true;reader.join();
    bool ok=check(writes==200,"200 successful atomic replacements");
    ok=check(reads>0 && bad==0,"concurrent readers see only complete snapshots")&&ok;
    ok=check(QJsonDocument::fromJson(read(path)).object().value("sequence").toInt()==200,"latest snapshot visible")&&ok;

    const auto before=read(path);
    // POSIX replacement intentionally supports held reader handles. Use an
    // explicitly read-only target for the failure-preserves-prior assertion.
    ok=check(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_ATTRIBUTE_READONLY)!=0,"read-only target established")&&ok;
    const bool replaced=publish(path,201);
    const bool restored=SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_ATTRIBUTE_NORMAL)!=0;
    ok=check(restored && !replaced && read(path)==before,"failed replace preserves prior snapshot and restores attribute")&&ok;    ok=check(!publish(dir.path()+"/absent/snapshot.json",1),"missing parent fails closed")&&ok;
    ok=check(QDir(dir.path()).entryList(QDir::Files).size()==1,"no temporary-file residue")&&ok;
    std::printf("{\"checks\":%d,\"successfulWrites\":%d,\"concurrentReads\":%d,\"badReads\":%d,\"passed\":%s}\n",
        checks,writes,reads.load(),bad.load(),ok?"true":"false");
    return ok?0:1;
}