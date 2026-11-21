// Repair Suite — CLAP load/process/state smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/all.h>

#include <dlfcn.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "params.hpp"

namespace {
struct EventList {
  const clap_event_header_t* event = nullptr;
};
uint32_t eventCount(const clap_input_events_t*) { return 1; }
const clap_event_header_t* eventGet(const clap_input_events_t* list,
                                    uint32_t index) {
  return index == 0 ? static_cast<const EventList*>(list->ctx)->event : nullptr;
}
int64_t streamWrite(const clap_ostream_t* stream, const void* buffer,
                    uint64_t size) {
  auto* out = static_cast<std::vector<uint8_t>*>(stream->ctx);
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  out->insert(out->end(), bytes, bytes + size);
  return static_cast<int64_t>(size);
}
size_t gReadOffset = 0;
int64_t streamRead(const clap_istream_t* stream, void* buffer, uint64_t size) {
  auto* in = static_cast<std::vector<uint8_t>*>(stream->ctx);
  const size_t available = in->size() - gReadOffset;
  const size_t n = std::min<size_t>(available, size);
  std::memcpy(buffer, in->data() + gReadOffset, n);
  gReadOffset += n;
  return static_cast<int64_t>(n);
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <RepairSuite.clap>\n", argv[0]);
    return 1;
  }
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(lib);
  auto* entry = static_cast<const clap_plugin_entry_t*>(
      dlsym(lib, "clap_entry"));
  assert(entry && entry->init(argv[1]));
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  assert(factory);
  assert(factory->get_plugin_count(factory) == 1);
  const clap_plugin_descriptor_t* desc =
      factory->get_plugin_descriptor(factory, 0);
  assert(desc);
  std::printf("plugin: %s\n", desc->name);

  const clap_plugin_t* plugin =
      factory->create_plugin(factory, nullptr, desc->id);
  assert(plugin && plugin->init(plugin));
  assert(plugin->activate(plugin, 48000.0, 1, 512));

  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  assert(params);
  const uint32_t count = params->count(plugin);
  std::printf("params: %u\n", count);
  assert(count > 30);
  for (uint32_t i = 0; i < count; ++i) {
    clap_param_info_t info{};
    assert(params->get_info(plugin, i, &info));
    assert(info.id == i);
    double value = 0.0;
    assert(params->get_value(plugin, i, &value));
    assert(std::isfinite(value));
  }

  const auto* latency = static_cast<const clap_plugin_latency_t*>(
      plugin->get_extension(plugin, CLAP_EXT_LATENCY));
  assert(latency);
  const uint32_t reported = latency->get(plugin);
  std::printf("latency: %u samples\n", reported);
  assert(reported == 4221);

  float inL[64] = {};
  float inR[64] = {};
  float outL[64] = {};
  float outR[64] = {};
  for (int i = 0; i < 64; ++i) {
    inL[i] = inR[i] =
        0.1f * std::sin(2.0 * 3.141592653589793 * 220.0 * i / 48000.0);
  }
  float* inPtrs[2] = {inL, inR};
  float* outPtrs[2] = {outL, outR};
  clap_audio_buffer_t inBuf{};
  inBuf.data32 = inPtrs;
  inBuf.channel_count = 2;
  clap_audio_buffer_t outBuf{};
  outBuf.data32 = outPtrs;
  outBuf.channel_count = 2;
  clap_process_t proc{};
  proc.frames_count = 64;
  proc.audio_inputs = &inBuf;
  proc.audio_inputs_count = 1;
  proc.audio_outputs = &outBuf;
  proc.audio_outputs_count = 1;
  assert(plugin->process(plugin, &proc) == CLAP_PROCESS_CONTINUE);
  for (int i = 0; i < 64; ++i) {
    assert(std::isfinite(outL[i]) && std::isfinite(outR[i]));
  }

  // Parameter flush updates the visible value.
  {
    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.param_id = repair::P_INPUT;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = 6.0;
    EventList list{&ev.header};
    clap_input_events_t events{};
    events.ctx = &list;
    events.size = eventCount;
    events.get = eventGet;
    params->flush(plugin, &events, nullptr);
    double reportedValue = 0.0;
    assert(params->get_value(plugin, repair::P_INPUT, &reportedValue));
    assert(std::fabs(reportedValue - 6.0) < 1.0e-9);
  }

  // State round-trip.
  {
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    assert(state && state->save && state->load);
    std::vector<uint8_t> saved;
    clap_ostream_t out{};
    out.ctx = &saved;
    out.write = streamWrite;
    assert(state->save(plugin, &out));
    assert(saved.size() > 8);

    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.param_id = repair::P_INPUT;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = 1.0;
    EventList list{&ev.header};
    clap_input_events_t events{};
    events.ctx = &list;
    events.size = eventCount;
    events.get = eventGet;
    params->flush(plugin, &events, nullptr);

    clap_istream_t in{};
    in.ctx = &saved;
    in.read = streamRead;
    gReadOffset = 0;
    assert(state->load(plugin, &in));
    double restored = 0.0;
    assert(params->get_value(plugin, repair::P_INPUT, &restored));
    assert(std::fabs(restored - 6.0) < 1.0e-9);
  }

  // GUI size round-trip: a resized editor must come back at the same size.
  {
    const auto* gui = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    assert(gui && state && gui->set_size && gui->get_size);
    assert(gui->set_size(plugin, 1300, 800));
    uint32_t w = 0, h = 0;
    assert(gui->get_size(plugin, &w, &h));
    assert(w == 1300 && h == 800);
    std::vector<uint8_t> saved;
    clap_ostream_t out{};
    out.ctx = &saved;
    out.write = streamWrite;
    assert(state->save(plugin, &out));
    assert(gui->set_size(plugin, 1040, 640));
    clap_istream_t in{};
    in.ctx = &saved;
    in.read = streamRead;
    gReadOffset = 0;
    assert(state->load(plugin, &in));
    assert(gui->get_size(plugin, &w, &h));
    assert(w == 1300 && h == 800);
  }

  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);
  std::printf("CLAP load/process ok\n");
  return 0;
}
