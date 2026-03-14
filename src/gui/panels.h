#pragma once

// This is the header partner for panels.cpp.  It does not declare any new
// symbols itself – the actual GUI class definition (including the panel
// helpers) lives in window.h – but having a dedicated header makes it easier
// for other parts of the code to include the panel API explicitly if desired.
//
// The file simply pulls in the full Gui declaration.

#include "gui/window.h"
