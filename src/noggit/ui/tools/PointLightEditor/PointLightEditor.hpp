// This file is part of Noggit3, licensed under GNU General Public License (version 3).



#pragma once



#include <QWidget>



class QTableWidget;

class MapView;

class QKeyEvent;



#include <functional>



namespace Noggit

{

  class PointLightTool;

}



namespace Noggit::Ui::Tools

{

  class PointLightPropertyDialog;



  class PointLightEditor final : public QWidget

  {

    Q_OBJECT



  public:

    PointLightEditor(Noggit::PointLightTool* tool, ::MapView* mapView, QWidget* parent = nullptr);



    void refreshFromWorld();

    void openPropertyDialog(std::optional<std::size_t> light_index);



    void copySelectedToClipboard();

    void pasteFromClipboard();

    [[nodiscard]] bool clipboardHasLight() const;

    void deleteSelectedLight();



  protected:

    void keyPressEvent(QKeyEvent* event) override;



  private:

    Noggit::PointLightTool* _tool = nullptr;

    ::MapView* _mapView = nullptr;

    QTableWidget* _lightTable = nullptr;

    std::function<void()> _deleteSelected;



    void syncListSelectionToWorld(std::size_t index);

    void selectWorldIndexInTable(std::size_t index);

    void clearTableSelection();

    [[nodiscard]] std::optional<std::size_t> selectedLightIndex() const;

    [[nodiscard]] std::uint32_t allocateNextLightId() const;

  };

}


