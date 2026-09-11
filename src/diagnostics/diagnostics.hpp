#pragma once
#include "core.hpp"

namespace vdp {
QString redactSecrets(const QString& text);
QString diagnosticReport(const ToolchainStatus& tools, const QString& lastError = {});
bool appendBoundedLog(const QString& directory, const QString& channel, const QString& message);
}
