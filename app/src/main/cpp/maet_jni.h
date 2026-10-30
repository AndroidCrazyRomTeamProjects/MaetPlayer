#include <jni.h>

#ifndef _MAET_JNI_H
#define _MAET_JNI_H

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jstring JNICALL
Java_org_crazyromteam_maetplayer_MaetDecoder_decodeSpi(
        JNIEnv *env,
        jobject thiz,
        jstring spiPath,
        jstring outPath);

JNIEXPORT jboolean JNICALL
Java_org_crazyromteam_maetplayer_MaetDecoder_previewSpi(
        JNIEnv *env,
        jobject thiz,
        jstring spiPath,
        jobject surface);

#ifdef __cplusplus
}
#endif

#endif // _MAET_JNI_H
