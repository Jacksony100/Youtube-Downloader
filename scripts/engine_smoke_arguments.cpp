#include "core.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 6) return 2;
    const auto generated = vdp::buildDownloadArguments(arguments[1], vdp::formatPreset(arguments[2]),
                                                       arguments[3], arguments[4], arguments[5]);
    const auto json = QJsonDocument(QJsonArray::fromStringList(generated)).toJson(QJsonDocument::Compact);
    return std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout) == static_cast<size_t>(json.size()) ? 0 : 3;
}
