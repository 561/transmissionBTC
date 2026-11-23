#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "commons.h"
#include "native_to_java.h"
#include "transmission-private.h"
#include <libtransmission/version.h>
#include <libtransmission/variant.h>
#include <libtransmission/file.h>
#include <libtransmission/utils.h>

static std::string configDirPath;
static std::set<std::string> suspendedHashes;

static std::string suspendedFile() {
  return configDirPath + "/suspended.txt";
}

static void loadSuspended() {
  suspendedHashes.clear();
  std::ifstream in(suspendedFile());
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) suspendedHashes.insert(line);
  }
}

static void saveSuspended() {
  if (suspendedHashes.empty()) {
    remove(suspendedFile().c_str());
    return;
  }

  std::ofstream out(suspendedFile(), std::ios::trunc);
  for (auto const &hash : suspendedHashes) out << hash << "\n";
}

static void sessionSuspend(tr_session *session, bool suspend) {
  tr_sessionSetPaused(session, suspend);

  if (suspend) {
    for (auto *tor : session->torrents()) {
      if (tor->is_running()) {
        suspendedHashes.insert(std::string{tor->info_hash_string()});
        tr_torrentStop(tor);
      }
    }
  } else {
    for (auto *tor : session->torrents()) {
      if (suspendedHashes.count(std::string{tor->info_hash_string()}) != 0) tr_torrentStart(tor);
    }

    suspendedHashes.clear();
  }

  saveSuspended();
}

static void saveSessionSettings(tr_session *session) {
  tr_sessionSaveSettings(session, tr_sessionGetConfigDir(session), tr_variant::make_map());
}

static void metadataFunc(tr_session *__unused session, tr_torrent *__unused tor, void *__unused data) {
  torrentMetadataReceived(tor);
  callAddedOrChangedCallback();
}

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_transmissionVersion(JNIEnv *env, jclass __unused c) {
  return env->NewStringUTF(SHORT_VERSION_STRING);
}

// ----------------------------------------- Start -------------------------------------------------

static tr_rpc_callback_status rpcFunc(tr_session *session, tr_rpc_callback_type type,
                                      struct tr_torrent *__unused tor,
                                      void *__unused user_data) {
  switch (type) {
    case TR_RPC_TORRENT_ADDED:
    case TR_RPC_TORRENT_STARTED:
    case TR_RPC_TORRENT_MOVED:
    case TR_RPC_TORRENT_CHANGED:
      callAddedOrChangedCallback();
      break;
    case TR_RPC_TORRENT_STOPPED:
    case TR_RPC_TORRENT_REMOVING:
    case TR_RPC_TORRENT_TRASHING:
      callStoppedCallback();
      break;
    case TR_RPC_SESSION_CHANGED:
      saveSessionSettings(session);
      callSessionChangedCallback();
      break;
    default:
      break;
  }

  return TR_RPC_OK;
}

static void altSpeedFunc(tr_session *__unused session, bool __unused active, bool userDriven,
                         void *__unused data) {
  if (!userDriven) callScheduledAltSpeedCallback();
}

static void *transmissionStart(tr_session *session, void *data, Err *__unused err) {
  jboolean suspend = (jboolean) (intptr_t) data;
  tr_sessionSetPaused(session, false);
  tr_sessionSetRPCCallback(session, rpcFunc, NULL);
  tr_sessionSetAltSpeedFunc(session, altSpeedFunc, NULL);
  tr_sessionSetMetadataCallback(session, metadataFunc, NULL);

  tr_ctor *ctor = tr_ctorNew(session);
  tr_sessionLoadTorrents(session, ctor);
  tr_ctorFree(ctor);
  tr_ioSetSuspended(suspend);
  tr_sessionRefreshDownloadLimit(session);
  loadSuspended();
  sessionSuspend(session, suspend);
  return NULL;
}

JNIEXPORT jlong JNICALL
Java_com_ap_transmission_btc_Native_transmissionStart(
    JNIEnv *env, jclass __unused c,
    jstring jconfigDir, jstring jdownloadsDir,
    jint encrMode,
    jboolean enableRpc, int rpcPort,
    jboolean enableAuth, jstring jusername, jstring jpassword,
    jboolean enableRpcWhitelist, jstring jrpcWhitelist,
    jboolean loadConfig, jboolean enableSequential, jboolean suspend) {
  tr_session *session;
  tr_lib_init();
  const char *configDir = env->GetStringUTFChars(jconfigDir, 0);
  configDirPath = configDir;
  tr_variant settings = loadConfig ? tr_sessionLoadSettings(configDir) : tr_variant::make_map();

  if (!settings.holds_alternative<tr_variant::Map>()) {
    logErr("Failed to load config: %s/settings.json", configDir);
    settings = tr_variant::make_map();
    loadConfig = false;
  }

  if (!loadConfig) {
    tr_variantDictAddBool(&settings, TR_KEY_rename_partial_files, false);
    tr_variantDictAddBool(&settings, TR_KEY_peer_port_random_on_start, true);
  }

  const char *downloadsDir = env->GetStringUTFChars(jdownloadsDir, 0);
  tr_variantDictAddStr(&settings, TR_KEY_download_dir, downloadsDir);
  env->ReleaseStringUTFChars(jdownloadsDir, downloadsDir);

  tr_variantDictAddInt(&settings, TR_KEY_encryption, encrMode);
  tr_variantDictAddInt(&settings, TR_KEY_preallocation, 0);
  tr_variantDictAddBool(&settings, TR_KEY_sequential_download, enableSequential);

  if (enableRpc) {
    tr_variantDictAddBool(&settings, TR_KEY_rpc_enabled, true);
    tr_variantDictAddInt(&settings, TR_KEY_rpc_port, rpcPort);

    if (enableAuth) {
      const char *username = env->GetStringUTFChars(jusername, 0);
      const char *password = env->GetStringUTFChars(jpassword, 0);
      tr_variantDictAddStr(&settings, TR_KEY_rpc_username, username);
      tr_variantDictAddStr(&settings, TR_KEY_rpc_password, password);
      env->ReleaseStringUTFChars(jusername, username);
      env->ReleaseStringUTFChars(jpassword, password);
      tr_variantDictAddBool(&settings, TR_KEY_rpc_authentication_required, true);
    } else {
      tr_variantDictAddBool(&settings, TR_KEY_rpc_authentication_required, false);
    }

    if (enableRpcWhitelist) {
      const char *rpcWhitelist = env->GetStringUTFChars(jrpcWhitelist, 0);
      tr_variantDictAddStr(&settings, TR_KEY_rpc_whitelist, rpcWhitelist);
      env->ReleaseStringUTFChars(jrpcWhitelist, rpcWhitelist);
      tr_variantDictAddBool(&settings, TR_KEY_rpc_whitelist_enabled, true);
    } else {
      tr_variantDictAddBool(&settings, TR_KEY_rpc_whitelist_enabled, false);
    }
  } else {
    tr_variantDictAddBool(&settings, TR_KEY_rpc_enabled, false);
  }

  session = tr_sessionInit(configDir, true, settings);
  tr_sessionSaveSettings(session, configDir, tr_variant::make_map());
  env->ReleaseStringUTFChars(jconfigDir, configDir);

  jlong jsession = (jlong) session;
  runInTransmissionThreadEx(env, jsession, transmissionStart, (void *) (intptr_t) suspend);
  return jsession;

  CATCH:
  return 0;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------------------- Stop --------------------------------------------------
JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_transmissionStop(
    JNIEnv *__unused env, jclass __unused c, jlong jsession, jstring jconfigDir) {
  tr_session *session = (tr_session *) jsession;
  tr_sessionSetRPCCallback(session, NULL, NULL);
  tr_sessionSetMetadataCallback(session, NULL, NULL);

  const char *configDir = env->GetStringUTFChars(jconfigDir, 0);
  tr_sessionSaveSettings(session, configDir, tr_variant::make_map());
  env->ReleaseStringUTFChars(jconfigDir, configDir);

  tr_sessionClose(session);
}
// -------------------------------------------------------------------------------------------------

// --------------------------------------- Suspend -------------------------------------------------
static std::vector<int> suspendedChecks;

static void *transmissionSuspend(tr_session *session, void *data, Err *__unused err) {
  bool suspend = (bool) data;
  tr_sessionRefreshDownloadLimit(session);

  if (suspend) {
    for (auto *tor : session->torrents()) {
      tr_torrent_activity activity = tor->activity();

      if (activity == TR_STATUS_CHECK || activity == TR_STATUS_CHECK_WAIT) {
        suspendedChecks.push_back(tr_torrentId(tor));
        tr_torrentStop(tor);
      }
    }
  } else {
    tr_sessionClearPeerFailures(session);
    sessionSuspend(session, false);
    for (int id : suspendedChecks) {
      tr_torrent *tor = tr_torrentFindFromId(session, id);
      if (tor != nullptr) tr_torrentVerify(tor);
    }

    suspendedChecks.clear();
  }

  return NULL;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_transmissionSuspend(
    JNIEnv *__unused env, jclass __unused c, jlong jsession, jboolean suspend) {
  tr_ioSetSuspended(suspend);
  runInTransmissionThreadEx(env, jsession, transmissionSuspend, (void *) (intptr_t) suspend);
  CATCH:;
}

JNIEXPORT void JNICALL
Java_com_ap_transmission_btc_Native_ioSetSuspended(
    JNIEnv *__unused env, jclass __unused c, jboolean suspend) {
  tr_ioSetSuspended(suspend);
}
// -------------------------------------------------------------------------------------------------

// -------------------------------- HasDownloadingTorrents -----------------------------------------
static void *
transmissionHasDownloadingTorrents(tr_session *session, void *__unused data, Err *__unused err) {
  for (auto *tor : session->torrents()) {
    switch (tor->activity()) {
      case TR_STATUS_DOWNLOAD:
      case TR_STATUS_DOWNLOAD_WAIT:
      case TR_STATUS_CHECK:
      case TR_STATUS_CHECK_WAIT:
        return (void *) JNI_TRUE;
      default:
        continue;
    }
  }

  return (void *) JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ap_transmission_btc_Native_transmissionHasDownloadingTorrents(
    JNIEnv *__unused env, jclass __unused c, jlong jsession) {
  return (jboolean) (intptr_t) runInTransmissionThreadEx(env, jsession,
                                                         transmissionHasDownloadingTorrents, NULL);
  CATCH:
  return JNI_FALSE;
}
// -------------------------------------------------------------------------------------------------

static void *transmissionPeerSocketStats(tr_session *session, void *data, Err *__unused err) {
  int *stats = (int *) data;
  tr_sessionGetPeerSocketStats(session, &stats[0], &stats[1], &stats[2], &stats[3]);
  return NULL;
}

JNIEXPORT jintArray JNICALL
Java_com_ap_transmission_btc_Native_transmissionPeerSocketStats(
    JNIEnv *env, jclass __unused c, jlong jsession) {
  jint stats[4] = {0, 0, 0, 0};
  jintArray result;
  runInTransmissionThreadEx(env, jsession, transmissionPeerSocketStats, stats);
  result = env->NewIntArray(4);
  if (result != NULL) env->SetIntArrayRegion(result, 0, 4, stats);
  return result;
  CATCH:
  return NULL;
}

// ------------------------------------- ListTorrentNames ------------------------------------------
typedef struct ListTorrentsData {
    int count;
    char **torrents;
} ListTorrentsData;

static void *
transmissionListTorrentNames(tr_session *session, void *data, Err *__unused err) {
  ListTorrentsData *d = (ListTorrentsData *) data;
  d->count = (int) std::size(session->torrents());
  if (d->count == 0) return NULL;
  d->torrents = (char **) malloc(d->count * (sizeof(char *)));
  int i = 0;

  for (auto *tor : session->torrents()) {
    std::string const hash{tor->info_hash_string()};
    char const *name = tr_torrentName(tor);
    size_t lineLen = hash.size() + strlen(name) + 16;
    char *line = (char *) malloc(lineLen);
    snprintf(line, lineLen, "%d %s %s", tr_torrentId(tor), hash.c_str(), name);
    d->torrents[i++] = line;
  }

  return NULL;
}

JNIEXPORT jobjectArray JNICALL
Java_com_ap_transmission_btc_Native_transmissionListTorrentNames(
    JNIEnv *__unused env, jclass __unused c, jlong jsession) {
  ListTorrentsData d = {0, NULL};
  jobjectArray result = NULL;
  runInTransmissionThreadEx(env, jsession, transmissionListTorrentNames, &d);

  if (d.count == 0) return NULL;
  result = env->NewObjectArray(d.count, env->FindClass("java/lang/String"), NULL);

  for (int i = 0; i < d.count; i++) {
    jobject jname = env->NewStringUTF(d.torrents[i]);
    env->SetObjectArrayElement(result, i, jname);
    env->DeleteLocalRef(jname);
    free(d.torrents[i]);
  }

  free(d.torrents);
  CATCH:
  return result;
}
// -------------------------------------------------------------------------------------------------

// ----------------------------------- GetEncryptionMode -------------------------------------------
JNIEXPORT jint JNICALL
Java_com_ap_transmission_btc_Native_transmissionGetEncryptionMode(
    JNIEnv *__unused env, jclass __unused c, jlong jsession) {
  return tr_sessionGetEncryption((tr_session *) jsession);
}
// -------------------------------------------------------------------------------------------------

} //extern "C"