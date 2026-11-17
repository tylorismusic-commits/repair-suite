// Repair Suite — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Eight-stage repair strip.  Control ports mirror the CLAP parameter ids:
// port = PORT_FIRST_PARAM + ParamId.  Two extra controls after the parameters
// are one-shot learn triggers from the UI.
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "dsp/engine.hpp"
#include "dsp/inline_display.hpp"
#include "dsp/repair_instance.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/repair-suite";

enum Port : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_FIRST_PARAM = 4,
  PORT_LEARN_INPUT = PORT_FIRST_PARAM + repair::P_COUNT,
  PORT_LEARN_SIB,
  PORT_COMPARE,
  PORT_COMP_GR,
  PORT_SIB_GR,
  PORT_HARSH_GR,
  PORT_LIM_GR,
  PORT_OUT_PEAK_L,
  PORT_OUT_PEAK_R,
  PORT_IN_PEAK_L,
  PORT_IN_PEAK_R,
  PORT_INPUT_LEARN_PEAK,
  PORT_SIB_LEARN_HZ,
  PORT_SIB_LEARN_VALID,
  PORT_HOST_BPM,
  PORT_HOST_TEMPO,
  PORT_LATENCY,
  PORT_TIME,
  PORT_COUNT
};

struct Plugin {
  repair::PublicInstance pub;
  float sampleRate = 48000.0f;
  float lastPort[repair::P_COUNT] = {};
  bool portsSeen = false;
  repair::Shared shared;
  repair::Engine engine;
  repair::Meters meters;

  const LV2_URID_Map* map = nullptr;
  uint32_t uridPosition = 0;
  uint32_t uridBpm = 0;
  uint32_t uridFloat = 0;
  uint32_t uridDouble = 0;
  uint32_t uridFrame = 0;
  uint32_t uridStateKey = 0;
  uint32_t uridEditsKey = 0;
  uint32_t uridChunk = 0;
  double transportSeconds = 0.0;
  bool transportValid = false;
  const LV2_Atom_Sequence* timePort = nullptr;

  std::unique_ptr<float*[]> ports;

  float* audioInL = nullptr;
  float* audioInR = nullptr;
  float* audioOutL = nullptr;
  float* audioOutR = nullptr;
  float* learnInput = nullptr;
  float* learnSib = nullptr;
  float* compare = nullptr;
  float* compGr = nullptr;
  float* sibGr = nullptr;
  float* harshGr = nullptr;
  float* limGr = nullptr;
  float* outPeakL = nullptr;
  float* outPeakR = nullptr;
  float* inPeakL = nullptr;
  float* inPeakR = nullptr;
  float* inputLearnPeak = nullptr;
  float* sibLearnHz = nullptr;
  float* sibLearnValid = nullptr;
  float* hostBpm = nullptr;
  float* hostTempo = nullptr;
  float* latency = nullptr;

  repair::InlineDisplay display;
  const repair::InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const* features) {
  auto* p = new Plugin();
  p->sampleRate = static_cast<float>(rate);
  p->engine.prepare(p->sampleRate);
  p->pub.bridge = &p->engine.capture();
  p->shared.sampleRate.store(p->sampleRate, std::memory_order_relaxed);
  p->engine.setControls(repair::controlsFrom(p->shared));
  p->shared.captureFreeze.store(false, std::memory_order_relaxed);
  p->shared.clearCapture.store(false, std::memory_order_relaxed);
  p->shared.clearEdits.store(false, std::memory_order_relaxed);

  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_URID__map)) {
      p->map = static_cast<const LV2_URID_Map*>(f[0]->data);
    } else if (!std::strcmp((*f)->URI, repair::kInlineDisplayQueueDrawURI) &&
               (*f)->data) {
      p->queueDraw = static_cast<const repair::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  if (p->map && p->map->map) {
    p->uridPosition = p->map->map(p->map->handle, LV2_TIME__Position);
    p->uridBpm = p->map->map(p->map->handle, LV2_TIME__beatsPerMinute);
    p->uridFloat = p->map->map(p->map->handle, LV2_ATOM__Float);
    p->uridDouble = p->map->map(p->map->handle, LV2_ATOM__Double);
    p->uridFrame = p->map->map(p->map->handle, LV2_TIME__frame);
    p->uridStateKey = p->map->map(p->map->handle, "urn:repair-suite:noise-profile");
    p->uridEditsKey = p->map->map(p->map->handle, "urn:repair-suite:spectral-edits");
    p->uridChunk = p->map->map(p->map->handle, LV2_ATOM__Chunk);
  }
  return p;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data) {
  auto* p = static_cast<Plugin*>(handle);
  switch (port) {
    case PORT_AUDIO_IN_L: p->audioInL = static_cast<float*>(data); break;
    case PORT_AUDIO_IN_R: p->audioInR = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_L: p->audioOutL = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_R: p->audioOutR = static_cast<float*>(data); break;
    case PORT_LEARN_INPUT: p->learnInput = static_cast<float*>(data); break;
    case PORT_LEARN_SIB: p->learnSib = static_cast<float*>(data); break;
    case PORT_COMPARE: p->compare = static_cast<float*>(data); break;
    case PORT_COMP_GR: p->compGr = static_cast<float*>(data); break;
    case PORT_SIB_GR: p->sibGr = static_cast<float*>(data); break;
    case PORT_HARSH_GR: p->harshGr = static_cast<float*>(data); break;
    case PORT_LIM_GR: p->limGr = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_L: p->outPeakL = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_R: p->outPeakR = static_cast<float*>(data); break;
    case PORT_IN_PEAK_L: p->inPeakL = static_cast<float*>(data); break;
    case PORT_IN_PEAK_R: p->inPeakR = static_cast<float*>(data); break;
    case PORT_INPUT_LEARN_PEAK:
      p->inputLearnPeak = static_cast<float*>(data);
      break;
    case PORT_SIB_LEARN_HZ: p->sibLearnHz = static_cast<float*>(data); break;
    case PORT_SIB_LEARN_VALID:
      p->sibLearnValid = static_cast<float*>(data);
      break;
    case PORT_HOST_BPM: p->hostBpm = static_cast<float*>(data); break;
    case PORT_HOST_TEMPO: p->hostTempo = static_cast<float*>(data); break;
    case PORT_LATENCY: p->latency = static_cast<float*>(data); break;
    case PORT_TIME:
      p->timePort = static_cast<const LV2_Atom_Sequence*>(data);
      break;
    default:
      if (!p->ports) {
        p->ports = std::make_unique<float*[]>(PORT_COUNT);
        std::memset(p->ports.get(), 0, sizeof(float*) * PORT_COUNT);
      }
      if (port < PORT_COUNT) {
        p->ports[port] = static_cast<float*>(data);
      }
      break;
  }
}

void updateTempo(Plugin* p) {
  if (!p->timePort || !p->uridPosition || !p->uridBpm) {
    return;
  }
  auto* seq = const_cast<LV2_Atom_Sequence*>(p->timePort);
  LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
    if (ev->body.type != p->uridPosition) {
      continue;
    }
    const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
    const LV2_Atom* bpmAtom = nullptr;
    const LV2_Atom* frameAtom = nullptr;
    LV2_Atom_Object_Query query[] = {
        {p->uridBpm, &bpmAtom},
        {p->uridFrame, &frameAtom},
        LV2_ATOM_OBJECT_QUERY_END,
    };
    lv2_atom_object_query(obj, query);
    if (bpmAtom && bpmAtom->type == p->uridFloat &&
        bpmAtom->size == sizeof(float)) {
      const float bpm = reinterpret_cast<const LV2_Atom_Float*>(bpmAtom)->body;
      if (std::isfinite(bpm) && bpm > 1.0f) {
        p->engine.setTempo(bpm, true);
        p->shared.bpm.store(bpm, std::memory_order_relaxed);
        p->shared.hostTempo.store(true, std::memory_order_relaxed);
      }
    }
    if (frameAtom && frameAtom->type == p->uridDouble &&
        frameAtom->size == sizeof(double) && p->sampleRate > 0.0f) {
      const double frame =
          reinterpret_cast<const LV2_Atom_Double*>(frameAtom)->body;
      if (std::isfinite(frame)) {
        p->transportSeconds = frame / static_cast<double>(p->sampleRate);
        p->transportValid = true;
      }
    }
  }
}

void activate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  p->engine.reset();
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }
}

void deactivate(LV2_Handle) {}

void run(LV2_Handle handle, uint32_t frames) {
  auto* p = static_cast<Plugin*>(handle);
  if (!p->audioInL || !p->audioOutL) {
    return;
  }
  if (!p->audioInR) {
    p->audioInR = p->audioInL;
  }
  if (!p->audioOutR) {
    p->audioOutR = p->audioOutL;
  }

  if (p->ports) {
    for (uint32_t i = 0; i < repair::P_COUNT; ++i) {
      float* v = p->ports[PORT_FIRST_PARAM + i];
      if (!v) {
        continue;
      }
      if (!p->portsSeen || *v != p->lastPort[i]) {
        p->shared.setFromHost(i, static_cast<double>(*v));
        p->lastPort[i] = *v;
      }
    }
    p->portsSeen = true;
  }
  if (p->learnInput) {
    p->shared.captureActive.store(*p->learnInput > 0.5f,
                                     std::memory_order_relaxed);
  }
  if (p->learnSib) {
    p->shared.noiseLearnActive.store(*p->learnSib > 0.5f,
                                   std::memory_order_relaxed);
  }
  if (p->compare) {
    p->shared.compare.store(*p->compare > 0.5f, std::memory_order_relaxed);
  }
  updateTempo(p);
  p->engine.setControls(repair::controlsFrom(p->shared));
  p->shared.captureFreeze.store(false, std::memory_order_relaxed);
  p->shared.clearCapture.store(false, std::memory_order_relaxed);
  p->shared.clearEdits.store(false, std::memory_order_relaxed);
  p->shared.analyzeRequest.store(false, std::memory_order_relaxed);
  if (p->shared.profilePending.exchange(false, std::memory_order_acq_rel)) {
    float profile[repair::kProfileBins] = {};
    const int bins = p->shared.getProfile(profile, repair::kProfileBins);
    if (bins > 0) {
      p->engine.loadNoiseProfile(profile, bins);
    }
  }
  if (p->audioOutL != p->audioInL) {
    std::memcpy(p->audioOutL, p->audioInL, frames * sizeof(float));
  }
  if (p->audioOutR != p->audioInR) {
    std::memcpy(p->audioOutR, p->audioInR, frames * sizeof(float));
  }

  p->engine.process(p->audioOutL, p->audioOutR, static_cast<int>(frames),
                    p->meters, p->transportSeconds, p->transportValid);
  if (p->engine.takeProfileReady()) {
    float profile[repair::kProfileBins] = {};
    const int bins = p->engine.copyNoiseProfile(profile, repair::kProfileBins);
    if (bins > 0) {
      p->shared.setProfile(profile, bins);
    }
  }

  p->shared.analysisReady.store(p->meters.analysisReady,
                                std::memory_order_relaxed);
  p->shared.analysisProgress.store(p->meters.analysisProgress,
                                   std::memory_order_relaxed);
  p->shared.analysisHumHz.store(p->meters.analysisHumHz,
                                std::memory_order_relaxed);
  p->shared.analysisHumDb.store(p->meters.analysisHumDb,
                                std::memory_order_relaxed);
  p->shared.analysisNoiseDb.store(p->meters.analysisNoiseDb,
                                  std::memory_order_relaxed);
  p->shared.analysisClipPct.store(p->meters.analysisClipPct,
                                  std::memory_order_relaxed);
  p->shared.analysisClickRate.store(p->meters.analysisClickRate,
                                    std::memory_order_relaxed);
  p->shared.analysisReverbSec.store(p->meters.analysisReverbSec,
                                    std::memory_order_relaxed);
  if (p->compGr) *p->compGr = p->meters.grDb;
  if (p->sibGr) *p->sibGr = p->meters.noiseGrDb;
  if (p->harshGr) *p->harshGr = p->meters.verbGrDb;
  if (p->limGr) *p->limGr = p->meters.specActive;
  if (p->outPeakL) *p->outPeakL = p->meters.outPeakLDb;
  if (p->outPeakR) *p->outPeakR = p->meters.outPeakRDb;
  if (p->inPeakL) *p->inPeakL = p->meters.inPeakLDb;
  if (p->inPeakR) *p->inPeakR = p->meters.inPeakRDb;
  if (p->inputLearnPeak) *p->inputLearnPeak = p->meters.captureSeconds;
  if (p->sibLearnHz) *p->sibLearnHz = p->meters.transportSeconds;
  if (p->sibLearnValid) {
    *p->sibLearnValid = p->meters.transportValid ? 1.0f : 0.0f;
  }
  if (p->hostBpm) *p->hostBpm = 0.0f;
  if (p->hostTempo) *p->hostTempo = 0.0f;
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }

  p->display.setGainReductionDb(p->meters.grDb);
  if (p->queueDraw && p->queueDraw->queue_draw &&
      ((p->drawCounter++ & 31u) == 0u)) {
    p->queueDraw->queue_draw(p->queueDraw->handle);
  }
}

void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }

repair::InlineDisplaySurface* renderInlineDisplay(void* instance, uint32_t width,
                                                uint32_t height) {
  auto* p = static_cast<Plugin*>(instance);
  return p ? p->display.render(width, height) : nullptr;
}

const repair::InlineDisplayInterface kInlineDisplay = {&renderInlineDisplay};

LV2_State_Status stateSave(LV2_Handle handle, LV2_State_Store_Function store,
                            LV2_State_Handle state,
                            uint32_t /*flags*/,
                            const LV2_Feature* const* /*features*/) {
  auto* p = static_cast<Plugin*>(handle);
  float profile[repair::kProfileBins] = {};
  const int bins = p->shared.getProfile(profile, repair::kProfileBins);
  constexpr uint32_t kMagic = 0x52534e31u;  // "RSN1"
  const int32_t count = bins;
  const size_t bytes = 8 + static_cast<size_t>(std::max(0, bins)) * sizeof(float);
  auto* data = static_cast<uint8_t*>(std::malloc(bytes));
  if (!data) {
    return LV2_STATE_ERR_NO_SPACE;
  }
  std::memcpy(data, &kMagic, sizeof(kMagic));
  std::memcpy(data + 4, &count, sizeof(count));
  if (bins > 0) {
    std::memcpy(data + 8, profile, static_cast<size_t>(bins) * sizeof(float));
  }
  LV2_State_Status st = store(
      state, p->uridStateKey, data, bytes, p->uridChunk,
      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  std::free(data);
  if (st != LV2_STATE_SUCCESS) {
    return st;
  }

  std::vector<repair::SpectralCapture::Edit> edits;
  p->engine.capture().copyEditsForState(&edits);
  size_t editBytes = 8;
  for (const auto& edit : edits) {
    editBytes += 20 + 2 * edit.left.size() * sizeof(float);
  }
  auto* editData = static_cast<uint8_t*>(std::malloc(editBytes));
  if (!editData) {
    return LV2_STATE_ERR_NO_SPACE;
  }
  constexpr uint32_t kEditMagic = 0x52534531u;  // "RSE1"
  const int32_t editCountOut = static_cast<int32_t>(edits.size());
  std::memcpy(editData, &kEditMagic, sizeof(kEditMagic));
  std::memcpy(editData + 4, &editCountOut, sizeof(editCountOut));
  size_t at = 8;
  for (const auto& edit : edits) {
    const int32_t frames = static_cast<int32_t>(edit.left.size());
    std::memcpy(editData + at, &edit.startTime, sizeof(edit.startTime));
    at += sizeof(edit.startTime);
    std::memcpy(editData + at, &edit.endTime, sizeof(edit.endTime));
    at += sizeof(edit.endTime);
    std::memcpy(editData + at, &frames, sizeof(frames));
    at += sizeof(frames);
    const size_t bytes = static_cast<size_t>(frames) * sizeof(float);
    std::memcpy(editData + at, edit.left.data(), bytes);
    at += bytes;
    std::memcpy(editData + at, edit.right.data(), bytes);
    at += bytes;
  }
  st = store(state, p->uridEditsKey, editData, at, p->uridChunk,
             LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  std::free(editData);
  return st;
}

LV2_State_Status stateRestore(LV2_Handle handle, LV2_State_Retrieve_Function retrieve,
                              LV2_State_Handle state, uint32_t /*flags*/,
                              const LV2_Feature* const* /*features*/) {
  auto* p = static_cast<Plugin*>(handle);
  size_t size = 0;
  uint32_t type = 0;
  uint32_t flags = 0;
  const void* data = retrieve(state, p->uridStateKey, &size, &type, &flags);
  if (!data || type != p->uridChunk || size < 8) {
    return LV2_STATE_SUCCESS;
  }
  uint32_t magic = 0;
  int32_t bins = 0;
  std::memcpy(&magic, data, sizeof(magic));
  std::memcpy(&bins, static_cast<const uint8_t*>(data) + 4, sizeof(bins));
  if (magic != 0x52534e31u || bins <= 0 ||
      bins > repair::kProfileBins ||
      size < 8 + static_cast<size_t>(bins) * sizeof(float)) {
    return LV2_STATE_SUCCESS;
  }
  const auto* floats = reinterpret_cast<const float*>(
      static_cast<const uint8_t*>(data) + 8);
  p->shared.setProfile(floats, bins);
  // The run loop consumes profilePending on the audio thread; do not touch
  // Denoise's buffers from the host state thread.

  const void* editData = retrieve(state, p->uridEditsKey, &size, &type, &flags);
  if (editData && type == p->uridChunk && size >= 8) {
    uint32_t editMagic = 0;
    int32_t editCount = 0;
    std::memcpy(&editMagic, editData, sizeof(editMagic));
    std::memcpy(&editCount, static_cast<const uint8_t*>(editData) + 4,
                sizeof(editCount));
    if (editMagic == 0x52534531u && editCount >= 0 && editCount <= 256) {
      std::vector<repair::SpectralCapture::Edit> edits;
      edits.reserve(static_cast<size_t>(editCount));
      size_t at = 8;
      bool ok = true;
      for (int32_t e = 0; e < editCount; ++e) {
        if (at + 20 > size) {
          ok = false;
          break;
        }
        repair::SpectralCapture::Edit edit;
        int32_t frames = 0;
        std::memcpy(&edit.startTime, static_cast<const uint8_t*>(editData) + at,
                    sizeof(edit.startTime));
        at += sizeof(edit.startTime);
        std::memcpy(&edit.endTime, static_cast<const uint8_t*>(editData) + at,
                    sizeof(edit.endTime));
        at += sizeof(edit.endTime);
        std::memcpy(&frames, static_cast<const uint8_t*>(editData) + at,
                    sizeof(frames));
        at += sizeof(frames);
        if (frames <= 0 || frames > 48000 * 30 ||
            at + 2 * static_cast<size_t>(frames) * sizeof(float) > size) {
          ok = false;
          break;
        }
        edit.left.resize(static_cast<size_t>(frames));
        edit.right.resize(static_cast<size_t>(frames));
        const size_t bytes = static_cast<size_t>(frames) * sizeof(float);
        std::memcpy(edit.left.data(), static_cast<const uint8_t*>(editData) + at,
                    bytes);
        at += bytes;
        std::memcpy(edit.right.data(), static_cast<const uint8_t*>(editData) + at,
                    bytes);
        at += bytes;
        edits.push_back(std::move(edit));
      }
      p->engine.capture().replaceEditsFromState(edits);
    }
  }
  return LV2_STATE_SUCCESS;
}

const LV2_State_Interface kState = {stateSave, stateRestore};

const void* extensionData(const char* uri) {
  if (uri && !std::strcmp(uri, repair::kInlineDisplayInterfaceURI)) {
    return &kInlineDisplay;
  }
  if (uri && !std::strcmp(uri, LV2_STATE__interface)) {
    return &kState;
  }
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate,
    cleanup, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
