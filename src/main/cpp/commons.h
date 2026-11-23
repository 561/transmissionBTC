#ifndef TRANSMISSIONBTC_COMMONS_H
#define TRANSMISSIONBTC_COMMONS_H

#include <jni.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <android/log.h>
#include <libtransmission/transmission.h>

extern "C" {

#define LOG_TAG "transmissionbtc"
#define CLASS_IOEX "java/io/IOException"
#define CLASS_IAEX "java/lang/IllegalArgumentException"
#define CLASS_NSTEX "com/ap/transmission/btc/torrent/NoSuchTorrentException"
#define CLASS_DUPEX "com/ap/transmission/btc/torrent/DuplicateTorrentException"

#define CATCH __onException__

#define logErr(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define throwEX(env, className, ...) { throwException(__FILENAME__, __LINE__, env, className, __VA_ARGS__); goto CATCH; }
#define throwIOEX(env, ...) throwEX(env, CLASS_IOEX, __VA_ARGS__)
#define throwOrLog(env, className, throw, ...) \
      if (throw) { throwEX(env, className, __VA_ARGS__); } \
      else { logErr(__VA_ARGS__); goto CATCH; }

typedef struct Err {
    bool isSet;

    void (*set)(struct Err *err, const char *ex, const char *msg, ...);
} Err;
#define errCheck(err) if (err->isSet) goto CATCH

jint throwException(const char *, int, JNIEnv *, const char *, const char *, ...);

size_t cp(JNIEnv *env, const char *, const char *);

#define SHA_DIGEST_LENGTH 20

tr_ctor *ctorFromFile(JNIEnv *, jlong, jstring, bool);

void *findTorrentByHash(tr_session *session, void *hash, Err *err);

#define findTorrentByIdEx(session, id, err) findTorrentById(session,  id, err); errCheck(err)

tr_torrent *findTorrentById(tr_session *session, int id, Err *err);

bool checkFileIndex(tr_torrent *tor, uint32_t idx, bool wanted, Err *err);

void torrentMetadataReceived(tr_torrent *tor);

#define runInTransmissionThreadEx(env, jsession, func, data) \
  runInTransmissionThread(__FILENAME__, __LINE__, env, jsession, func, data);\
  if (env->ExceptionCheck()) goto CATCH

void *runInTransmissionThread(const char *file, int line, JNIEnv *env, jlong jsession,
                              void *(*func)(tr_session *session, void *userData, Err *err),
                              void *userData);

} // extern "C"
#endif //TRANSMISSIONBTC_COMMONS_H
