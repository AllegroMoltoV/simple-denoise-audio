#pragma once

// Implemented in the static library and exported by plugin-entry.cpp, as clap-wrapper's clap-first layout expects.
bool plugin_entry_init(const char *pluginPath);
void plugin_entry_deinit(void);
const void *plugin_entry_get_factory(const char *factoryId);
