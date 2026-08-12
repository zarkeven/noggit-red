// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/DBC.h>
#include <noggit/MapHeaders.h>
#include <noggit/application/Utils.hpp>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/ui/Checkbox.hpp>
#include <noggit/ui/pushbutton.hpp>
#include <noggit/ui/Water.h>
#include <noggit/unsigned_int_property.hpp>
#include <noggit/World.h>

#include <blizzard-database-library/include/BlizzardDatabase.h>
#include <blizzard-database-library/include/BlizzardDatabaseTable.h>

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QRadioButton>

#include <cctype>
#include <sstream>

namespace
{
  bool is_wmo_only_liquid_id(int liquid_id)
  {
    return liquid_id == LIQUID_WMO_Water || liquid_id == LIQUID_WMO_Ocean
        || liquid_id == LIQUID_WMO_Water_Interior
        || liquid_id == LIQUID_WMO_Magma || liquid_id == LIQUID_WMO_Slime;
  }

  std::string to_lower_ascii(std::string s)
  {
    for (auto& ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  }

  liquid_basic_types liquid_type_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    auto sbit = row.Columns.find("SoundBank");
    if (sbit != row.Columns.end() && !sbit->second.Value.empty())
    {
      try
      {
        int const bank = std::stoi(sbit->second.Value);
        if (bank >= 0 && bank <= 3)
          return static_cast<liquid_basic_types>(bank);
      }
      catch (...)
      {
      }
    }

    std::string name;
    auto nit = row.Columns.find("Name");
    if (nit != row.Columns.end())
      name = to_lower_ascii(nit->second.Value);

    if (name.find("ocean") != std::string::npos)
      return liquid_basic_types_ocean;
    if (name.find("magma") != std::string::npos || name.find("lava") != std::string::npos)
      return liquid_basic_types_magma;
    if (name.find("slime") != std::string::npos || name.find("ooze") != std::string::npos
        || name.find("plague") != std::string::npos)
      return liquid_basic_types_slime;
    return liquid_basic_types_water;
  }

  unsigned liquid_id_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    if (row.RecordId > 0)
      return static_cast<unsigned>(row.RecordId);
    auto it = row.Columns.find("ID");
    if (it != row.Columns.end() && !it->second.Value.empty())
    {
      try { return static_cast<unsigned>(std::stoul(it->second.Value)); }
      catch (...) {}
    }
    return 0;
  }
}

namespace Noggit
{
  namespace Ui
  {
    water::water ( unsigned_int_property* current_layer
                 , BoolToggleProperty* display_all_layers
                 , QWidget* parent
                 )
      : QWidget (parent)
      , _liquid_id(5)
      , _liquid_type(liquid_basic_types_water)
      , _radius(10.0f)
      , _angle(10.0f)
      , _orientation(0.0f)
      , _locked(false)
      , _angled_mode(false)
      , _override_liquid_id(true)
      , _override_height(true)
      , _opacity_mode(auto_opacity)
      , _custom_opacity_factor(RIVER_OPACITY_VALUE)
      , _lock_pos(glm::vec3(0.0f, 0.0f, 0.0f))
      , tile(0, 0)
    {
      setMinimumWidth(250);
      // setMaximumWidth(250);

      auto layout (new QFormLayout (this));

      auto brush_group(new QGroupBox("Brush", this));
      auto brush_layout (new QFormLayout (brush_group));

      _radius_spin = new QDoubleSpinBox (this);
      _radius_spin->setRange (0.f, 1000.f);
      connect ( _radius_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _radius = f; }
              );
      _radius_spin->setValue(_radius);
      brush_layout->addRow ("Radius", _radius_spin);

      waterType = new QComboBox(this);

      auto add_liquid_item = [&](int liquid_id, std::string const& name, liquid_basic_types type)
      {
        if (is_wmo_only_liquid_id(liquid_id))
          return;

        _liquid_names[liquid_id] = name;
        _liquid_types[liquid_id] = type;

        std::stringstream ss;
        ss << liquid_id << "-" << name;
        waterType->addItem (QString::fromUtf8(ss.str().c_str()), QVariant (liquid_id));
      };

      if (gLiquidTypeDB.getRecordCount() > 0)
      {
        for (DBCFile::Iterator i = gLiquidTypeDB.begin(); i != gLiquidTypeDB.end(); ++i)
        {
          int liquid_id = i->getInt(LiquidTypeDB::ID);
          add_liquid_item(liquid_id
                         , LiquidTypeDB::getLiquidName(liquid_id)
                         , static_cast<liquid_basic_types>(LiquidTypeDB::getLiquidType(liquid_id)));
        }
      }
      else if (auto* project = Noggit::Project::CurrentProject::get();
               project && project->ClientDatabase)
      {
        // Shadowlands: LiquidType.dbc is not opened; enumerate LiquidType.db2.
        try
        {
          auto& table = const_cast<BlizzardDatabaseLib::BlizzardDatabaseTable&>(
            project->ClientDatabase->LoadTable("LiquidType", readFileAsIMemStream));
          auto iterator = table.Records();
          while (iterator.HasRecords())
          {
            auto const& row = iterator.Next();
            unsigned const liquid_id = liquid_id_from_modern_row(row);
            if (!liquid_id)
              continue;

            std::string name = "Liquid";
            auto nit = row.Columns.find("Name");
            if (nit != row.Columns.end() && !nit->second.Value.empty())
              name = nit->second.Value;

            add_liquid_item(static_cast<int>(liquid_id), name, liquid_type_from_modern_row(row));
          }
          project->ClientDatabase->UnloadTable("LiquidType");
        }
        catch (...)
        {
        }
      }

      if (waterType->count() == 0)
      {
        // Minimal fallbacks so the tool remains usable without DB tables.
        add_liquid_item(1, "Water", liquid_basic_types_water);
        add_liquid_item(2, "Ocean", liquid_basic_types_ocean);
        add_liquid_item(3, "Magma", liquid_basic_types_magma);
        add_liquid_item(4, "Slime", liquid_basic_types_slime);
        add_liquid_item(5, "Slow Water", liquid_basic_types_water);
      }

      connect (waterType, qOverload<int> (&QComboBox::currentIndexChanged)
              , [&]
                {
                  changeWaterType(waterType->currentData().toInt());

                  // change auto opacity based on liquid type
                  if (_opacity_mode == custom_opacity || _opacity_mode == auto_opacity)
                      return;

                  // other liquid types shouldn't use opacity(depth)
                  int liquid_type = static_cast<int>(_liquid_type);
                  if (auto it = _liquid_types.find(_liquid_id); it != _liquid_types.end())
                    liquid_type = static_cast<int>(it->second);
                  else
                    liquid_type = LiquidTypeDB::getLiquidType(_liquid_id);

                  if (liquid_type == liquid_basic_types_ocean) // ocean
                  {
                      ocean_button->setChecked(true);
                      _opacity_mode = ocean_opacity;
                  }
                  else // water. opacity doesn't matter for lava/slim
                  {
                      river_button->setChecked(true);
                      _opacity_mode = river_opacity;
                  }

                }
              );

      brush_layout->addRow (waterType);

      layout->addRow (brush_group);

      auto angle_group (new QGroupBox ("Angled mode", this));
      angle_group->setCheckable (true);
      angle_group->setChecked (_angled_mode.get());
      
      
      connect ( &_angled_mode, &BoolToggleProperty::changed
              , angle_group, &QGroupBox::setChecked
              );
      connect ( angle_group, &QGroupBox::toggled
              , &_angled_mode, &BoolToggleProperty::set
              );
      auto angle_layout (new QFormLayout (angle_group));

      _angle_spin = new QDoubleSpinBox (this);
      _angle_spin->setRange (0.00001f, 89.f);
      _angle_spin->setSingleStep (2.0f);
      connect ( _angle_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _angle = f; }
              );
      _angle_spin->setValue(_angle);
      angle_layout->addRow ("Angle", _angle_spin);

      _orientation_spin = new QDoubleSpinBox (this);
      _orientation_spin->setRange (0.f, 360.f);
      _orientation_spin->setWrapping (true);
      _orientation_spin->setValue(_orientation);
      _orientation_spin->setSingleStep (5.0f);
      connect ( _orientation_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _orientation = f; }
              );

      angle_layout->addRow ("Orienation", _orientation_spin);

      layout->addRow (angle_group);

      auto lock_group (new QGroupBox ("Lock", this));
      lock_group->setCheckable (true);
      lock_group->setChecked (_locked.get());
      auto lock_layout (new QFormLayout (lock_group));

      lock_layout->addRow("X:", _x_spin = new QDoubleSpinBox (this));
      lock_layout->addRow("Z:", _z_spin = new QDoubleSpinBox (this));
      lock_layout->addRow("H:", _h_spin = new QDoubleSpinBox (this));

      _x_spin->setRange (std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
      _z_spin->setRange (std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
      _h_spin->setRange (std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
      _x_spin->setDecimals (2);
      _z_spin->setDecimals (2);
      _h_spin->setDecimals (2);

      connect ( _x_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _lock_pos.x = f; }
              );
      connect ( _z_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _lock_pos.z = f; }
              );
      connect ( _h_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _lock_pos.y = f; }
              );

      connect ( &_locked, &BoolToggleProperty::changed
              , lock_group, &QGroupBox::setChecked
              );
      connect ( lock_group, &QGroupBox::toggled
              , &_locked, &BoolToggleProperty::set
              );

      layout->addRow(lock_group);

      auto override_group (new QGroupBox ("Override", this));
      auto override_layout (new QFormLayout (override_group));

      override_layout->addWidget (new CheckBox ("Liquid ID", &_override_liquid_id, this));
      override_layout->addWidget (new CheckBox ("Height", &_override_height, this));

      layout->addRow(override_group);

      auto opacity_group (new QGroupBox ("Auto opacity", this));
      auto opacity_layout (new QFormLayout (opacity_group));

      auto auto_button(new QRadioButton("Auto", this));
      auto_button->setToolTip("Automatically uses river or ocean opacity based on liquid type.");
      river_button = new QRadioButton ("River", this);
      river_button->setToolTip(std::to_string(RIVER_OPACITY_VALUE).c_str());
      ocean_button = new QRadioButton ("Ocean", this);
      ocean_button->setToolTip(std::to_string(OCEAN_OPACITY_VALUE).c_str());
      custom_button = new QRadioButton ("Custom factor:", this);

      transparency_toggle = new QButtonGroup (this);
      transparency_toggle->addButton(auto_button, auto_opacity);
      transparency_toggle->addButton (river_button, river_opacity);
      transparency_toggle->addButton (ocean_button, ocean_opacity);
      transparency_toggle->addButton (custom_button, custom_opacity);

      connect ( transparency_toggle, qOverload<int> (&QButtonGroup::idClicked)
              , [&] (int id) { _opacity_mode = id; }
              );

      opacity_layout->addRow(auto_button);
      opacity_layout->addRow (river_button);
      opacity_layout->addRow (ocean_button);
      opacity_layout->addRow (custom_button);

      transparency_toggle->button (_opacity_mode)->setChecked (true);

      QDoubleSpinBox *opacity_spin = new QDoubleSpinBox (this);
      opacity_spin->setRange (0.f, 1.f);
      opacity_spin->setDecimals (4);
      opacity_spin->setSingleStep (0.02f);
      opacity_spin->setValue(_custom_opacity_factor);
      connect ( opacity_spin, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [&] (float f) { _custom_opacity_factor = f; }
              );
      opacity_layout->addRow (opacity_spin);

      layout->addRow (opacity_group);

      layout->addRow ( new pushbutton
                            ( "Regen ADT opacity"
                            , [this]
                              {
                                emit regenerate_water_opacity
                                  (get_opacity_factor());
                              }
                            )
                        );
      layout->addRow ( new pushbutton
                            ( "Crop water"
                            , [this]
                              {
                                emit crop_water();
                              }
                            )
                        );

      auto layer_group (new QGroupBox ("Layers", this));
      auto layer_layout (new QFormLayout (layer_group));

      layer_layout->addRow (new CheckBox("Show all layers", display_all_layers));
      layer_layout->addRow (new QLabel("Current layer:", this));

      waterLayer = new QSpinBox (this);
      waterLayer->setValue (current_layer->get());
      waterLayer->setRange (0, 100);
      layer_layout->addRow (waterLayer);

      layout->addRow (layer_group);

      connect ( waterLayer, qOverload<int> (&QSpinBox::valueChanged)
              , current_layer, &unsigned_int_property::set
              );
      connect ( current_layer, &unsigned_int_property::changed
              , waterLayer, &QSpinBox::setValue
              );

      updateData();

    }

    void water::updatePos(TileIndex const& newTile)
    {
      if (newTile == tile) return;

      tile = newTile;

      updateData();
    }

    void water::updateData()
    {
      std::string name = LiquidTypeDB::getLiquidName(_liquid_id);
      if (auto it = _liquid_names.find(_liquid_id); it != _liquid_names.end())
        name = it->second;

      std::stringstream mt;
      mt << _liquid_id << " - " << name;
      waterType->setCurrentText (QString::fromStdString (mt.str()));

      if (auto it = _liquid_types.find(_liquid_id); it != _liquid_types.end())
        _liquid_type = it->second;
      else
        _liquid_type = static_cast<liquid_basic_types>(LiquidTypeDB::getLiquidType(_liquid_id));
    }

    void water::changeWaterType(int waterint)
    {
      _liquid_id = waterint;

      updateData();
    }

    void water::changeRadius(float change)
    {
      _radius_spin->setValue(_radius + change);
    }

    void water::setRadius(float radius)
    {
      _radius_spin->setValue(radius);
    }

    void water::changeOrientation(float change)
    {
      _orientation += change;

      while (_orientation >= 360.0f)
      {
        _orientation -= 360.0f;
      }
      while (_orientation < 0.0f)
      {
        _orientation += 360.0f;
      }

      _orientation_spin->setValue(_orientation);
    }

    void water::changeAngle(float change)
    {
      _angle_spin->setValue(_angle + change);
    }

    void water::change_height(float change)
    {
      _h_spin->setValue(_lock_pos.y + change);
    }

    void water::paintLiquid (World* world, glm::vec3 const& pos, bool add)
    {
      world->paintLiquid ( pos
                         , _radius
                         , _liquid_id
                         , add
                         , math::degrees (_angled_mode.get() ? _angle : 0.0f)
                         , math::degrees (_angled_mode.get() ? _orientation : 0.0f)
                         , _locked.get()
                         , _lock_pos
                         , _override_height.get()
                         , _override_liquid_id.get()
                         , get_opacity_factor()
                         );
    }

    void water::lockPos(glm::vec3 const& cursor_pos)
    {
      QSignalBlocker const blocker_x(_x_spin);
      QSignalBlocker const blocker_z(_z_spin);
      QSignalBlocker const blocker_h(_h_spin);
      _lock_pos = cursor_pos;

      _x_spin->setValue(_lock_pos.x);
      _z_spin->setValue(_lock_pos.z);
      _h_spin->setValue(_lock_pos.y);

      if (!_locked.get())
      {
        toggle_lock();
      }
    }

    void water::toggle_lock()
    {
      _locked.toggle();
    }

    void water::toggle_angled_mode()
    {
      _angled_mode.toggle();
    }

    float water::brushRadius() const
    {
      return _radius;
    }

    float water::angle() const
    {
      return _angle;
    }

    float water::orientation() const
    {
      return _orientation;
    }

    bool water::angled_mode() const
    {
      return _angled_mode.get();
    }

    bool water::use_ref_pos() const
    {
      return _locked.get();
    }

    glm::vec3 water::ref_pos() const
    {
      return _lock_pos;
    }

    float water::get_opacity_factor() const
    {
      switch (_opacity_mode)
      {
      default:          // values found by experimenting
      case river_opacity:  return RIVER_OPACITY_VALUE;
      case ocean_opacity:  return OCEAN_OPACITY_VALUE;
      case custom_opacity: return _custom_opacity_factor;
      case auto_opacity:
      {
        switch (_liquid_type)
        {
        case 0: return RIVER_OPACITY_VALUE;
        case 1: return OCEAN_OPACITY_VALUE;
        default:  return RIVER_OPACITY_VALUE; // lava and slime, opacity isn't used
        }
      }
      break;
      }
    }

    QSize water::sizeHint() const
    {
      return QSize(250, height());
    }
  }
}
