#include "AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION

#include <miniaudio.h>

#include "Utils/Log.h"
#include "Utils/Profiler.h"

namespace Ox {
void AudioEngine::init() {
  OX_SCOPED_ZONE;
  ma_engine_config config = ma_engine_config_init();
  config.listenerCount = 1;

  engine = new ma_engine();
  const ma_result result = ma_engine_init(&config, engine);
  OX_CORE_ASSERT(result == MA_SUCCESS, "Failed to initialize audio engine!")

  OX_CORE_INFO("Initalized audio engine.");
}

void AudioEngine::deinit() {
  ma_engine_uninit(engine);
  delete engine;
}

ma_engine* AudioEngine::get_engine() const {
  return engine;
}
}
