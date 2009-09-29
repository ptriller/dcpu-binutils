// target.cc

// Copyright 2009 Free Software Foundation, Inc.
// Written by Doug Kwan <dougkwan@google.com>.

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

#include "gold.h"
#include "target.h"
#include "dynobj.h"

namespace gold
{

// Return whether NAME is a local label name.  This is used to implement the
// --discard-locals options and can be overriden by children classes to
// implement system-specific behaviour.  The logic here is the same as that
// in _bfd_elf_is_local_label_name().

bool
Target::do_is_local_label_name (const char* name) const
{
  // Normal local symbols start with ``.L''.
  if (name[0] == '.' && name[1] == 'L')
    return true;

  // At least some SVR4 compilers (e.g., UnixWare 2.1 cc) generate
  // DWARF debugging symbols starting with ``..''.
  if (name[0] == '.' && name[1] == '.')
    return true;

  // gcc will sometimes generate symbols beginning with ``_.L_'' when
  // emitting DWARF debugging output.  I suspect this is actually a
  // small bug in gcc (it calls ASM_OUTPUT_LABEL when it should call
  // ASM_GENERATE_INTERNAL_LABEL, and this causes the leading
  // underscore to be emitted on some ELF targets).  For ease of use,
  // we treat such symbols as local.
  if (name[0] == '_' && name[1] == '.' && name[2] == 'L' && name[3] == '_')
    return true;

  return false;
}

// Implementations of methods Target::do_make_elf_object are almost identical
// except for the address sizes and endianities.  So we extract this
// into a template.

template<int size, bool big_endian>
inline Object*
Target::do_make_elf_object_implementation(
    const std::string& name,
    Input_file* input_file,
    off_t offset,
    const elfcpp::Ehdr<size, big_endian>& ehdr)
{
  int et = ehdr.get_e_type();
  if (et == elfcpp::ET_REL)
    {
      Sized_relobj<size, big_endian>* obj =
	new Sized_relobj<size, big_endian>(name, input_file, offset, ehdr);
      obj->setup(this);
      return obj;
    }
  else if (et == elfcpp::ET_DYN)
    {
      Sized_dynobj<size, big_endian>* obj =
	new Sized_dynobj<size, big_endian>(name, input_file, offset, ehdr);
      obj->setup(this);
      return obj;
    }
  else
    {
      gold_error(_("%s: unsupported ELF file type %d"),
		 name.c_str(), et);
      return NULL;
    }
}

// Make an ELF object called NAME by reading INPUT_FILE at OFFSET.  EHDR
// is the ELF header of the object.  There are four versions of this
// for different address sizes and endianities.

#ifdef HAVE_TARGET_32_LITTLE
Object*
Target::do_make_elf_object(const std::string& name, Input_file* input_file,
			   off_t offset, const elfcpp::Ehdr<32, false>& ehdr)
{
  return this->do_make_elf_object_implementation<32, false>(name, input_file,
							    offset, ehdr);
}
#endif

#ifdef HAVE_TARGET_32_BIG
Object*
Target::do_make_elf_object(const std::string& name, Input_file* input_file,
			   off_t offset, const elfcpp::Ehdr<32, true>& ehdr)
{
  return this->do_make_elf_object_implementation<32, true>(name, input_file,
							   offset, ehdr);
}
#endif

#ifdef HAVE_TARGET_64_LITTLE
Object*
Target::do_make_elf_object(const std::string& name, Input_file* input_file,
			   off_t offset, const elfcpp::Ehdr<64, false>& ehdr)
{
  return this->do_make_elf_object_implementation<64, false>(name, input_file,
							    offset, ehdr);
}
#endif

#ifdef HAVE_TARGET_64_BIG
Object*
Target::do_make_elf_object(const std::string& name, Input_file* input_file,
			   off_t offset, const elfcpp::Ehdr<64, true>& ehdr)
{
  return this->do_make_elf_object_implementation<64, true>(name, input_file,
							   offset, ehdr);
}
#endif

} // End namespace gold.
