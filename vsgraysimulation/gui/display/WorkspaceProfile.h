#pragma once
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <vector>
#include <cstdint>
namespace vsgraysim::gui::display {
struct WorkspacePanel {
    QString id,kind,title;
    uint32_t equipment_id=0; // 0: first compatible bound equipment
    QString area="right";
};
struct WorkspaceProfile {
    QString id="mission",title=QStringLiteral("综合任务工作台");
    QString cockpit_profile="config/cockpits/f16c.json";
    std::vector<WorkspacePanel> panels;
    QByteArray dock_state;
    static WorkspaceProfile preset(const QString& id);
    static QStringList presetIds();
    static QStringList panelKinds();
    QByteArray serialize() const;
    static bool parse(const QByteArray&,WorkspaceProfile&,QString& error);
    bool save(const QString& path,QString& error) const;
    static bool load(const QString& path,WorkspaceProfile&,QString& error);
};
}
