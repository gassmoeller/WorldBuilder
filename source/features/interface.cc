/*
  Copyright (C) 2018 - 2021 by the authors of the World Builder code.

  This file is part of the World Builder.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "world_builder/features/interface.h"

#include <algorithm>

#include "world_builder/types/array.h"
#include "world_builder/types/object.h"
#include "world_builder/types/point.h"
#include "world_builder/world.h"

namespace WorldBuilder
{
  using namespace Utilities;

  namespace Features
  {
    namespace Internal
    {



      /**
       * A function to turn strings into interpolation type enums.
       */
      static InterpolationType string_to_interpolation_type (const std::string &string)
      {
        if (string == "none")
          {
            return InterpolationType::None;
          }
        if (string == "linear")
          {
            return InterpolationType::Linear;
          }
        if  (string == "monotone spline")
          {
            return InterpolationType::MonotoneSpline;
          }
        if (string == "continuous monotone spline")
          {
            return InterpolationType::ContinuousMonotoneSpline;
          }

        WBAssertThrow(false,
                      "You provided an interpolation type which is not supported: " << string <<
                      ". The options are none, linear, monotone spline and continuous monotone spline.");

        return InterpolationType::Invalid;
      }
    } // namespace Internal
    Interface::Interface()
      = default;

    Interface::~Interface ()
      = default;

    void
    Interface::declare_entries(Parameters &prm, const std::string &parent_name, const std::vector<std::string> &required_entries)
    {

      unsigned int counter = 0;
      for (auto  &it : get_declare_map())
        {
          prm.enter_subsection("oneOf");
          {
            prm.enter_subsection(std::to_string(counter));
            {

              prm.enter_subsection("properties");
              {
                prm.declare_entry("", Types::Object(required_entries), "feature object");

                prm.declare_entry("model", Types::String("",it.first),
                                  "The name which the user has given to the feature.");
                prm.declare_entry("name", Types::String(""),
                                  "The name which the user has given to the feature.");
                prm.declare_entry("coordinates", Types::Array(Types::Point<2>(), 1),
                                  "An array of 2d Points representing an array of coordinates where the feature is located.");

                prm.declare_entry("interpolation",Types::String("global"),
                                  "What type of interpolation should be used to enforce the minimum points per "
                                  "distance parameter. Options are global, none, linear, monotone spline and "
                                  "continuous monotone spline interpolation. If this "
                                  "value is set to global, the global value for interpolation is used.");
                WBAssert(it.second != NULL, "No declare entries given.");
                it.second(prm, parent_name, {});
              }
              prm.leave_subsection();
            }
            prm.leave_subsection();
          }
          prm.leave_subsection();

          counter++;

        }
    }
    void
    Interface::declare_interface_entries(Parameters &prm,
                                         const CoordinateSystem  /*unused*/)
    {
      this->coordinates = prm.get_vector<Point<2> >("coordinates");
    }

    void
    Interface::get_coordinates(const std::string & /*unused*/,
                               Parameters &prm,
                               const CoordinateSystem coordinate_system)
    {
      coordinates = prm.get_vector<Point<2> >("coordinates");
      if (coordinate_system == CoordinateSystem::spherical)
        std::transform(coordinates.begin(),coordinates.end(), coordinates.begin(),
                       [](const WorldBuilder::Point<2> &p) -> WorldBuilder::Point<2> { return p *const_pi / 180.0;});


      // If global is given, we use the global interpolation setting, otherwise use the provided value.
      const std::string interpolation_type_string = prm.get<std::string>("interpolation") == "global" ? this->world->interpolation : prm.get<std::string>("interpolation");
      interpolation_type = WorldBuilder::Features::Internal::string_to_interpolation_type(interpolation_type_string);

      // the one_dimensional_coordinates is always needed, so fill it.
      original_number_of_coordinates = coordinates.size();

      std::vector<double> one_dimensional_coordinates_local(original_number_of_coordinates,0.0);
      for (size_t j=0; j<original_number_of_coordinates; ++j)
        {
          one_dimensional_coordinates_local[j] = static_cast<double>(j);
        }

      if (interpolation_type != WorldBuilder::Utilities::InterpolationType::None)
        {
          WBAssert(interpolation_type == WorldBuilder::Utilities::InterpolationType::Linear ||
                   interpolation_type == WorldBuilder::Utilities::InterpolationType::MonotoneSpline ||
                   interpolation_type == WorldBuilder::Utilities::InterpolationType::ContinuousMonotoneSpline,
                   "For interpolation, linear and monotone spline are the only allowed values. "
                   << "You provided " << interpolation_type_string << ".");

          double maximum_distance_between_coordinates = this->world->maximum_distance_between_coordinates *
                                                        (coordinate_system == CoordinateSystem::spherical ? const_pi / 180.0 : 1.0);


          // I don't think this is usefull for continuous monotone spline, although it might
          // help in a spherical case like for the linear case.
          std::vector<double> x_list(original_number_of_coordinates,0.0);
          std::vector<double> y_list(original_number_of_coordinates,0.0);
          std::vector<Point<2> > coordinate_list_local = coordinates;
          for (size_t j=0; j<original_number_of_coordinates; ++j)
            {
              x_list[j] = coordinates[j][0];
              y_list[j] = coordinates[j][1];
            }

          x_spline.set_points(one_dimensional_coordinates_local,
                              x_list,
                              interpolation_type != WorldBuilder::Utilities::InterpolationType::Linear);
          y_spline.set_points(one_dimensional_coordinates_local,
                              y_list,
                              interpolation_type != WorldBuilder::Utilities::InterpolationType::Linear);

          if (maximum_distance_between_coordinates > 0 && interpolation_type != WorldBuilder::Utilities::InterpolationType::ContinuousMonotoneSpline)
            {
              size_t additional_parts = 0;
              for (size_t i_plane=0; i_plane<original_number_of_coordinates-1; ++i_plane)
                {
                  const Point<2> P1 (x_spline(one_dimensional_coordinates_local[i_plane + additional_parts]),
                                     y_spline(one_dimensional_coordinates_local[i_plane + additional_parts]),
                                     coordinate_system);

                  const Point<2> P2 (x_spline(one_dimensional_coordinates_local[i_plane + additional_parts + 1]),
                                     y_spline(one_dimensional_coordinates_local[i_plane  + additional_parts+ 1]),
                                     coordinate_system);

                  const double length = (P1 - P2).norm();
                  const size_t parts = static_cast<size_t>(std::ceil(length / maximum_distance_between_coordinates));
                  for (size_t j = 1; j < parts; j++)
                    {
                      const double x_position3 = static_cast<double>(i_plane) + static_cast<double>(j)/static_cast<double>(parts);
                      const Point<2> P3(x_spline(x_position3), y_spline(x_position3), coordinate_system);
                      one_dimensional_coordinates_local.insert(one_dimensional_coordinates_local.begin() + static_cast<std::vector<double>::difference_type>(additional_parts + i_plane + 1), x_position3);
                      coordinate_list_local.insert(coordinate_list_local.begin() + static_cast<std::vector<double>::difference_type>(additional_parts + i_plane + 1), P3);
                      additional_parts++;
                    }
                }
              coordinates = coordinate_list_local;
            }
        }
      one_dimensional_coordinates = one_dimensional_coordinates_local;
    }


    void
    Interface::registerType(const std::string &name,
                            void ( *declare_entries)(Parameters &, const std::string &,const std::vector<std::string> &),
                            ObjectFactory *factory)
    {
      get_factory_map()[name] = factory;
      get_declare_map()[name] = declare_entries;
    }

    std::unique_ptr<Interface>
    Interface::create(const std::string &name, WorldBuilder::World *world)
    {
      std::string lower_case_name;
      std::transform(name.begin(),
                     name.end(),
                     std::back_inserter(lower_case_name),
                     ::tolower);;

      // Have a nice assert message to check whether a plugin exists in the case
      // of a debug compilation.
      WBAssertThrow(get_factory_map().find(lower_case_name) != get_factory_map().end(),
                    "Internal error: Plugin with name '" << lower_case_name << "' is not found. "
                    "The size of factories is " << get_factory_map().size() << ".");

      // Using at() because the [] will just insert values
      // which is undesirable in this case. An exception is
      // thrown when the name is not present.
      return get_factory_map().at(lower_case_name)->create(world);
    }

  } // namespace Features
} // namespace WorldBuilder

