#pragma once
#include <QWidget>
#include <functional>
#include <future>
#include <optional>
#include <vsgraysim/earth/VoxelScene.h>
class QComboBox;
class QLabel;
namespace vsgraysim::ui {
// Uses the existing designer's authoritative world and publication transaction.
class SceneAuthoringPanel final : public QWidget {
  public:
    SceneAuthoringPanel(std::function<earth::VoxelScene *()> world, std::function<void()> changed,
                        QWidget *parent = nullptr);

  private:
    std::function<earth::VoxelScene *()> world_;
    std::function<void()> changed_;
    QComboBox *objects_;
    QLabel *status_;
    std::optional<earth::VoxelScene::ImportPreparation> import_;
    earth::VoxelSceneSnapshotPtr importBase_;
    void refresh();
    void sculpt();
    void material();
    void environment();
    void import(bool terrain);
    void run(std::function<void()>);
    earth::VoxelScene &world();
    std::string selected();
};
} // namespace vsgraysim::ui
