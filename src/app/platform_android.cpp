// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/platform.h"
#include "app/rom_import.h"
#include "SDL.h"
#include <android/log.h>
#include <jni.h>
#include <iostream>

namespace psmodern::app {
namespace {
class LogBuffer : public std::streambuf {
    std::string line_;
    int_type overflow(int_type c) override {
        if (c=='\n' || line_.size()>=3800) {
            __android_log_write(ANDROID_LOG_INFO,"PSModern",line_.c_str());
            line_.clear();
        }
        if (c!='\n' && c!=traits_type::eof()) line_.push_back(static_cast<char>(c));
        return traits_type::not_eof(c);
    }
};
}
void attach_parent_console() {
    static LogBuffer buffer;
    std::cerr.rdbuf(&buffer);
}
std::vector<std::string> utf8_arguments(int,char**) { return {"PSModern"}; }
std::filesystem::path production_save_folder() {
    const auto needed=SDL_ANDROID_EXTERNAL_STORAGE_READ|SDL_ANDROID_EXTERNAL_STORAGE_WRITE;
    if ((SDL_AndroidGetExternalStorageState()&needed)!=needed)
        throw std::runtime_error("App external storage is unavailable.");
    const char* path=SDL_AndroidGetExternalStoragePath();
    if (!path || !*path) throw std::runtime_error("Cannot resolve app external storage.");
    return path;
}
std::optional<std::filesystem::path> pick_rom_file(SDL_Window*,Language) {
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (env->PushLocalFrame(8)<0) throw std::runtime_error("Cannot allocate picker JNI frame.");
    struct Frame { JNIEnv* env; ~Frame(){env->PopLocalFrame(nullptr);} } frame{env};
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    auto method=env->GetMethodID(env->GetObjectClass(activity),"chooseDocument","()Ljava/lang/String;");
    auto suffix=method ? static_cast<jstring>(env->CallObjectMethod(activity,method)) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(); env->ExceptionClear();
        throw std::runtime_error("Cannot read selected ROM document.");
    }
    if (!suffix) return {};
    const char* chars=env->GetStringUTFChars(suffix,nullptr);
    if (!chars) { env->ExceptionClear(); throw std::runtime_error("Cannot read ROM suffix."); }
    const std::string extension(chars);
    env->ReleaseStringUTFChars(suffix,chars);
    if (extension!=".sms" && extension!=".zip") throw std::runtime_error("Unsupported ROM suffix.");
    return production_save_folder()/"roms"/(".import"+extension);
}
void open_folder(const std::filesystem::path&) { throw std::logic_error("Folder action is hidden on Android."); }
}
