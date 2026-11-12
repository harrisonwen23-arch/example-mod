#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <cmath>

using namespace geode::prelude;

static FMOD::DSP* g_pitch = nullptr;
static FMOD::DSP* g_dist = nullptr;
static FMOD::DSP* g_echo = nullptr;
static FMOD::DSP* g_reverb = nullptr;
static FMOD::DSP* g_lowpass = nullptr;
static FMOD::ChannelGroup* g_group = nullptr;
static bool g_inLevel = false;

static void releaseChain() {
    FMOD::DSP** all[] = {&g_pitch, &g_dist, &g_echo, &g_reverb, &g_lowpass};
    for (auto d : all) {
        if (*d) {
            (*d)->release();
            *d = nullptr;
        }
    }
}

static void buildChain(FMOD::System* sys, FMOD::ChannelGroup* group) {
    releaseChain();
    sys->createDSPByType(FMOD_DSP_TYPE_PITCHSHIFT, &g_pitch);
    sys->createDSPByType(FMOD_DSP_TYPE_DISTORTION, &g_dist);
    sys->createDSPByType(FMOD_DSP_TYPE_ECHO, &g_echo);
    sys->createDSPByType(FMOD_DSP_TYPE_SFXREVERB, &g_reverb);
    sys->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &g_lowpass);

    if (g_pitch) g_pitch->setParameterFloat(FMOD_DSP_PITCHSHIFT_FFTSIZE, 2048.f);
    if (g_echo) {
        g_echo->setParameterFloat(FMOD_DSP_ECHO_DELAY, 300.f);
        g_echo->setParameterFloat(FMOD_DSP_ECHO_FEEDBACK, 35.f);
        g_echo->setParameterFloat(FMOD_DSP_ECHO_DRYLEVEL, 0.f);
    }
    if (g_reverb) g_reverb->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 2500.f);

    // Added in processing order: pitch -> distortion -> echo -> reverb -> muffle
    FMOD::DSP* order[] = {g_pitch, g_dist, g_echo, g_reverb, g_lowpass};
    for (auto d : order) {
        if (d) {
            d->setBypass(true);
            group->addDSP(0, d);
        }
    }
    g_group = group;
}

static void applyEffects() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;
    FMOD::ChannelGroup* group = engine->m_backgroundMusicChannel;
    if (!group) return;

    if (group != g_group) buildChain(engine->m_system, group);

    auto mod = Mod::get();
    bool enabled = mod->getSettingValue<bool>("enabled");
    bool inLevelsToo = mod->getSettingValue<bool>("apply-in-levels");
    bool active = enabled && (!g_inLevel || inLevelsToo);

    auto get = [&](char const* id) {
        return active ? static_cast<float>(mod->getSettingValue<double>(id)) : 0.f;
    };

    float semitones = get("pitch-semitones");
    float speed = 1.f;
    if (active && !g_inLevel) {
        speed = static_cast<float>(mod->getSettingValue<double>("speed"));
    }
    float dist = get("distortion");
    float reverb = get("reverb");
    float echo = get("echo");
    float muffle = get("muffle");

    // Speed changes pitch too, so the pitch shifter cancels that out.
    group->setPitch(speed);
    float shift = std::pow(2.f, semitones / 12.f) / speed;
    shift = std::min(2.f, std::max(0.5f, shift));

    if (g_pitch) {
        g_pitch->setParameterFloat(FMOD_DSP_PITCHSHIFT_PITCH, shift);
        g_pitch->setBypass(std::fabs(shift - 1.f) < 0.001f);
    }
    if (g_dist) {
        g_dist->setParameterFloat(FMOD_DSP_DISTORTION_LEVEL, dist);
        g_dist->setBypass(dist <= 0.001f);
    }
    if (g_echo) {
        g_echo->setParameterFloat(FMOD_DSP_ECHO_WETLEVEL, -30.f + 30.f * echo);
        g_echo->setBypass(echo <= 0.001f);
    }
    if (g_reverb) {
        g_reverb->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, -40.f + 40.f * reverb);
        g_reverb->setBypass(reverb <= 0.001f);
    }
    if (g_lowpass) {
        g_lowpass->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 22000.f * std::pow(0.01f, muffle));
        g_lowpass->setBypass(muffle <= 0.001f);
    }
}

static CCMenu* makeFxMenu(CCNode* parent, CCObject* target, SEL_MenuHandler handler) {
    auto spr = ButtonSprite::create("FX");
    spr->setScale(0.8f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, handler);
    auto menu = CCMenu::create();
    menu->addChild(btn);
    auto win = CCDirector::get()->getWinSize();
    menu->setPosition({win.width - 40.f, win.height - 30.f});
    parent->addChild(menu, 100);
    return menu;
}

// Jukebox and the normal game both play songs through playMusic, so this
// hook makes the effects work with Jukebox songs automatically.
class $modify(PDEngine, FMODAudioEngine) {
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
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

// FX button in the pause menu
class $modify(PDPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        makeFxMenu(this, this, menu_selector(PDPauseLayer::onFx));
    }
    void onFx(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

// FX button on the level page
class $modify(PDLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        makeFxMenu(this, this, menu_selector(PDLevelInfoLayer::onFx));
        return true;
    }
    void onFx(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

$on_mod(Loaded) {
    for (auto id : {"pitch-semitones", "speed", "distortion", "reverb", "echo", "muffle"}) {
        listenForSettingChanges<double>(id, [](double) { applyEffects(); });
    }
    listenForSettingChanges<bool>("enabled", [](bool) { applyEffects(); });
    listenForSettingChanges<bool>("apply-in-levels", [](bool) { applyEffects(); });
}
