/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element classes in parallel.

  Copyright (C) 2015 the developers

  t8code is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  t8code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with t8code; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <t8_forest/t8_forest_adapt.h>
#include <t8_forest/t8_forest_types.h>
#include <t8_forest.h>

/* The last inserted element must be the last element of a family. */
static void
t8_forest_adapt_coarsen_recursive (t8_forest_t forest, t8_locidx_t ltreeid,
                                   t8_eclass_scheme_t * ts,
                                   sc_array_t * telement,
                                   t8_locidx_t el_coarsen,
                                   t8_locidx_t * el_inserted,
                                   t8_element_t ** el_buffer)
{
  t8_element_t       *element;
  t8_element_t       *replace;
  t8_element_t      **fam;
  t8_locidx_t         pos;
  int                 num_children, i, isfamily;
  /* el_inserted is the index of the last element in telement plus one.
   * el_coarsen is the index of the first element which could possibly
   * be coarsened. */

  T8_ASSERT (*el_inserted == (t8_locidx_t) telement->elem_count);
  T8_ASSERT (el_coarsen >= 0);
  element = t8_element_array_index (ts, telement, *el_inserted - 1);
  num_children = t8_eclass_num_children[ts->eclass];
  T8_ASSERT (t8_element_child_id (ts, element) == num_children - 1);

  fam = el_buffer;
  pos = *el_inserted - num_children;
  isfamily = 1;
  if (forest->set_replace_fn != NULL) {
    t8_element_new (ts, 1, &replace);
  }
  while (isfamily && pos >= el_coarsen && t8_element_child_id (ts, element)
         == num_children - 1) {
    isfamily = 1;
    for (i = 0; i < num_children; i++) {
      fam[i] = t8_element_array_index (ts, telement, pos + i);
      if (t8_element_child_id (ts, fam[i]) != i) {
        isfamily = 0;
        break;
      }
    }
    T8_ASSERT (!isfamily || t8_element_is_family (ts, fam));
    if (isfamily && forest->set_adapt_fn (forest, ltreeid, ts, num_children,
                                          fam) < 0) {
      *el_inserted -= num_children - 1;
      telement->elem_count = *el_inserted;
      if (forest->set_replace_fn != NULL) {
        t8_element_parent (ts, fam[0], replace);
      }
      else {
        t8_element_parent (ts, fam[0], fam[0]);
      }
      if (forest->set_replace_fn != NULL) {
        forest->set_replace_fn (forest, ltreeid, ts, num_children,
                                fam, 1, &replace);
        t8_element_copy (ts, replace, fam[0]);
      }
      element = fam[0];
    }
    else {
      /* If the elements are no family or
       * the family is not to be coarsened we abort the coarsening process */
      isfamily = 0;
    }
    pos -= num_children - 1;
  }
  if (forest->set_replace_fn != NULL) {
    t8_element_destroy (ts, 1, &replace);
  }
}

static void
t8_forest_adapt_refine_recursive (t8_forest_t forest, t8_locidx_t ltreeid,
                                  t8_eclass_scheme_t * ts,
                                  sc_list_t * elem_list,
                                  sc_array_t * telements,
                                  t8_locidx_t * num_inserted,
                                  t8_element_t ** el_buffer)
{
  t8_element_t       *insert_el;
  t8_element_t       *el_pop;
  int                 num_children;
  int                 ci;

  if (elem_list->elem_count <= 0) {
    return;
  }
  num_children = t8_eclass_num_children[ts->eclass];
  if (forest->set_replace_fn != NULL) {
    t8_element_new (ts, 1, &el_pop);
  }
  while (elem_list->elem_count > 0) {
    el_buffer[0] = (t8_element_t *) sc_list_pop (elem_list);
    if (forest->set_adapt_fn (forest, ltreeid, ts, 1, el_buffer) > 0) {
      t8_element_new (ts, num_children - 1, el_buffer + 1);
      if (forest->set_replace_fn != NULL) {
        t8_element_copy (ts, el_buffer[0], el_pop);
      }
      t8_element_children (ts, el_buffer[0], num_children, el_buffer);
      if (forest->set_replace_fn != NULL) {
        forest->set_replace_fn (forest, ltreeid, ts, 1,
                                &el_pop, num_children, el_buffer);
      }
      for (ci = num_children - 1; ci >= 0; ci--) {
        (void) sc_list_prepend (elem_list, el_buffer[ci]);
      }
    }
    else {
      insert_el = (t8_element_t *) sc_array_push (telements);
      t8_element_copy (ts, el_buffer[0], insert_el);
      t8_element_destroy (ts, 1, el_buffer);
      (*num_inserted)++;
    }
  }
  if (forest->set_replace_fn != NULL) {
    t8_element_destroy (ts, 1, &el_pop);
  }
}

/* TODO: optimize this when we own forest_from */
void
t8_forest_adapt (t8_forest_t forest)
{
  t8_forest_t         forest_from;
  sc_list_t          *refine_list = NULL;       /* This is only needed when we adapt recursively */
  sc_array_t         *telements, *telements_from;
  size_t              tt;
  t8_locidx_t         el_considered;
  t8_locidx_t         el_inserted;
  t8_locidx_t         el_coarsen;
  t8_locidx_t         num_el_from;
  t8_locidx_t         el_offset;
  size_t              num_children, zz;
  t8_tree_t           tree, tree_from;
  t8_eclass_scheme_t *tscheme;
  t8_element_t      **elements, **elements_from, *elpop;
  int                 refine;
  int                 ci;
  int                 num_elements;
#ifdef T8_ENABLE_DEBUG
  int                 is_family;
#endif

  T8_ASSERT (forest != NULL);
  T8_ASSERT (forest->set_from != NULL);
  T8_ASSERT (forest->set_adapt_recursive != -1);
  T8_ASSERT (forest->from_method == T8_FOREST_FROM_ADAPT);

  forest_from = forest->set_from;
  t8_global_productionf ("Into t8_forest_adapt from %lld total elements\n",
                         (long long) forest_from->global_num_elements);

  /* TODO: Allocate memory for the trees of forest.
   * Will we do this here or in an extra function? */
  T8_ASSERT (forest->trees->elem_count == forest_from->trees->elem_count);

  if (forest->set_adapt_recursive) {
    refine_list = sc_list_new (NULL);
  }
  forest->local_num_elements = 0;
  el_offset = 0;
  for (tt = 0; tt < forest->trees->elem_count; tt++) {
    tree = (t8_tree_t) t8_sc_array_index_topidx (forest->trees, tt);
    tree_from = (t8_tree_t) t8_sc_array_index_topidx (forest_from->trees, tt);
    telements = &tree->elements;
    telements_from = &tree_from->elements;
    num_el_from = (t8_locidx_t) telements_from->elem_count;
    tscheme = forest->scheme->eclass_schemes[tree->eclass];
    el_considered = 0;
    el_inserted = 0;
    el_coarsen = 0;
    /* TODO: this will generate problems with pyramidal elements */
    num_children = t8_eclass_num_children[tree->eclass];
    elements = T8_ALLOC (t8_element_t *, num_children);
    elements_from = T8_ALLOC (t8_element_t *, num_children);
    while (el_considered < num_el_from) {
#ifdef T8_ENABLE_DEBUG
      is_family = 1;
#endif
      num_elements = num_children;
      for (zz = 0; zz < num_children &&
           el_considered + (t8_locidx_t) zz < num_el_from; zz++) {
        elements_from[zz] = t8_element_array_index (tscheme, telements_from,
                                                    el_considered + zz);
        if ((size_t) t8_element_child_id (tscheme, elements_from[zz]) != zz) {
          break;
        }
      }
      if (zz != num_children) {
        num_elements = 1;
#ifdef T8_ENABLE_DEBUG
        is_family = 0;
#endif
      }
      T8_ASSERT (!is_family || t8_element_is_family (tscheme, elements_from));
      refine = forest->set_adapt_fn (forest, tt, tscheme, num_elements,
                                     elements_from);
      T8_ASSERT (is_family || refine >= 0);
      if (refine > 0) {
        /* The first element is to be refined */
        if (forest->set_adapt_recursive) {
          /* el_coarsen is the index of the first element in the new element
           * array which could be coarsened recursively.
           * We can set this here, since a family that emerges from a refinement will never be coarsened */
          el_coarsen = el_inserted + num_children;
          t8_element_new (tscheme, num_children, elements);
          t8_element_children (tscheme, elements_from[0], num_children,
                               elements);
          for (ci = num_children - 1; ci >= 0; ci--) {
            (void) sc_list_prepend (refine_list, elements[ci]);
          }
          if (forest->set_replace_fn) {
            forest->set_replace_fn (forest, tt, tscheme, 1,
                                    elements_from, num_children, elements);
          }
          t8_forest_adapt_refine_recursive (forest, tt, tscheme,
                                            refine_list,
                                            telements, &el_inserted,
                                            elements);
        }
        else {
          /* add the children to the element array of the current tree */
          (void) sc_array_push_count (telements, num_children);
          for (zz = 0; zz < num_children; zz++) {
            elements[zz] = t8_element_array_index (tscheme, telements,
                                                   el_inserted + zz);
          }
          t8_element_children (tscheme, elements_from[0], num_children,
                               elements);
          if (forest->set_replace_fn) {
            forest->set_replace_fn (forest, tt, tscheme, 1,
                                    elements_from, num_children, elements);
          }
          el_inserted += num_children;
        }
        el_considered++;
      }
      else if (refine < 0) {
        /* The elements form a family and are to be coarsened */
        elements[0] = (t8_element_t *) sc_array_push (telements);
        t8_element_parent (tscheme, elements_from[0], elements[0]);
        if (forest->set_replace_fn) {
          forest->set_replace_fn (forest, tt, tscheme, num_children,
                                  elements_from, 1, elements);
        }
        el_inserted++;
        if (forest->set_adapt_recursive) {
          if ((size_t) t8_element_child_id (tscheme, elements[0])
              == num_children - 1) {
            t8_forest_adapt_coarsen_recursive (forest, tt, tscheme,
                                               telements, el_coarsen,
                                               &el_inserted, elements);
          }
        }
        el_considered += num_children;
      }
      else {
        /* The considered elements are neither to be coarsened nor is the first
         * one to be refined */
        T8_ASSERT (refine == 0);
        elements[0] = (t8_element_t *) sc_array_push (telements);
        t8_element_copy (tscheme, elements_from[0], elements[0]);
        el_inserted++;
        if (forest->set_adapt_recursive &&
            (size_t) t8_element_child_id (tscheme, elements[0])
            == num_children - 1) {
          t8_forest_adapt_coarsen_recursive (forest, tt, tscheme,
                                             telements, el_coarsen,
                                             &el_inserted, elements);
        }
        el_considered++;
      }
    }
    if (forest->set_adapt_recursive) {
      while (refine_list->elem_count > 0) {
        SC_ABORT_NOT_REACHED ();
        elpop = (t8_element_t *) sc_list_pop (refine_list);
        elements[0] = (t8_element_t *) sc_array_push (telements);
        t8_element_copy (tscheme, elpop, elements[0]);
        t8_element_destroy (tscheme, 1, &elpop);
        el_inserted++;
      }
    }
    tree->elements_offset = el_offset;
    el_offset += el_inserted;
    forest->local_num_elements += el_inserted;
    sc_array_resize (telements, el_inserted);

    T8_FREE (elements);
    T8_FREE (elements_from);
    /* TODO: compute tree->element_offset */
  }
  if (forest->set_adapt_recursive) {
    sc_list_destroy (refine_list);
  }
  t8_forest_comm_global_num_elements (forest);
  t8_global_productionf ("Done t8_forest_adapt with %lld total elements\n",
                         (long long) forest->global_num_elements);
}
