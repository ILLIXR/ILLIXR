#pragma once

#ifdef __ANDROID__

#    include <android_native_app_glue.h>
#    include <string>

namespace ILLIXR {

/**
 * @brief Extracts bundled profile YAML files from APK assets and shows a
 *        blocking spinner dialog for the user to choose one.
 *
 * Must be called from a thread that is NOT the Android UI thread (i.e., from
 * within the NativeActivity event-loop helper thread).
 *
 * On first call (and on every subsequent call, since profiles are overwritten
 * to pick up APK updates) all files under the {@code profiles/} directory in
 * the APK assets are extracted to the app's internal files directory.  The
 * user is then shown a spinner containing the profile names (filename without
 * the {@code .yaml} suffix, underscores preserved).
 *
 * @param app  Pointer to the android_app struct supplied by android_main.
 * @return     Absolute path to the chosen YAML file on the device filesystem,
 *             or an empty string if the user cancelled or no profiles exist.
 */
std::string show_profile_picker_dialog(struct android_app* app);

} // namespace ILLIXR

#endif // __ANDROID__
