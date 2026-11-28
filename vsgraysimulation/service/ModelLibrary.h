#pragma once

#include <QString>
#include <QByteArray>
#include <nlohmann/json.hpp>

namespace vsgraysim::service {
class ModelLibrary {
public:
    using Json = nlohmann::json;
    explicit ModelLibrary(QString root);
    Json list() const;
    Json get(const std::string& id) const;
    Json update(const std::string& id, const Json&);
    Json importGlb(const QByteArray& data);
    Json renderSpec(const std::string& id, const std::string& representation) const;
    std::string matchingModel(const QString& scene, const Json& object) const;
    Json sensorBinding(const std::string& id, const Json& expectedRevision) const;

private:
    Json entry(const std::string& id) const;
    Json publicEntry(const Json&) const;
    QString root_, directory_;
};
}
