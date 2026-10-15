#pragma once

#include <jni.h>

#include <string>

namespace imago::android {

/// Stored once in JNI_OnLoad.
JavaVM* javaVM();

/// The JNIEnv of the calling thread, attaching it to the VM the first time. Core and decoder
/// threads stay attached until they exit (a thread-local destructor detaches them).
JNIEnv* currentEnv();

/// Clears a pending Java exception thrown by a callback, so a bug on the Kotlin side cannot
/// take down a core thread. Returns true if there was one.
bool clearException(JNIEnv* env);

std::string toStdString(JNIEnv* env, jstring text);

} // namespace imago::android
