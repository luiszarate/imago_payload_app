#include "jni_util.h"

#include <android/log.h>

namespace imago::android {

namespace {

JavaVM* gVM = nullptr;

/// Detaches the thread from the VM when it exits, if this code attached it.
struct ThreadAttachment {
    JNIEnv* env = nullptr;
    bool attachedHere = false;

    ~ThreadAttachment() {
        if (attachedHere && gVM != nullptr) {
            gVM->DetachCurrentThread();
        }
    }
};

thread_local ThreadAttachment tAttachment;

} // namespace

JavaVM* javaVM() { return gVM; }

JNIEnv* currentEnv() {
    if (tAttachment.env != nullptr) {
        return tAttachment.env;
    }
    JNIEnv* env = nullptr;
    if (gVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        tAttachment.env = env; // a Java thread: the VM owns the attachment
        return env;
    }
    JavaVMAttachArgs args{JNI_VERSION_1_6, "imago-core", nullptr};
    if (gVM->AttachCurrentThread(&env, &args) != JNI_OK) {
        return nullptr;
    }
    tAttachment.env = env;
    tAttachment.attachedHere = true;
    return env;
}

bool clearException(JNIEnv* env) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_ERROR, "imago", "Java exception in a native callback");
    return true;
}

std::string toStdString(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string result = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(text, chars);
    }
    return result;
}

} // namespace imago::android

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    imago::android::gVM = vm;
    return JNI_VERSION_1_6;
}
