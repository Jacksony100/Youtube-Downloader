#pragma once
#include "core.hpp"
#include <QObject>

namespace vdp {
class ToolchainService final : public QObject {
    Q_OBJECT
public:
    explicit ToolchainService(AppPaths paths, QObject* parent = nullptr);
    bool isBusy() const { return busy_; }
public slots:
    void ensure();
    void refresh();
    void repair();
signals:
    void ready(const vdp::ToolchainStatus& status);
    void failed(const QString& message);
private:
    void run(bool provision);
    AppPaths paths_;
    bool busy_ = false;
};
}
Q_DECLARE_METATYPE(vdp::ToolchainStatus)
