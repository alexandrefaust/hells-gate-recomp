// Sound played when an achievement notification appears: a synthesized chime,
// or the WAV file named by achievement_sound_file.

#pragma once

namespace overlay {

// UI thread. Does nothing when achievement_sound is off.
void PlayAchievementSound();
void ShutdownAchievementSound();

}  // namespace overlay
