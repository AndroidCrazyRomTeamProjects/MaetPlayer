#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <string.h>

#define LOG_TAG "MAET-BRIDGE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Reverse Engineered Structures from Samsung lpm & libmaet.so
// ---------------------------------------------------------------------------

// The Bitstream buffer (bitb) descriptor passed to maetd_decode in lpm:
// +0x20: uint8_t* payload
// +0x30: uint32_t byte_size
typedef struct {
    uint8_t  pad[0x20];
    uint8_t* data;       // +0x20
    uint8_t  pad2[0x08]; // +0x28
    uint32_t size;       // +0x30: Chunk length in bytes
    uint8_t  pad3[0x0C];
} BitbDesc;

// The image clone descriptor (0x150 bytes) used by maetd_clone in lpm:
// +0x20: int width
// +0x24: int height
// +0x30: int colorspace (500 = RGBA8888)
// +0x34: int stride (width * 4)
// +0x44: int height
// +0x58: void* buffer (RGBA pixels)
typedef struct {
    uint8_t  pad[0x20];
    int32_t  width;      // +0x20
    int32_t  height;     // +0x24
    uint8_t  pad2[0x08]; // +0x28
    int32_t  colorspace; // +0x30 (500)
    int32_t  stride;     // +0x34 (width * 4)
    uint8_t  pad3[0x0C]; // +0x38
    int32_t  height2;    // +0x44
    uint8_t  pad4[0x10]; // +0x48
    void*    buffer;     // +0x58 (output pixel buffer)
    uint8_t  pad5[0xF0];
} ImgbDesc;

// Function pointers to libmaet.so
static void* handle_maet       = nullptr;
static void* handle_sxqk       = nullptr;
static void* handle_sxqk_skia  = nullptr;

static int   (*maet_init)   (void)                                      = nullptr;
static int*  (*maetd_create)(uint64_t*, int*)                           = nullptr;
static void  (*maetd_delete)(int*)                                      = nullptr;
static int   (*maetd_decode)(int*, void*, int*)                         = nullptr;
static int   (*maetd_config)(int*, int, uint32_t*, int*)                = nullptr;
static int   (*maetd_clone) (int*, void*)                               = nullptr;

static void ensure_libs() {
    if (handle_maet) return;

    handle_sxqk_skia = dlopen("libsxqk_skia.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle_sxqk_skia) {
        LOGE("Failed to load libsxqk_skia.so: %s", dlerror());
    }

    handle_sxqk = dlopen("libsxqk.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle_sxqk) {
        LOGE("Failed to load libsxqk.so: %s", dlerror());
    }

    handle_maet = dlopen("libmaet.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle_maet) {
        maet_init    = (int(*)(void))dlsym(handle_maet, "maet_init");
        maetd_create = (int*(*)(uint64_t*, int*))dlsym(handle_maet, "maetd_create");
        maetd_delete = (void(*)(int*))dlsym(handle_maet, "maetd_delete");
        maetd_decode = (int(*)(int*, void*, int*))dlsym(handle_maet, "maetd_decode");
        maetd_config = (int(*)(int*, int, uint32_t*, int*))dlsym(handle_maet, "maetd_config");
        maetd_clone  = (int(*)(int*, void*))dlsym(handle_maet, "maetd_clone");
        LOGI("libmaet.so symbols resolved successfully");
    } else {
        LOGE("Failed to load libmaet.so: %s", dlerror());
    }
}

// Internal helper to decode SPI into an RGBA buffer
static bool decode_spi_internal(const char* spiPath, uint8_t*& out_rgba, int& out_w, int& out_h, int& out_chunks) {
    ensure_libs();
    if (!maet_init || !maetd_create || !maetd_decode || !maetd_config || !maetd_clone) {
        LOGE("Required libmaet symbols missing");
        return false;
    }

    if (maet_init() != 0) {
        LOGE("maet_init failed");
        return false;
    }

    int fd = open(spiPath, O_RDONLY);
    if (fd < 0) {
        LOGE("Could not open file: %s", spiPath);
        return false;
    }

    struct stat st;
    fstat(fd, &st);
    size_t file_size = (size_t)st.st_size;
    if (file_size < 8) {
        close(fd);
        LOGE("File too small: %zu bytes", file_size);
        return false;
    }

    uint8_t* file_buf = (uint8_t*)malloc(file_size);
    if (!file_buf) {
        close(fd);
        return false;
    }
    read(fd, file_buf, file_size);
    close(fd);

    uint64_t param_1[2] = {1, 0};
    int* ctx = maetd_create(param_1, 0);
    if (!ctx) {
        free(file_buf);
        LOGE("maetd_create returned NULL");
        return false;
    }

    size_t offset = 0;
    out_chunks = 0;
    int stat[8] = { 0 };

    while (offset + 4 <= file_size) {
        uint32_t chunk_size = *(uint32_t*)(file_buf + offset);
        if (offset + 4 + chunk_size > file_size) {
            LOGI("Remaining trailing bytes: %zu (chunk claims %u)", file_size - offset, chunk_size);
            break;
        }

        uint8_t* chunk_payload = file_buf + offset + 4;
        BitbDesc bitb;
        memset(&bitb, 0, sizeof(bitb));
        bitb.data = chunk_payload;
        bitb.size = chunk_size;

        int ret = maetd_decode(ctx, &bitb, stat);
        LOGI("Decoded chunk #%d (size %u): ret=%d", out_chunks, chunk_size, ret);
        out_chunks++;
        offset += 4 + chunk_size;
    }

    int width = 0, height = 0, scheme = 0;
    int size4 = 4;
    maetd_config(ctx, 0xc9, (uint32_t*)&width, &size4);
    size4 = 4;
    maetd_config(ctx, 0xca, (uint32_t*)&height, &size4);
    size4 = 4;
    maetd_config(ctx, 0x19d, (uint32_t*)&scheme, &size4);

    LOGI("Queried dimensions: %dx%d, scheme=%d", width, height, scheme);

    bool ok = false;
    if (width > 0 && height > 0) {
        out_w = width;
        out_h = height;
        size_t rgba_size = (size_t)width * height * 4;
        out_rgba = (uint8_t*)malloc(rgba_size);

        if (out_rgba) {
            memset(out_rgba, 0, rgba_size);
            ImgbDesc imgb;
            memset(&imgb, 0, sizeof(imgb));
            imgb.width      = width;
            imgb.height     = height;
            imgb.height2    = height;
            imgb.colorspace = 500; // RGBA8888
            imgb.stride     = width * 4;
            imgb.buffer     = out_rgba;

            int clone_ret = maetd_clone(ctx, &imgb);
            LOGI("maetd_clone result: %d", clone_ret);
            ok = (clone_ret >= 0);
        }
    }

    if (maetd_delete) maetd_delete(ctx);
    free(file_buf);
    return ok;
}

// ---------------------------------------------------------------------------
// JNI: getSpiDimensions
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jintArray JNICALL
Java_org_crazyromteam_maetplayer_MaetDecoder_getSpiDimensions(
        JNIEnv *env, jobject /*thiz*/, jstring spiPath)
{
    const char *spi = env->GetStringUTFChars(spiPath, nullptr);

    ensure_libs();
    if (!maet_init || !maetd_create || !maetd_decode || !maetd_config) {
        env->ReleaseStringUTFChars(spiPath, spi);
        return nullptr;
    }

    maet_init();

    int fd = open(spi, O_RDONLY);
    if (fd < 0) {
        env->ReleaseStringUTFChars(spiPath, spi);
        return nullptr;
    }

    struct stat st;
    fstat(fd, &st);
    size_t file_size = (size_t)st.st_size;
    uint8_t* file_buf = (uint8_t*)malloc(file_size);
    read(fd, file_buf, file_size);
    close(fd);

    uint64_t param_1[2] = {1, 0};
    int* ctx = maetd_create(param_1, 0);

    size_t offset = 0;
    int stat[8] = { 0 };
    while (offset + 4 <= file_size) {
        uint32_t chunk_size = *(uint32_t*)(file_buf + offset);
        if (offset + 4 + chunk_size > file_size) break;
        BitbDesc bitb;
        memset(&bitb, 0, sizeof(bitb));
        bitb.data = file_buf + offset + 4;
        bitb.size = chunk_size;
        maetd_decode(ctx, &bitb, stat);
        offset += 4 + chunk_size;
    }

    int width = 0, height = 0;
    int size4 = 4;
    maetd_config(ctx, 0xc9, (uint32_t*)&width, &size4);
    size4 = 4;
    maetd_config(ctx, 0xca, (uint32_t*)&height, &size4);

    if (maetd_delete) maetd_delete(ctx);
    free(file_buf);
    env->ReleaseStringUTFChars(spiPath, spi);

    if (width <= 0 || height <= 0) return nullptr;

    jintArray result = env->NewIntArray(2);
    jint dims[2] = {width, height};
    env->SetIntArrayRegion(result, 0, 2, dims);
    return result;
}

// ---------------------------------------------------------------------------
// JNI: decodeSpi (Export PNG)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_org_crazyromteam_maetplayer_MaetDecoder_decodeSpi(
        JNIEnv *env, jobject /*thiz*/, jstring spiPath, jstring outPath)
{
    const char *spi = env->GetStringUTFChars(spiPath, nullptr);
    const char *out = env->GetStringUTFChars(outPath, nullptr);

    uint8_t* rgba = nullptr;
    int width = 0, height = 0, chunks = 0;
    bool success = decode_spi_internal(spi, rgba, width, height, chunks);
    bool save_success = false;

    if (success && rgba && width > 0 && height > 0) {
        jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
        jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
        jfieldID configField = env->GetStaticFieldID(configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
        jobject config = env->GetStaticObjectField(configClass, configField);

        jmethodID createBitmap = env->GetStaticMethodID(bitmapClass, "createBitmap",
                                                        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
        jobject bitmap = env->CallStaticObjectMethod(bitmapClass, createBitmap, width, height, config);

        if (bitmap) {
            void* pixels = nullptr;
            if (AndroidBitmap_lockPixels(env, bitmap, &pixels) == 0) {
                memcpy(pixels, rgba, (size_t)width * height * 4);
                AndroidBitmap_unlockPixels(env, bitmap);

                jclass fosClass = env->FindClass("java/io/FileOutputStream");
                jmethodID fosConstructor = env->GetMethodID(fosClass, "<init>", "(Ljava/lang/String;)V");
                jobject fos = env->NewObject(fosClass, fosConstructor, env->NewStringUTF(out));

                if (fos) {
                    jclass formatClass = env->FindClass("android/graphics/Bitmap$CompressFormat");
                    jfieldID pngField = env->GetStaticFieldID(formatClass, "PNG", "Landroid/graphics/Bitmap$CompressFormat;");
                    jobject pngFormat = env->GetStaticObjectField(formatClass, pngField);

                    jmethodID compress = env->GetMethodID(bitmapClass, "compress",
                                                          "(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z");
                    save_success = env->CallBooleanMethod(bitmap, compress, pngFormat, 100, fos);

                    jmethodID close = env->GetMethodID(fosClass, "close", "()V");
                    env->CallVoidMethod(fos, close);
                }
            }
        }
    }

    if (rgba) free(rgba);
    env->ReleaseStringUTFChars(spiPath, spi);
    env->ReleaseStringUTFChars(outPath, out);

    std::string result = "Chunks: " + std::to_string(chunks) +
                         " | Res: " + std::to_string(width) + "x" + std::to_string(height) +
                         " | PNG: " + (save_success ? "Success" : "Failed");
    return env->NewStringUTF(result.c_str());
}

// ---------------------------------------------------------------------------
// JNI: previewSpi (Render directly to SurfaceView)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_org_crazyromteam_maetplayer_MaetDecoder_previewSpi(
        JNIEnv *env, jobject /*thiz*/, jstring spiPath, jobject surface)
{
    if (!surface) return JNI_FALSE;

    const char *spi = env->GetStringUTFChars(spiPath, nullptr);
    uint8_t* rgba = nullptr;
    int width = 0, height = 0, chunks = 0;
    bool success = decode_spi_internal(spi, rgba, width, height, chunks);
    env->ReleaseStringUTFChars(spiPath, spi);

    if (!success || !rgba || width <= 0 || height <= 0) {
        if (rgba) free(rgba);
        return JNI_FALSE;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("ANativeWindow_fromSurface failed");
        free(rgba);
        return JNI_FALSE;
    }

    ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);

    ANativeWindow_Buffer buffer;
    bool rendered = false;
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0) {
        uint8_t* dst = (uint8_t*)buffer.bits;
        size_t row_bytes = (size_t)width * 4;
        size_t dst_stride_bytes = (size_t)buffer.stride * 4;

        for (int y = 0; y < height; y++) {
            memcpy(dst + y * dst_stride_bytes, rgba + y * row_bytes, row_bytes);
        }

        ANativeWindow_unlockAndPost(window);
        rendered = true;
        LOGI("Preview rendered %dx%d on SurfaceView", width, height);
    } else {
        LOGE("ANativeWindow_lock failed");
    }

    ANativeWindow_release(window);
    free(rgba);
    return rendered ? JNI_TRUE : JNI_FALSE;
}
