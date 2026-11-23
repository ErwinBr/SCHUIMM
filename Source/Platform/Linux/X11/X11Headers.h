#pragma once

// Single entry point for Xlib/GLX headers inside the X11 backend.
//
// Xlib defines object-like macros with common names (Status, Bool, None, True, False, ...).
// Include this header *after* all engine headers, and never from an engine-facing header.
// `Status` collides with schuimm::Status and is removed here; Xlib functions that return
// Status simply return int.

#include <GL/glx.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#undef Status
