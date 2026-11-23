#pragma once
#include <jni.h>
#include "profile.h"

// Called from postAppSpecialize inside a target app: overwrites android.os.Build,
// then installs native hooks (system properties, GLES, EGL, Vulkan).
void spoof_install(const Profile &p, JNIEnv *env);
