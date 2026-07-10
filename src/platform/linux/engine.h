// Copyright 2026 Paiza. All rights reserved.
//
// IBus engine type: a thin GObject adapter around paiza::InputSession
// (all key policy lives there — keep this layer free of input logic so
// the platforms stay feature-identical; see docs/linux-port.md).

#ifndef PAIZA_LINUX_ENGINE_H_
#define PAIZA_LINUX_ENGINE_H_

#include <ibus.h>

G_BEGIN_DECLS

#define PAIZA_TYPE_ENGINE (paiza_engine_get_type())

GType paiza_engine_get_type(void);

G_END_DECLS

#endif  // PAIZA_LINUX_ENGINE_H_
