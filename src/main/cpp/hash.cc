#include "commons.h"
#include "transmission-private.h"

extern "C" {
JNIEXPORT jint JNICALL
Java_com_ap_transmission_btc_Native_hashLength(JNIEnv *__unused env, jclass __unused c) {
  return SHA_DIGEST_LENGTH;
}

JNIEXPORT jstring JNICALL
Java_com_ap_transmission_btc_Native_hashBytesToString(
    JNIEnv *env, jclass __unused c, jbyteArray jhash) {
  tr_sha1_digest_t digest;
  env->GetByteArrayRegion(jhash, 0, SHA_DIGEST_LENGTH, (jbyte *) digest.data());
  auto const hashString = tr_sha1_to_string(digest);
  return env->NewStringUTF(hashString.c_str());
}

JNIEXPORT jbyteArray JNICALL
Java_com_ap_transmission_btc_Native_hashStringToBytes(
    JNIEnv *env, jclass __unused c, jstring jhashString) {
  const char *hashString = env->GetStringUTFChars(jhashString, 0);
  auto const digest = tr_sha1_from_string(hashString);
  env->ReleaseStringUTFChars(jhashString, hashString);
  jbyteArray jhash = env->NewByteArray(SHA_DIGEST_LENGTH);
  if (digest) env->SetByteArrayRegion(jhash, 0, SHA_DIGEST_LENGTH, (const jbyte *) digest->data());
  return jhash;
}

JNIEXPORT jbyteArray JNICALL
Java_com_ap_transmission_btc_Native_hashGetTorrentHash(
    JNIEnv *env, jclass __unused c, jstring torrentPath) {
  const char *path = env->GetStringUTFChars(torrentPath, 0);
  tr_torrent_metainfo tm;
  bool const ok = tm.parse_torrent_file(path);

  if (!ok) {
    throwException(__FILENAME__, __LINE__, env, CLASS_IOEX, "Failed to parse torrent file: %s", path);
    env->ReleaseStringUTFChars(torrentPath, path);
    return NULL;
  }

  env->ReleaseStringUTFChars(torrentPath, path);
  jbyteArray jhash = env->NewByteArray(SHA_DIGEST_LENGTH);
  env->SetByteArrayRegion(jhash, 0, SHA_DIGEST_LENGTH, (const jbyte *) tm.info_hash().data());
  return jhash;
}
} // extern "C"
