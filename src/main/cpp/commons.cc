#include "commons.h"
#include <stdio.h>
#include "transmission-private.h"

extern "C" {

jint throwException(const char *file, int line, JNIEnv *env, const char *className,
										const char *format, ...) {
	int len;
	char msg[256];
	char *fmt = (char *) format;

#ifndef NDEBUG
	char dfmt[1024];
	len = snprintf(dfmt, sizeof(dfmt), "[%s:%d] %s", file, line, format);
	if (len > 0) fmt = dfmt;
#endif

	va_list args;
	va_start(args, format);
	len = vsnprintf(msg, sizeof(msg), (const char *) fmt, args);
	va_end(args);
	if (len > 0) fmt = msg;

	jclass c = env->FindClass(className);
	if (c == NULL) env->FindClass("java/lang/Error");

	if (env->ExceptionCheck()) {
		logErr("Exception: %s", fmt);
	} else {
		env->ThrowNew(c, fmt);
	}

	return 0;
}

size_t cp(JNIEnv *env, const char *fromPath, const char *toPath) {
	FILE *from = NULL, *to = NULL;
	size_t count = 0;

	if ((from = fopen(fromPath, "rb")) == NULL) {
		throwIOEX(env, "Failed to open source file %s", fromPath);
	}

	if ((to = fopen(toPath, "wb")) == NULL) {
		fclose(from);
		throwIOEX(env, "Failed to open destination file %s", toPath);
	}

	size_t n;
	char buffer[BUFSIZ];

	while ((n = fread(buffer, sizeof(char), sizeof(buffer), from)) > 0) {
		if (fwrite(buffer, sizeof(char), n, to) != n) {
			throwIOEX(env, "Error writing to destination file %s", toPath);
		} else {
			count += n;
		}
	}

	CATCH:
	if (from != NULL) fclose(from);
	if (to != NULL) fclose(to);
	return count;
}

tr_ctor *ctorFromFile(JNIEnv *env, jlong jsession, jstring jpath, bool throwErr) {
	tr_session *session = (tr_session *) jsession;
	const char *path = env->GetStringUTFChars(jpath, 0);
	tr_ctor *ctor = tr_ctorNew(session);

	if (!tr_ctorSetMetainfoFromFile(ctor, path, nullptr)) {
		tr_ctorFree(ctor);
		ctor = NULL;
		throwOrLog(env, CLASS_IOEX, throwErr, "Invalid torrent file: %s", path);
	}

	CATCH:
	env->ReleaseStringUTFChars(jpath, path);
	return ctor;
}

void *findTorrentByHash(tr_session *session, void *hash, Err *err) {
	tr_sha1_digest_t digest;
	memcpy(digest.data(), hash, SHA_DIGEST_LENGTH);
	tr_torrent *tor = session->torrents().get(digest);

	if (tor == NULL) {
		auto const hashString = tr_sha1_to_string(digest);
		err->set(err, CLASS_NSTEX, "No such torrent: hash=%s", hashString.c_str());
		return (void *) (intptr_t) -1;
	}

	return (void *) (intptr_t) tr_torrentId(tor);
}

tr_torrent *findTorrentById(tr_session *session, int id, Err *err) {
	tr_torrent *tor = tr_torrentFindFromId(session, id);
	if (tor == NULL) err->set(err, CLASS_NSTEX, "No such torrent: id=%d", id);
	return tor;
}

bool checkFileIndex(tr_torrent *tor, uint32_t idx, bool wanted, Err *err) {
	if (idx >= tr_torrentFileCount(tor)) {
		err->set(err, CLASS_IAEX, "Invalid file index: %d", idx);
		return false;
	}

	if (wanted) {
		tr_file_view const f = tr_torrentFile(tor, idx);
		if (!f.wanted) {
			err->set(err, CLASS_IAEX, "File #%d is unwanted for download: %s", idx, f.name);
			return false;
		}
	}

	return true;
}

struct Future {
	Err err;
	sem_t sem;
	tr_session *session;
	void *userData;
	void *result;
	const char *ex;
	char *exMsg;
	uint16_t exMsgBufLen;

	void *(*func)(tr_session *session, void *userData, Err *err);
};

static void setError(struct Err *err, const char *ex, const char *msg, ...) {
	struct Future *f = (struct Future *) err;

	if (f->exMsg == NULL) {
		f->exMsgBufLen = 512;
		f->exMsg = (char *) calloc(f->exMsgBufLen, sizeof(char));
	}

	va_list args;
	va_start(args, msg);
	vsnprintf(f->exMsg, f->exMsgBufLen, msg, args);
	va_end(args);
	f->ex = ex;
	f->err.isSet = true;
}

static void runInEventThread(void *data) {
	struct Future *f = (struct Future *) data;
	f->result = f->func(f->session, f->userData, &(f->err));
	sem_post(&(f->sem));
}

void *runInTransmissionThread(const char *file, int line, JNIEnv *env, jlong jsession,
															void *(*func)(tr_session *, void *, Err *),
															void *userData) {
	struct Future f;
	sem_t *sem = &(f.sem);
	memset(sem, 0, sizeof(sem_t));
	sem_init(sem, 0, 0);
	f.session = (tr_session *) jsession;
	f.userData = userData;
	f.ex = NULL;
	f.exMsg = NULL;
	f.err.isSet = false;
	f.err.set = setError;
	f.func = func;

	f.session->run_in_session_thread(runInEventThread, &f);
	while ((sem_wait(sem) == -1) && (errno == EINTR));
	sem_destroy(sem);

	if (f.err.isSet) {
		throwException(file, line, env, f.ex, f.exMsg);
		free(f.exMsg);
		return NULL;
	} else {
		return f.result;
	}
}
} // extern "C"