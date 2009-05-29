// inremental.h -- incremental linking support for gold   -*- C++ -*-

// Copyright 2009 Free Software Foundation, Inc.
// Written by Mikolaj Zalewski <mikolajz@google.com>.

// This file is part of gold.

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
// MA 02110-1301, USA.

#ifndef GOLD_INCREMENTAL_H
#define GOLD_INCREMENTAL_H

#include <map>
#include <vector>

#include "stringpool.h"
#include "workqueue.h"

namespace gold
{

class Archive;
class Input_argument;
class Incremental_inputs_checker;
class Object;
class Output_section_data;

// Incremental input type as stored in .gnu_incremental_inputs.

enum Incremental_input_type
{
  INCREMENTAL_INPUT_INVALID = 0,
  INCREMENTAL_INPUT_OBJECT = 1,
  INCREMENTAL_INPUT_ARCHIVE = 2,
  INCREMENTAL_INPUT_SHARED_LIBRARY = 3,
  INCREMENTAL_INPUT_SCRIPT = 4
};

// This class contains the information needed during an incremental
// build about the inputs necessary to build the .gnu_incremental_inputs.
class Incremental_inputs
{
 public:
  Incremental_inputs()
    : lock_(new Lock()), inputs_(NULL), command_line_key_(0),
      strtab_(new Stringpool())
  { }
  ~Incremental_inputs() { delete this->strtab_; }

  // Record the command line.
  void
  report_command_line(int argc, const char* const* argv);

  // Record the input arguments obtained from parsing the command line.
  void
  report_inputs(const Input_arguments& inputs)
  { this->inputs_ = &inputs; }

  // Record that the input argument INPUT is an archive ARCHIVE.
  void
  report_archive(const Input_argument* input, Archive* archive);

  // Record that the input argument INPUT is to an object OBJ.
  void
  report_object(const Input_argument* input, Object* obj);

  // Record that the input argument INPUT is to an script SCRIPT.
  void
  report_script(const Input_argument* input, Script_info* script);

  // Prepare for layout.  Called from Layout::finalize.
  void
  finalize();

  // Create the content of the .gnu_incremental_inputs section.
  Output_section_data*
  create_incremental_inputs_section_data();

  // Return the .gnu_incremental_strtab stringpool.
  Stringpool*
  get_stringpool()
  { return this->strtab_; }

 private:
  // Code for each of the four possible variants of create_inputs_section_data.
  template<int size, bool big_endian>
  Output_section_data*
  sized_create_inputs_section_data();

  // Compute indexes in the order in which the inputs should appear in
  // .gnu_incremental_inputs and put file names to the stringtable.
  // This needs to be done after all the scripts are parsed.

  void
  finalize_inputs(Input_argument_list::const_iterator begin,
		  Input_argument_list::const_iterator end,
		  unsigned int* index);

  // Additional data about an input needed for an incremental link.
  // None of these pointers is owned by the structure.
  struct Input_info
  {
    Input_info()
      : type(INCREMENTAL_INPUT_INVALID), archive(NULL), object(NULL),
        script(NULL), filename_key(0), index(0)
    { }

    // Type of the file pointed by this argument.
    Incremental_input_type type;

    // Present if type == INCREMENTAL_INPUT_ARCHIVE.
    Archive* archive;

    // Present if type == INCREMENTAL_INPUT_OBJECT or
    // INCREMENTAL_INPUT_SHARED_LIBRARY.
    Object* object;

    // Present if type == INCREMENTAL_INPUT_SCRIPT.
    Script_info* script;

    // Key of the filename string in the section stringtable.
    Stringpool::Key filename_key;

    // Position of the entry information in the output section.
    unsigned int index;
  };

  typedef std::map<const Input_argument*, Input_info> Inputs_info_map;

  // A lock guarding access to inputs_ during the first phase of linking, when
  // report_ function may be called from multiple threads.
  Lock* lock_;
  
  // The list of input arguments obtained from parsing the command line.
  const Input_arguments* inputs_;

  // A map containing additional information about the input elements.
  Inputs_info_map inputs_map_;

  // The key of the command line string in the string pool.
  Stringpool::Key command_line_key_;
  // The .gnu_incremental_strtab string pool associated with the
  // .gnu_incremental_inputs.
  Stringpool* strtab_;
};

} // End namespace gold.

#endif // !defined(GOLD_INCREMENTAL_H)
