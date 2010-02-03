// gc.h -- garbage collection of unused sections

// Copyright 2009, 2010 Free Software Foundation, Inc.
// Written by Sriraman Tallam <tmsriram@google.com>.

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

#ifndef GOLD_GC_H
#define GOLD_GC_H

#include <queue>
#include <vector>

#include "elfcpp.h"
#include "symtab.h"
#include "object.h"
#include "icf.h"

namespace gold
{

class Object;

template<int size, bool big_endian>
class Sized_relobj;

template<int sh_type, int size, bool big_endian>
class Reloc_types;

class Output_section;
class General_options;
class Layout;

class Garbage_collection
{
 public:

  typedef Unordered_set<Section_id, Section_id_hash> Sections_reachable;
  typedef std::map<Section_id, Sections_reachable> Section_ref;
  typedef std::queue<Section_id> Worklist_type;
  // This maps the name of the section which can be represented as a C
  // identifier (cident) to the list of sections that have that name.
  // Different object files can have cident sections with the same name.
  typedef std::map<std::string, Sections_reachable> Cident_section_map;

  Garbage_collection()
  : is_worklist_ready_(false)
  { }

  // Accessor methods for the private members.

  Sections_reachable&
  referenced_list()
  { return referenced_list_; }

  Section_ref&
  section_reloc_map()
  { return this->section_reloc_map_; }

  Worklist_type&
  worklist()
  { return this->work_list_; }

  bool
  is_worklist_ready()
  { return this->is_worklist_ready_; }

  void
  worklist_ready()
  { this->is_worklist_ready_ = true; }

  void
  do_transitive_closure();

  bool
  is_section_garbage(Object* obj, unsigned int shndx)
  { return (this->referenced_list().find(Section_id(obj, shndx))
            == this->referenced_list().end()); }

  Cident_section_map*
  cident_sections()
  { return &cident_sections_; }

  void
  add_cident_section(std::string section_name,
		     Section_id secn)
  { this->cident_sections_[section_name].insert(secn); }

  // Add a reference from the SRC_SHNDX-th section of SRC_OBJECT to
  // DST_SHNDX-th section of DST_OBJECT.
  void
  add_reference(Object* src_object, unsigned int src_shndx,
		Object* dst_object, unsigned int dst_shndx)
  {
    Section_id src_id(src_object, src_shndx);
    Section_id dst_id(dst_object, dst_shndx);
    Section_ref::iterator p = this->section_reloc_map_.find(src_id);
    if (p == this->section_reloc_map_.end())
      this->section_reloc_map_[src_id].insert(dst_id);
    else
      p->second.insert(dst_id);
  }

 private:

  Worklist_type work_list_;
  bool is_worklist_ready_;
  Section_ref section_reloc_map_;
  Sections_reachable referenced_list_;
  Cident_section_map cident_sections_;
};

// Data to pass between successive invocations of do_layout
// in object.cc while garbage collecting.  This data structure
// is filled by using the data from Read_symbols_data.

struct Symbols_data
{
  // Section headers.
  unsigned char* section_headers_data;
  // Section names.
  unsigned char* section_names_data;
  // Size of section name data in bytes.
  section_size_type section_names_size;
  // Symbol data.
  unsigned char* symbols_data;
  // Size of symbol data in bytes.
  section_size_type symbols_size;
  // Offset of external symbols within symbol data.  This structure
  // sometimes contains only external symbols, in which case this will
  // be zero.  Sometimes it contains all symbols.
  section_offset_type external_symbols_offset;
  // Symbol names.
  unsigned char* symbol_names_data;
  // Size of symbol name data in bytes.
  section_size_type symbol_names_size;
};

// This function implements the generic part of reloc
// processing to map a section to all the sections it
// references through relocs.  It is called only during
// garbage collection (--gc-sections) and identical code
// folding (--icf).

template<int size, bool big_endian, typename Target_type, int sh_type,
	 typename Scan>
inline void
gc_process_relocs(
    Symbol_table* symtab,
    Layout*,
    Target_type* ,
    Sized_relobj<size, big_endian>* src_obj,
    unsigned int src_indx,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section*,
    bool ,
    size_t local_count,
    const unsigned char* plocal_syms)
{
  Object *dst_obj;
  unsigned int dst_indx;

  typedef typename Reloc_types<sh_type, size, big_endian>::Reloc Reltype;
  const int reloc_size = Reloc_types<sh_type, size, big_endian>::reloc_size;
  const int sym_size = elfcpp::Elf_sizes<size>::sym_size;

  std::vector<Section_id>* secvec = NULL;
  std::vector<Symbol*>* symvec = NULL;
  std::vector<std::pair<long long, long long> >* addendvec = NULL;
  bool is_icf_tracked = false;
  const char* cident_section_name = NULL;

  if (parameters->options().icf_enabled()
      && is_section_foldable_candidate(src_obj->section_name(src_indx).c_str()))
    {
      is_icf_tracked = true;
      Section_id src_id(src_obj, src_indx);
      secvec = &symtab->icf()->section_reloc_list()[src_id];
      symvec = &symtab->icf()->symbol_reloc_list()[src_id];
      addendvec = &symtab->icf()->addend_reloc_list()[src_id];
    }

  for (size_t i = 0; i < reloc_count; ++i, prelocs += reloc_size)
    {
      Reltype reloc(prelocs);
      typename elfcpp::Elf_types<size>::Elf_WXword r_info = reloc.get_r_info();
      unsigned int r_sym = elfcpp::elf_r_sym<size>(r_info);
      typename elfcpp::Elf_types<size>::Elf_Swxword addend =
      Reloc_types<sh_type, size, big_endian>::get_reloc_addend_noerror(&reloc);

      if (r_sym < local_count)
        {
          gold_assert(plocal_syms != NULL);
          typename elfcpp::Sym<size, big_endian> lsym(plocal_syms
                                                      + r_sym * sym_size);
          unsigned int shndx = lsym.get_st_shndx();
          bool is_ordinary;
          shndx = src_obj->adjust_sym_shndx(r_sym, shndx, &is_ordinary);
          if (!is_ordinary)
            continue;
          dst_obj = src_obj;
          dst_indx = shndx;
          Section_id dst_id(dst_obj, dst_indx);
          if (is_icf_tracked)
            {
              (*secvec).push_back(dst_id);
              (*symvec).push_back(NULL);
              long long symvalue = static_cast<long long>(lsym.get_st_value());
              (*addendvec).push_back(std::make_pair(symvalue,
                                              static_cast<long long>(addend)));
            }
          if (shndx == src_indx)
            continue;
        }
      else
        {
          Symbol* gsym = src_obj->global_symbol(r_sym);
          gold_assert(gsym != NULL);
          if (gsym->is_forwarder())
            gsym = symtab->resolve_forwards(gsym);
          if (gsym->source() != Symbol::FROM_OBJECT)
            continue;
          bool is_ordinary;
          dst_obj = gsym->object();
          dst_indx = gsym->shndx(&is_ordinary);
          if (!is_ordinary)
            continue;
          Section_id dst_id(dst_obj, dst_indx);
          // If the symbol name matches '__start_XXX' then the section with
          // the C identifier like name 'XXX' should not be garbage collected.
          // A similar treatment to symbols with the name '__stop_XXX'.
          if (is_prefix_of(cident_section_start_prefix, gsym->name()))
            {
              cident_section_name = (gsym->name() 
                                     + strlen(cident_section_start_prefix));
            }
          else if (is_prefix_of(cident_section_stop_prefix, gsym->name()))
            {
              cident_section_name = (gsym->name() 
                                     + strlen(cident_section_stop_prefix));
            }
          if (is_icf_tracked)
            {
              (*secvec).push_back(dst_id);
              (*symvec).push_back(gsym);
              Sized_symbol<size>* sized_gsym =
                        static_cast<Sized_symbol<size>* >(gsym);
              long long symvalue =
                        static_cast<long long>(sized_gsym->value());
              (*addendvec).push_back(std::make_pair(symvalue,
                                        static_cast<long long>(addend)));
            }
        }
      if (parameters->options().gc_sections())
        {
	  symtab->gc()->add_reference(src_obj, src_indx, dst_obj, dst_indx);
          if (cident_section_name != NULL)
            {
              Garbage_collection::Cident_section_map::iterator ele =
                symtab->gc()->cident_sections()->find(std::string(cident_section_name));
              if (ele == symtab->gc()->cident_sections()->end())
                continue;
	      Section_id src_id(src_obj, src_indx);
              Garbage_collection::Sections_reachable&
                v(symtab->gc()->section_reloc_map()[src_id]);
              Garbage_collection::Sections_reachable& cident_secn(ele->second);
              for (Garbage_collection::Sections_reachable::iterator it_v
                     = cident_secn.begin();
                   it_v != cident_secn.end();
                   ++it_v)
                {
                  v.insert(*it_v);
                }
            }
        }
    }
  return;
}

} // End of namespace gold.

#endif
