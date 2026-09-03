#ifdef __ANDROID__

#    include "profile_picker_dialog.hpp"

#    include <android/asset_manager.h>
#    include <android/native_activity.h>
#    include <android_native_app_glue.h>
#    include <cstdio>
#    include <cstring>
#    include <jni.h>
#    include <spdlog/spdlog.h>
#    include <stdexcept>
#    include <string>
#    include <sys/stat.h>
#    include <vector>

namespace ILLIXR {

namespace {

    /**
     * @brief Calls {@code ProfilePickerDialog.showDialog(Activity, String)} via JNI.
     *
     * @param app           android_app pointer.
     * @param profiles_dir  Absolute path to the directory where profile files were
     *                      extracted — passed to Java so it can enumerate them.
     * @return              Full path to the chosen file, or empty string on cancel.
     * @throws std::runtime_error on any JNI lookup failure.
     */
    std::string invoke_java_dialog(struct android_app* app, const std::string& profiles_dir) {
        ANativeActivity* native_activity = app->activity;
        JavaVM*          jvm             = native_activity->vm;
        JNIEnv*          env             = nullptr;

        jint attach_result = jvm->AttachCurrentThread(&env, nullptr);
        if (attach_result != JNI_OK || env == nullptr) {
            throw std::runtime_error("[profile_picker_dialog] Failed to attach thread to JVM");
        }

        jclass dialog_class = env->FindClass("com/example/ILLIXR/ILLIXRNativeActivity$ProfilePickerDialog");
        if (dialog_class == nullptr) {
            jvm->DetachCurrentThread();
            throw std::runtime_error("[profile_picker_dialog] Could not find class "
                                     "com/example/ILLIXR/ProfilePickerDialog");
        }

        // Signature: static String showDialog(Activity activity, String profilesDir)
        jmethodID show_method =
            env->GetStaticMethodID(dialog_class, "showDialog", "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
        if (show_method == nullptr) {
            env->DeleteLocalRef(dialog_class);
            jvm->DetachCurrentThread();
            throw std::runtime_error("[profile_picker_dialog] Could not find method "
                                     "ProfilePickerDialog.showDialog(Activity, String)");
        }

        jstring j_profiles_dir  = env->NewStringUTF(profiles_dir.c_str());
        jobject activity_object = native_activity->clazz;

        auto result_jstring =
            reinterpret_cast<jstring>(env->CallStaticObjectMethod(dialog_class, show_method, activity_object, j_profiles_dir));

        std::string result;
        if (result_jstring != nullptr) {
            const char* chars = env->GetStringUTFChars(result_jstring, nullptr);
            if (chars != nullptr) {
                result = chars;
                env->ReleaseStringUTFChars(result_jstring, chars);
            }
            env->DeleteLocalRef(result_jstring);
        }

        env->DeleteLocalRef(j_profiles_dir);
        env->DeleteLocalRef(dialog_class);
        jvm->DetachCurrentThread();
        return result;
    }

    /**
     * @brief Extracts all files from the {@code profiles/} asset directory to
     *        @p dest_dir, overwriting any existing files.
     *
     * @param asset_manager  AAssetManager from the NativeActivity.
     * @param dest_dir       Absolute writable path to extract into.
     * @return               Number of files extracted, or -1 on error.
     */
    int extract_profiles(AAssetManager* asset_manager, const std::string& dest_dir) {
        AAssetDir* asset_dir = AAssetManager_openDir(asset_manager, "profiles");
        if (asset_dir == nullptr) {
            spdlog::get("illixr")->error("[profile_picker_dialog] No 'profiles' directory found in APK assets");
            return -1;
        }

        int         count    = 0;
        const char* filename = nullptr;
        while ((filename = AAssetDir_getNextFileName(asset_dir)) != nullptr) {
            std::string asset_path = std::string("profiles/") + filename;
            std::string dest_path  = dest_dir + "/" + filename;

            AAsset* asset = AAssetManager_open(asset_manager, asset_path.c_str(), AASSET_MODE_BUFFER);
            if (asset == nullptr) {
                spdlog::get("illixr")->warn("[profile_picker_dialog] Could not open asset: {}", asset_path);
                continue;
            }

            const void* buf  = AAsset_getBuffer(asset);
            const off_t size = AAsset_getLength(asset);

            FILE* fp = std::fopen(dest_path.c_str(), "wb");
            if (fp != nullptr) {
                std::fwrite(buf, 1, static_cast<size_t>(size), fp);
                std::fclose(fp);
                ++count;
            } else {
                spdlog::get("illixr")->warn("[profile_picker_dialog] Could not write: {}", dest_path);
            }

            AAsset_close(asset);
        }

        AAssetDir_close(asset_dir);
        return count;
    }

} // anonymous namespace

std::string show_profile_picker_dialog(struct android_app* app) {
    ANativeActivity* native_activity = app->activity;

    // Determine the extraction directory: app internal storage, profiles subdir.
    // internalDataPath is guaranteed writable and private to the app.
    const std::string profiles_dir = std::string(native_activity->internalDataPath) + "/profiles";

    // Ensure the destination directory exists.  mkdir is idempotent if it
    // already exists.
    ::mkdir(profiles_dir.c_str(), 0755);

    // Extract (overwrite) profile files from APK assets every time to pick up
    // any changes introduced by an app update.
    const int extracted = extract_profiles(native_activity->assetManager, profiles_dir);
    if (extracted < 0) {
        spdlog::get("illixr")->error("[profile_picker_dialog] Failed to extract profiles from APK assets");
        return "";
    }
    if (extracted == 0) {
        spdlog::get("illixr")->error("[profile_picker_dialog] No profile files found in APK assets/profiles/");
        return "";
    }

    spdlog::get("illixr")->info("[profile_picker_dialog] Extracted {} profile(s) to {}", extracted, profiles_dir);

    std::string chosen;
    try {
        chosen = invoke_java_dialog(app, profiles_dir);
    } catch (const std::runtime_error& ex) {
        spdlog::get("illixr")->error("{}", ex.what());
        return "";
    }

    if (chosen.empty()) {
        spdlog::get("illixr")->warn("[profile_picker_dialog] User cancelled profile selection.");
    } else {
        spdlog::get("illixr")->info("[profile_picker_dialog] Profile selected: {}", chosen);
    }

    return chosen;
}

} // namespace ILLIXR

#endif // __ANDROID__
