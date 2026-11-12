#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static FMOD::DSP* g_dsp = nullptr;
static bool g_needsDsp = true;
static bool g_inLevel = false;

static void applyEffects() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    FMOD::ChannelGroup* channel = engine->m_backgroundMusicChannel;
    if (!channel) return;

    auto mod = Mod::get();
    bool enabled = mod->getSettingValue<bool>("enabled");
    bool inLevelsToo = mod->getSettingValue<bool>("apply-in-levels");
    bool active = enabled && (!g_inLevel || inLevelsToo);

    float pitch = active ? static_cast<float>(mod->getSettingValue<double>("pitch")) : 1.f;
    float dist = active ? static_cast<float>(mod->getSettingValue<double>("distortion")) : 0.f;

    channel->setPitch(pitch);

    // A new channel is made for every song, so the DSP must be re-attached each time.
    if (g_needsDsp) {
        if (g_dsp) {
            g_dsp->release();
            g_dsp = nullptr;
        }
        if (engine->m_system->createDSPByType(FMOD_DSP_TYPE_DISTORTION, &g_dsp) == FMOD_OK && g_dsp) {
            channel->addDSP(0, g_dsp);
        }
        g_needsDsp = false;
    }

    if (g_dsp) {
        g_dsp->setParameterFloat(FMOD_DSP_DISTORTION_LEVEL, dist);
        g_dsp->setBypass(dist <= 0.f);
    }
}

// Jukebox (and the normal game) play songs through playMusic, so hooking it
// makes this mod work with Jukebox songs automatically.
class $modify(PDEngine, FMODAudioEngine) {
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
        g_needsDsp = true;
        applyEffects();
    }
};

class $modify(PDPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g_inLevel = true;
        applyEffects();
    }
    void onQuit() {
        g_inLevel = false;
        PlayLayer::onQuit();
        applyEffects();
    }
};

$on_mod(Loaded) {
    listenForSettingChanges<double>("pitch", [](double) { applyEffects(); });
    listenForSettingChanges<double>("distortion", [](double) { applyEffects(); });
    listenForSettingChanges<bool>("enabled", [](bool) { applyEffects(); });
    listenForSettingChanges<bool>("apply-in-levels", [](bool) { applyEffects(); });
}
