// The install wizard (Win32 PropertySheet, PSH_AEROWIZARD). Uninstall has no wizard of its own
// (see main.cpp) -- only the install side needs the multi-page flow the spec asks for.
#pragma once

#include "Args.h"

namespace kick {

// Runs the modal wizard to completion (or Cancel). Returns a process exit code.
int RunInstallWizard(const ParsedArgs& args);

} // namespace kick
