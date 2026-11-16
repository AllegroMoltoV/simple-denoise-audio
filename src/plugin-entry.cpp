#include <clap/clap.h>

#include "plugin-entry.h"

extern "C" {
CLAP_EXPORT const clap_plugin_entry clap_entry = {CLAP_VERSION_INIT, plugin_entry_init, plugin_entry_deinit,
						  plugin_entry_get_factory};
}
