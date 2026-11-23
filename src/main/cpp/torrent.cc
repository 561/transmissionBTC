#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "commons.h"
#include "native_to_java.h"
#include "transmission-private.h"
#include <semaphore.h>

typedef struct MagnetToTorrentData {
    sem_t *sem;
    const char *magnet;
    char *path;
    int torrentId;
    jboolean enqueue;
} MagnetToTorrentData;

static std::mutex magnetMutex;
static std::map<int, MagnetToTorrentData *> magnetWaiters;

void torrentMetadataReceived(tr_torrent *tor) {
  MagnetToTorrentData *d = nullptr;
  {
    std::lock_guard<std::mutex> lock(magnetMutex);
    auto it = magnetWaiters.find(tr_torrentId(tor));
    if (it == magnetWaiters.end()) return;
    d = it->second;
    magnetWaiters.erase(it);
  }

  std::string const filename = tr_torrentFilename(tor);
  d->path = strdup(filename.c_str());
  sem_post(d->sem);
}

static void setSequential(tr_torrent *tor, bool sequential) {
  tor->set_sequential_download(sequential);
}

extern "C" {
// ----------------------------------- torrentAdd --------------------------------------------------
/*
 * Returns:
 *  0 - OK
 *  1 - PARSE_ERR
 *  2 - DUPLICATE
 *  3 - OK_DELETE
 */
JNIEXPORT jint JNICALL
Java_com_ap_transmission_btc_Native_torrentAdd(
    JNIEnv *env, jclass __unused c, jlong jsession, jstring jpath, jstring jdownloadDir,
    jboolean setDelete, jboolean sequential, jintArray unwantedIndexes,
    jbyteArray returnMeTorrentHash) {
  bool del = false;
  tr_ctor *ctor = ctorFromFile(env, jsession, jpath, false);

  if (ctor == NULL) return 1;

  if (jdownloadDir != NULL) {
    const char *downloadDir = env->GetStringUTFChars(jdownloadDir, 0);
    tr_ctorSetDownloadDir(ctor, TR_FORCE, downloadDir);
    env->ReleaseStringUTFChars(jdownloadDir, downloadDir);
  }

  std::vector<jint> unwanted;

  if (unwantedIndexes != NULL) {
    jsize len = env->GetArrayLength(unwantedIndexes);
    unwanted.resize(len);
    env->GetIntArrayRegion(unwantedIndexes, 0, len, unwanted.data());
    std::vector<tr_file_index_t> trIndexes(unwanted.begin(), unwanted.end());
    tr_ctorSetFilesWanted(ctor, trIndexes.data(), (tr_file_index_t) trIndexes.size(), false);
  }

  int err = 0;
  tr_ctorSetDeleteSource(ctor, setDelete);
  tr_torrent *duplicate = NULL;
  tr_torrent *tor = tr_torrentNew(ctor, &duplicate);

  if (tor != NULL) {
    setSequential(tor, sequential);
    err = (tr_ctorGetDeleteSource(ctor, &del) && del) ? 3 : 0;
  } else if (duplicate != NULL) {
    err = 2;

    if (unwantedIndexes != NULL) {
      tor = duplicate;
      std::vector<tr_file_index_t> wanted;

      for (tr_file_index_t i = 0, n = tr_torrentFileCount(tor); i < n; i++) {
        if (tr_torrentFile(tor, i).wanted) continue;
        bool w = true;

        for (jint u : unwanted) {
          if ((tr_file_index_t) u == i) {
            w = false;
            break;
          }
        }

        if (w) wanted.push_back(i);
      }

      if (!wanted.empty()) {
        tr_torrentSetFileDLs(tor, wanted.data(), (tr_file_index_t) wanted.size(), true);
        err = (tr_ctorGetDeleteSource(ctor, &del) && del) ? 3 : 0;
      }
    }
  } else {
    err = 1;
  }

  if ((returnMeTorrentHash != NULL) && (tor != NULL)) {
    env->SetByteArrayRegion(returnMeTorrentHash, 0, SHA_DIGEST_LENGTH,
                            (const jbyte *) tor->info_hash().data());
  }

  tr_ctorFree(ctor);
  return err;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentRemove ----------------------------------------------
typedef struct TorrentRemoveData {
    jint torrentId;
    jboolean removeLocalData;
} TorrentRemoveData;

static void *torrentRemove(tr_session *session, void *data, Err *err) {
  TorrentRemoveData *d = (TorrentRemoveData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  tr_torrentRemove(tor, d->removeLocalData, NULL, NULL);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentRemove(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jboolean removeLocalData) {
  TorrentRemoveData d = {torrentId, removeLocalData};
  runInTransmissionThreadEx(env, jsession, torrentRemove, &d);
  CATCH:;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentStop ------------------------------------------------
static void *torrentStop(tr_session *session, void *data, Err *err) {
  tr_torrent *tor = findTorrentByIdEx(session, (int) (intptr_t) data, err);
  tr_torrentStop(tor);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentStop(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  runInTransmissionThreadEx(env, jsession, torrentStop, (void *) (intptr_t) torrentId);
  CATCH:;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentStart -----------------------------------------------
static void *torrentStart(tr_session *session, void *data, Err *err) {
  tr_torrent *tor = findTorrentByIdEx(session, (int) (intptr_t) data, err);
  tr_torrentStart(tor);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentStart(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  runInTransmissionThreadEx(env, jsession, torrentStart, (void *) (intptr_t) torrentId);
  CATCH:;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentVerify ----------------------------------------------
static void *torrentVerify(tr_session *session, void *data, Err *err) {
  tr_torrent *tor = findTorrentByIdEx(session, (int) (intptr_t) data, err);
  tr_torrentVerify(tor);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentVerify(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  runInTransmissionThreadEx(env, jsession, torrentVerify, (void *) (intptr_t) torrentId);
  CATCH:;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------- torrentListFilesFromFile ----------------------------------------
JNIEXPORT jobjectArray JNICALL
Java_com_ap_transmission_btc_Native_torrentListFilesFromFile(
    JNIEnv *env, jclass __unused c, jstring jtorrent) {
  const char *path = env->GetStringUTFChars(jtorrent, 0);
  tr_torrent_metainfo tm;

  if (!tm.parse_torrent_file(path)) {
    throwException(__FILENAME__, __LINE__, env, CLASS_IOEX, "Failed to parse torrent file: %s", path);
    env->ReleaseStringUTFChars(jtorrent, path);
    return NULL;
  }

  env->ReleaseStringUTFChars(jtorrent, path);
  tr_file_index_t count = tm.file_count();
  jobjectArray a = env->NewObjectArray(count, env->FindClass("java/lang/String"), NULL);

  for (tr_file_index_t i = 0; i < count; i++) {
    jobject jname = env->NewStringUTF(tm.file_subpath(i).c_str());
    env->SetObjectArrayElement(a, i, jname);
    env->DeleteLocalRef(jname);
  }

  return a;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------------- torrentListFiles --------------------------------------------
typedef struct ListFilesData {
    jint torrentId;
    std::vector<std::string> fileNames;
} ListFilesData;

static void *torrentListFiles(tr_session *session, void *data, Err *err) {
  ListFilesData *d = (ListFilesData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);

  for (tr_file_index_t i = 0, n = tr_torrentFileCount(tor); i < n; i++) {
    d->fileNames.emplace_back(tr_torrentFile(tor, i).name);
  }

  CATCH:
  return NULL;
}

JNIEXPORT jobjectArray JNICALL
Java_com_ap_transmission_btc_Native_torrentListFiles(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  ListFilesData d;
  d.torrentId = torrentId;
  jobjectArray result = NULL;
  runInTransmissionThreadEx(env, jsession, torrentListFiles, &d);
  if (d.fileNames.empty()) return NULL;
  result = env->NewObjectArray((jsize) d.fileNames.size(), env->FindClass("java/lang/String"), NULL);

  for (size_t i = 0; i < d.fileNames.size(); i++) {
    jobject jname = env->NewStringUTF(d.fileNames[i].c_str());
    env->SetObjectArrayElement(result, (jsize) i, jname);
    env->DeleteLocalRef(jname);
  }

  CATCH:
  return result;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------- torrentMagnetToTorrentFile ----------------------------------------
static void *torrentMagnetToTorrentFile(tr_session *session, void *data, Err *err) {
  MagnetToTorrentData *d = (MagnetToTorrentData *) data;
  tr_ctor *ctor = tr_ctorNew(session);

  if (!tr_ctorSetMetainfoFromMagnetLink(ctor, d->magnet, nullptr)) {
    tr_ctorFree(ctor);
    err->set(err, CLASS_IOEX, "Failed to set meta from magnet link");
    return NULL;
  }

  tr_torrent *duplicate = NULL;
  tr_torrent *tor = tr_torrentNew(ctor, &duplicate);
  tr_ctorFree(ctor);

  if (tor == NULL) {
    if (duplicate != NULL) err->set(err, CLASS_DUPEX, "Duplicate torrent");
    else err->set(err, CLASS_IOEX, "Failed to add torrent");
    return NULL;
  }

  d->torrentId = tr_torrentId(tor);

  if (tr_torrentHasMetadata(tor)) {
    std::string const filename = tr_torrentFilename(tor);
    d->path = strdup(filename.c_str());
  } else {
    std::lock_guard<std::mutex> lock(magnetMutex);
    magnetWaiters[d->torrentId] = d;
  }

  return NULL;
}

static void *torrentMagnetToTorrentFileCleanup(tr_session *session, void *data, Err *__unused err) {
  MagnetToTorrentData *d = (MagnetToTorrentData *) data;
  {
    std::lock_guard<std::mutex> lock(magnetMutex);
    magnetWaiters.erase(d->torrentId);
  }

  tr_torrent *tor = (d->torrentId > 0) ? tr_torrentFindFromId(session, d->torrentId) : NULL;

  if ((tor != NULL) && !d->enqueue) {
    tr_torrentRemove(tor, true, NULL, NULL);
    callStoppedCallback();
  }

  if (d->path != NULL) free(d->path);
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentMagnetToTorrentFile(
    JNIEnv *env, jclass __unused c, jlong jsession, jlong jsem,
    jstring jmagnet, jstring jpath, jint timeout, jbooleanArray enqueue) {
  const char *magnet = env->GetStringUTFChars(jmagnet, 0);
  sem_t *sem = (sem_t *) jsem;
  MagnetToTorrentData d = {0};
  d.sem = sem;
  d.magnet = magnet;

  runInTransmissionThreadEx(env, jsession, torrentMagnetToTorrentFile, &d);

  {
    int s = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    do {
      if (d.path != NULL) break;
    } while (((s = sem_timedwait(sem, &ts)) == -1) && errno == EINTR);

    if ((s == -1) && (d.path == NULL)) {
      time_t eot = ts.tv_sec;
      clock_gettime(CLOCK_REALTIME, &ts);

      if (ts.tv_sec >= eot) {
        throwEX(env, CLASS_IOEX, "Timed out while downloading magnet meta");
      } else {
        throwEX(env, CLASS_IOEX, "Failed to download meta data");
      }
    } else {
      jboolean isCopy;
      jboolean *enq = env->GetBooleanArrayElements(enqueue, &isCopy);
      d.enqueue = enq[0];
      env->ReleaseBooleanArrayElements(enqueue, enq, 0);

      if ((d.path != NULL) && !d.enqueue) {
        const char *path = env->GetStringUTFChars(jpath, 0);
        cp(env, d.path, path);
        env->ReleaseStringUTFChars(jpath, path);
      } else if (!d.enqueue) {
        throwEX(env, CLASS_IOEX, "Failed to download meta data");
      }
    }
  }

  CATCH:
  env->ReleaseStringUTFChars(jmagnet, magnet);
  runInTransmissionThread(__FILENAME__, __LINE__, env, jsession,
                          torrentMagnetToTorrentFileCleanup, &d);
}
// -------------------------------------------------------------------------------------------------

// ---------------------------------- torrentFindByHash --------------------------------------------
JNIEXPORT jint JNICALL
Java_com_ap_transmission_btc_Native_torrentFindByHash(
    JNIEnv *env, jclass __unused c, jlong jsession, jbyteArray torrentHash) {
  char hash[SHA_DIGEST_LENGTH];
  env->GetByteArrayRegion(torrentHash, 0, sizeof(hash), (jbyte *) hash);
  return (jint) (intptr_t) runInTransmissionThreadEx(env, jsession, findTorrentByHash, hash);
  CATCH:
  return -1;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentGetName ---------------------------------------------
static void *torrentGetName(tr_session *session, void *data, Err *err) {
  tr_torrent *tor = findTorrentByIdEx(session, (int) (intptr_t) data, err);
  return strdup(tr_torrentName(tor));
  CATCH:
  return NULL;
}

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_torrentGetName(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  jstring jname = NULL;
  char *name = (char *) runInTransmissionThreadEx(env, jsession, torrentGetName,
                                                  (void *) (intptr_t) torrentId);
  jname = env->NewStringUTF(name);
  free(name);
  CATCH:
  return jname;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------ torrentGetHash ---------------------------------------------
typedef struct GetHashData {
    jint torrentId;
    jbyte *hash;
} GetHashData;

static void *torrentGetHash(tr_session *session, void *data, Err *err) {
  GetHashData *d = (GetHashData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  memcpy(d->hash, tor->info_hash().data(), SHA_DIGEST_LENGTH);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentGetHash(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jbyteArray torrentHash) {
  GetHashData d = {torrentId};
  d.hash = env->GetByteArrayElements(torrentHash, 0);
  runInTransmissionThreadEx(env, jsession, torrentGetHash, &d);
  CATCH:
  env->ReleaseByteArrayElements(torrentHash, d.hash, 0);
}
// -------------------------------------------------------------------------------------------------

// ---------------------------------- torrentGetPieceHash ------------------------------------------
typedef struct GetPieceHashData {
    jint torrentId;
    jlong pieceIdx;
    jbyte *hash;
} GetPieceHashData;

static void *torrentGetPieceHash(tr_session *session, void *data, Err *err) {
  GetPieceHashData *d = (GetPieceHashData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);

  if ((d->pieceIdx >= 0) && ((tr_piece_index_t) d->pieceIdx < tor->piece_count())) {
    memcpy(d->hash, tor->piece_hash((tr_piece_index_t) d->pieceIdx).data(), SHA_DIGEST_LENGTH);
  } else {
    err->set(err, CLASS_IAEX, "Invalid piece index: %lld", (long long) d->pieceIdx);
  }

  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentGetPieceHash(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId,
    jlong pieceIdx, jbyteArray pieceHash) {
  GetPieceHashData d = {torrentId, pieceIdx};
  d.hash = env->GetByteArrayElements(pieceHash, 0);
  runInTransmissionThreadEx(env, jsession, torrentGetPieceHash, &d);
  CATCH:
  env->ReleaseByteArrayElements(pieceHash, d.hash, 0);
}
// -------------------------------------------------------------------------------------------------

// ---------------------------------- torrentSetPiecesHiPri ----------------------------------------
typedef struct SetPiecesHiPriData {
    jint torrentId;
    jlong firstPiece;
    jlong lastPiece;
} SetPiecesHiPriData;

static void *torrentSetPiecesHiPri(tr_session *session, void *data, Err *err) {
  SetPiecesHiPriData *d = (SetPiecesHiPriData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);

  if ((d->firstPiece >= 0) && ((tr_piece_index_t) d->firstPiece < tor->piece_count())) {
    setSequential(tor, true);
    tor->set_sequential_download_from_piece((tr_piece_index_t) d->firstPiece);
    if (!tor->is_running()) tr_torrentStart(tor);
  }

  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentSetPiecesHiPri(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId,
    jlong firstPiece, jlong lastPiece) {
  SetPiecesHiPriData d = {torrentId, firstPiece, lastPiece};
  runInTransmissionThreadEx(env, jsession, torrentSetPiecesHiPri, &d);
  CATCH:;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------- torrentFindFile -------------------------------------------
typedef struct FileData {
    jint torrentId;
    jint fileIdx;
    std::string result;
} FileData;

static void *torrentFindFile(tr_session *session, void *data, Err *err) {
  FileData *d = (FileData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  if (!checkFileIndex(tor, (uint32_t) d->fileIdx, true, err)) return NULL;
  d->result = tr_torrentFindFile(tor, (tr_file_index_t) d->fileIdx);
  CATCH:
  return NULL;
}

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_torrentFindFile(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jint fileIdx) {
  FileData d = {torrentId, fileIdx};
  runInTransmissionThreadEx(env, jsession, torrentFindFile, &d);
  if (d.result.empty()) return NULL;
  return env->NewStringUTF(d.result.c_str());
  CATCH:
  return NULL;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------------- torrentGetFileName ------------------------------------------
static void *torrentGetFileName(tr_session *session, void *data, Err *err) {
  FileData *d = (FileData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  if (!checkFileIndex(tor, (uint32_t) d->fileIdx, false, err)) return NULL;
  d->result = tr_torrentFile(tor, (tr_file_index_t) d->fileIdx).name;
  CATCH:
  return NULL;
}

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_torrentGetFileName(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jint fileIdx) {
  FileData d = {torrentId, fileIdx};
  runInTransmissionThreadEx(env, jsession, torrentGetFileName, &d);
  return env->NewStringUTF(d.result.c_str());
  CATCH:
  return NULL;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------------- torrentGetFileStat ------------------------------------------
typedef struct FileStatData {
    jint torrentId;
    jint fileIdx;
    std::vector<jlong> stat;
} FileStatData;

static void *torrentGetFileStat(tr_session *session, void *data, Err *err) {
  FileStatData *d = (FileStatData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  if (!checkFileIndex(tor, (uint32_t) d->fileIdx, false, err)) return NULL;

  {
    tr_file_view const f = tr_torrentFile(tor, (tr_file_index_t) d->fileIdx);
    tr_piece_index_t const pieceCount = tor->piece_count();
    tr_piece_index_t const first = f.beginPiece;
    tr_piece_index_t const last = (f.endPiece > f.beginPiece) ? (f.endPiece - 1) : f.beginPiece;
    uint64_t offset = 0;

    for (tr_file_index_t i = 0; i < (tr_file_index_t) d->fileIdx; i++) {
      offset += tr_torrentFile(tor, i).length;
    }

    std::vector<int8_t> tabs(pieceCount);
    tr_torrentAvailability(tor, tabs.data(), (int) pieceCount);

    size_t const filePieces = last - first + 1;
    size_t const fieldsCount = (filePieces + 63) / 64;
    d->stat.assign(fieldsCount + 6, 0);
    bool complete = true;

    for (size_t i = 6, t = first; i < d->stat.size(); i++) {
      jlong bf = 0;

      for (int n = 0; (n < 64) && (t <= last); n++, t++) {
        if ((t < pieceCount) && (tabs[t] == -1)) bf |= (1LL << n);
        else complete = false;
      }

      d->stat[i] = bf;
    }

    d->stat[0] = tor->piece_size();
    d->stat[1] = (jlong) f.length;
    d->stat[2] = (jlong) offset;
    d->stat[3] = first;
    d->stat[4] = last;
    d->stat[5] = !f.wanted ? 2 : complete ? 1 : 0;
  }

  CATCH:
  return NULL;
}

/**
 * Returns:
 *  jlongArray[0] = pieceLength
 *  jlongArray[1] = fileLength
 *  jlongArray[2] = fileOffset
 *  jlongArray[3] = firstPieceIndex
 *  jlongArray[4] = lastPieceIndex
 *  jlongArray[5] = fileComplete
 *  jlongArray[6...] = pieceBitFields
 */
JNIEXPORT jlongArray JNICALL
Java_com_ap_transmission_btc_Native_torrentGetFileStat(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jint fileIdx, jlongArray stat) {
  FileStatData d;
  d.torrentId = torrentId;
  d.fileIdx = fileIdx;
  runInTransmissionThreadEx(env, jsession, torrentGetFileStat, &d);

  if ((stat == NULL) || (env->GetArrayLength(stat) != (jsize) d.stat.size())) {
    stat = env->NewLongArray((jsize) d.stat.size());
  }

  env->SetLongArrayRegion(stat, 0, (jsize) d.stat.size(), d.stat.data());
  return stat;
  CATCH:
  return NULL;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------- torrentGetPiece -------------------------------------------
typedef struct GetPieceData {
    jint torrentId;
    jlong pieceIdx;
    jbyte *dst;
    jint offset;
    jint len;
} GetPieceData;

static void *torrentGetPiece(tr_session *session, void *data, Err *err) {
  GetPieceData *d = (GetPieceData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);

  if ((d->pieceIdx < 0) || ((tr_piece_index_t) d->pieceIdx >= tor->piece_count())) {
    err->set(err, CLASS_IAEX, "Invalid piece index: %lld", (long long) d->pieceIdx);
    return NULL;
  }

  if (session->cache->read_block(*tor, tor->piece_loc((tr_piece_index_t) d->pieceIdx, (uint32_t) d->offset),
                                 (size_t) d->len, (uint8_t *) d->dst) != 0) {
    err->set(err, CLASS_IOEX, "Failed to get piece %lld: offset=%d, length=%d",
             (long long) d->pieceIdx, d->offset, d->len);
  }

  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentGetPiece(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jlong pieceIdx,
    jbyteArray dst, jint offset, jint len) {
  GetPieceData d = {torrentId, pieceIdx, env->GetByteArrayElements(dst, 0), offset, len};
  runInTransmissionThreadEx(env, jsession, torrentGetPiece, &d);
  CATCH:
  env->ReleaseByteArrayElements(dst, d.dst, 0);
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------- torrentStatBrief ------------------------------------------
typedef struct StatBriefData {
    std::vector<jlong> stat;
} StatBriefData;

/*
 * Returns:
 * stat[0] - torrentId
 * stat[1] - status
 * stat[2] - progress
 * stat[3] - totalLength
 * stat[4] - remainingLength
 * stat[5] - uploadedLength
 * stat[6] - peersUp
 * stat[7] - peersDown
 * stat[8] - speedUp
 * stat[9] - speedDown
 */
static void *torrentStatBrief(tr_session *session, void *data, __unused Err *err) {
  StatBriefData *d = (StatBriefData *) data;
  d->stat.assign(std::size(session->torrents()) * 10, 0);
  size_t i = 0;

  for (auto *tor : session->torrents()) {
    tr_stat const *st = tr_torrentStat(tor);
    jlong *s = &d->stat[i];
    i += 10;

    s[0] = tr_torrentId(tor);
    s[3] = (jlong) st->sizeWhenDone;
    s[4] = (jlong) st->leftUntilDone;
    s[5] = (jlong) st->uploadedEver;

    if (st->error == TR_STAT_LOCAL_ERROR) {
      s[1] = 4;
      s[2] = 0;
      continue;
    }

    switch (st->activity) {
      case TR_STATUS_STOPPED:
        s[1] = 0;
        s[2] = (jlong) (st->percentDone * 100);
        continue;
      case TR_STATUS_CHECK_WAIT:
      case TR_STATUS_CHECK:
        s[1] = 1;
        s[2] = (jlong) (st->recheckProgress * 100);
        break;
      case TR_STATUS_DOWNLOAD_WAIT:
      case TR_STATUS_DOWNLOAD:
        s[1] = 2;
        s[2] = (jlong) (st->percentDone * 100);
        break;
      case TR_STATUS_SEED_WAIT:
      case TR_STATUS_SEED:
        s[1] = 3;
        s[2] = (jlong) (st->percentDone * 100);
        break;
    }

    s[6] = st->peersGettingFromUs;
    s[7] = st->peersSendingToUs + st->webseedsSendingToUs;
    s[8] = (jlong) (st->pieceUploadSpeed_KBps * 1000);
    s[9] = (jlong) (st->pieceDownloadSpeed_KBps * 1000);
  }

  return NULL;
}

JNIEXPORT jlongArray JNICALL
Java_com_ap_transmission_btc_Native_torrentStatBrief(
    JNIEnv *env, jclass __unused c, jlong jsession, jlongArray jstat) {
  StatBriefData d;
  runInTransmissionThreadEx(env, jsession, torrentStatBrief, &d);

  if ((jstat == NULL) || (env->GetArrayLength(jstat) != (jsize) d.stat.size())) {
    jstat = env->NewLongArray((jsize) d.stat.size());
  }

  env->SetLongArrayRegion(jstat, 0, (jsize) d.stat.size(), d.stat.data());
  return jstat;
  CATCH:
  return jstat;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------- torrentGetError -------------------------------------------
static void *torrentGetError(tr_session *session, void *data, Err *err) {
  tr_torrent *tor = findTorrentByIdEx(session, (int) (intptr_t) data, err);
  return strdup(tr_torrentStat(tor)->errorString);
  CATCH:
  return NULL;
}

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_torrentGetError(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId) {
  char *err = (char *) runInTransmissionThreadEx(env, jsession, torrentGetError,
                                                 (void *) (intptr_t) torrentId);
  {
    jstring jerr = env->NewStringUTF(err);
    free(err);
    return jerr;
  }
  CATCH:
  return NULL;
}
// -------------------------------------------------------------------------------------------------

// ------------------------------------- torrentSetDnd ---------------------------------------------
typedef struct SetDndData {
    bool dnd;
    jint torrentId;
    const tr_file_index_t *files;
    tr_file_index_t fileCount;
} SetDndData;

static void *torrentSetDnd(tr_session *session, void *data, Err *err) {
  SetDndData *d = (SetDndData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  tr_torrentSetFileDLs(tor, d->files, d->fileCount, !d->dnd);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentSetDnd(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jintArray files, jboolean dnd) {
  SetDndData d;
  d.dnd = dnd;
  d.torrentId = torrentId;
  d.files = (tr_file_index_t *) env->GetIntArrayElements(files, 0);
  d.fileCount = (tr_file_index_t) env->GetArrayLength(files);
  runInTransmissionThreadEx(env, jsession, torrentSetDnd, &d);
  CATCH:
  env->ReleaseIntArrayElements(files, (jint *) d.files, 0);
}
// -------------------------------------------------------------------------------------------------

// ---------------------------------- torrentSetLocation -------------------------------------------
typedef struct SetLocationData {
    jint torrentId;
    const char *path;
} SetLocationData;

static void *torrentSetLocation(tr_session *session, void *data, Err *err) {
  SetLocationData *d = (SetLocationData *) data;
  tr_torrent *tor = findTorrentByIdEx(session, d->torrentId, err);
  tr_torrentSetLocation(tor, d->path, true, NULL);
  CATCH:
  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_torrentSetLocation(
    JNIEnv *env, jclass __unused c, jlong jsession, jint torrentId, jstring jpath) {
  jboolean isCopy;
  const char *path = env->GetStringUTFChars(jpath, &isCopy);
  SetLocationData d = {torrentId, path};
  runInTransmissionThreadEx(env, jsession, torrentSetLocation, &d);
  CATCH:
  env->ReleaseStringUTFChars(jpath, path);
}
// -------------------------------------------------------------------------------------------------
} // extern "C"
