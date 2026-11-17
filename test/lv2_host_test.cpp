// Repair Suite — LV2 load/process smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lilv/lilv.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "params.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <bundle-dir>\n", argv[0]);
    return 1;
  }
  std::string bundle = argv[1];
  if (!bundle.empty() && bundle.back() != '/') {
    bundle.push_back('/');
  }
  LilvWorld* world = lilv_world_new();
  LilvNode* uri = lilv_new_file_uri(world, nullptr, bundle.c_str());
  lilv_world_load_bundle(world, uri);
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  LilvNode* pluginUri =
      lilv_new_uri(world, "https://github.com/tylorismusic-commits/repair-suite");
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, pluginUri);
  assert(plugin);
  const uint32_t nports = lilv_plugin_get_num_ports(plugin);
  std::printf("ports: %u\n", nports);
  assert(nports >= 4 + repair::P_COUNT);

  LilvInstance* inst = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  assert(inst);

  static float audioInL[256], audioInR[256], audioOutL[256], audioOutR[256];
  static float values[512] = {};
  for (int i = 0; i < 256; ++i) {
    audioInL[i] = audioInR[i] =
        0.1f * std::sin(2.0 * 3.141592653589793 * 220.0 * i / 48000.0);
  }
  lilv_instance_connect_port(inst, 0, audioInL);
  lilv_instance_connect_port(inst, 1, audioInR);
  lilv_instance_connect_port(inst, 2, audioOutL);
  lilv_instance_connect_port(inst, 3, audioOutR);
  for (uint32_t i = 4; i < nports; ++i) {
    values[i] = 0.0f;
    lilv_instance_connect_port(inst, i, &values[i]);
  }
  for (uint32_t i = 0; i < repair::P_COUNT; ++i) {
    values[4 + i] = static_cast<float>(repair::kParamSpecs[i].def);
  }
  values[4 + repair::P_POWER] = 1.0f;
  values[4 + repair::P_INPUT] = 0.0f;
  values[4 + repair::P_OUTPUT] = 0.0f;

  lilv_instance_activate(inst);
  lilv_instance_run(inst, 256);
  for (int i = 0; i < 256; ++i) {
    assert(std::isfinite(audioOutL[i]) && std::isfinite(audioOutR[i]));
  }
  const uint32_t latencyPort = 4 + repair::P_COUNT + 16;
  std::printf("latency port: %.0f samples\n", values[latencyPort]);
  assert(values[latencyPort] > 0.0f);

  lilv_instance_deactivate(inst);
  lilv_instance_free(inst);
  lilv_world_free(world);
  std::printf("LV2 load/process ok\n");
  return 0;
}
