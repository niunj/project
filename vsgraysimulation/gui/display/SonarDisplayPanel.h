#pragma once

#include "SensorPanels.h"
#include <vsgraysim/broker/SonarFrame.h>
#include <deque>

class QLabel;
class QPushButton;

namespace vsgraysim::gui::display {

class SonarDisplayPanel final : public DisplayPanel {
public:
    explicit SonarDisplayPanel(QWidget* parent);
    void tick(EquipmentSession*) override;
private:
    class DepthHistory;
    QLabel *status_,*reading_,*timing_;
    QPushButton* power_;
    DepthHistory* history_;
    EquipmentSession* session_=nullptr;
    uint64_t generation_=0;
};

}
