/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element classes in parallel.

  Copyright (C) 2010 The University of Texas System
  Written by Carsten Burstedde, Lucas C. Wilcox, and Tobin Isaac

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

/** \file t8_element.h
 * This file defines basic operations on an element in a refinement tree.
 *
 * All operations work for all element classes by providing a virtual function table.
 * For each element class, one implementation of the type and virtual table is required.
 */

#ifndef T8_ELEMENT_H
#define T8_ELEMENT_H

#include <t8_eclass.h>

T8_EXTERN_C_BEGIN ();

/** Opaque structure for a generic element, only used as pointer.
 * Applications are free to cast it to their internal element class.
 */
typedef struct t8_element t8_element_t;

/** This typedef holds virtual functions for a particular element class. */
typedef struct t8_eclass_scheme t8_eclass_scheme_t;

/* *INDENT-OFF* */
/** Return the size of the element data type in bytes.
 * \return              Data type size in bytes.
 */
typedef size_t      (*t8_element_size_t) (void);
/* *INDENT-ON* */

/** Construct the parent of a given element. */
typedef void        (*t8_element_parent_t) (const t8_element_t * elem,
                                            t8_element_t * parent);

/** Construct a given same-size sibling of a given element. */
typedef void        (*t8_element_sibling_t) (const t8_element_t * elem,
                                             int sibid,
                                             t8_element_t * sibling);

/** Construct the child element of a given number. */
typedef void        (*t8_element_child_t) (const t8_element_t * elem,
                                           int childid, t8_element_t * child);

/** Construct the nearest common ancestor of two elements in the same tree. */
typedef void        (*t8_element_nca_t) (const t8_element_t * elem1,
                                         const t8_element_t * elem2,
                                         t8_element_t * nca);

/** Construct all codimension-one boundary elements of a given element. */
typedef void        (*t8_element_boundary_t) (const t8_element_t * elem,
                                              int min_dim, int length,
                                              t8_element_t ** boundary);

/** Allocate space for the codimension-one boundary elements. */
typedef void        (*t8_element_new_t) (void *ts_context,
                                         int length, t8_element_t ** elem);

/** Deallocate space for the codimension-one boundary elements. */
typedef void        (*t8_element_destroy_t) (void *ts_context,
                                             int length,
                                             t8_element_t ** elem);

/** Destructor for the element virtual table. */
typedef void        (*t8_eclass_scheme_destroy_t) (t8_eclass_scheme_t * ts);

/** The virtual table for a particular implementation of an element class. */
struct t8_eclass_scheme
{
  /* these element routines are context free */
  t8_element_size_t   elem_size;        /**< Compute element size in bytes. */
  t8_element_parent_t elem_parent;      /**< Compute the parent element. */
  t8_element_sibling_t elem_sibling;    /**< Compute a given sibling element. */
  t8_element_child_t  elem_child;       /**< Compute a child element. */
  t8_element_nca_t    elem_nca;         /**< Compute nearest common ancestor. */
  t8_element_boundary_t elem_boundary;  /**< Compute a set of boundary elements. */

  /* these element routines have a context for memory allocation */
  t8_element_new_t    elem_new;         /**< Allocate space for one or more elements. */
  t8_element_destroy_t elem_destroy;    /**< Deallocate space for one or more elements. */

  /* variables that relate to the element class scheme itself */
  t8_eclass_scheme_destroy_t ts_destroy;        /**< Virtual destructor for this scheme. */
  void               *ts_context;               /**< Anonymous implementation context. */
};

/** The scheme holds implementations for one or more element classes. */
typedef struct t8_scheme
{
  /** This array holds one virtual table per element class. */
  t8_eclass_scheme_t *eclass_schemes[T8_ECLASS_LAST];
}
t8_scheme_t;

/** Destroy an element scheme. */
void                t8_scheme_destroy (t8_scheme_t * scheme);

/** Destroy an implementation of a particular element class. */
void                t8_eclass_scheme_destroy (t8_eclass_scheme_t * ts);

/** Allocate a set of elements suitable for the boundary of a given class.
 * \param [in] scheme           Defines the implementation of the element class.
 * \param [in] theclass         The element class whose boundary we want.
 * \param [in] min_dim          Ignore boundary points of lesser dimension.
 * \param [in] length           Must be equal to the return value
 *                              of \ref t8_eclass_count_boundary.
 * \param [in,out] boundary     On input, array of element pointers of at
 *                              least length \b length.  Filled on output.
 */
void                t8_eclass_boundary_new (t8_scheme_t * scheme,
                                            t8_eclass_t theclass, int min_dim,
                                            int length,
                                            t8_element_t ** boundary);

/** Destroy a set of elements suitable for the boundary of a given class.
 * \param [in] scheme           Defines the implementation of the element class.
 * \param [in] theclass         The element class whose boundary we have.
 * \param [in] min_dim          Ignore boundary points of lesser dimension.
 * \param [in] length           Must be equal to the return value
 *                              of \ref t8_eclass_count_boundary.
 * \param [in,out] boundary     Array of element pointers holding elements
 *                              as created by \ref t8_eclass_boundary_new.
 *                              The elements are destroyed by this function.
 */
void                t8_eclass_boundary_destroy (t8_scheme_t * scheme,
                                                t8_eclass_t theclass,
                                                int min_dim, int length,
                                                t8_element_t ** boundary);

/** Return the size of an element of a given class. */
size_t              t8_element_size (t8_eclass_scheme_t * ts);

void                t8_element_parent (t8_eclass_scheme_t * ts,
                                       const t8_element_t * elem,
                                       t8_element_t * parent);
void                t8_element_sibling (t8_eclass_scheme_t * ts,
                                        const t8_element_t * elem, int sibid,
                                        t8_element_t * sibling);
void                t8_element_child (t8_eclass_scheme_t * ts,
                                      const t8_element_t * elem, int childid,
                                      t8_element_t * child);
void                t8_element_nca (t8_eclass_scheme_t * ts,
                                    const t8_element_t * elem1,
                                    const t8_element_t * elem2,
                                    t8_element_t * nca);
void                t8_element_boundary (t8_eclass_scheme_t * ts,
                                         const t8_element_t * elem,
                                         int min_dim, int length,
                                         t8_element_t ** boundary);

void                t8_element_new (t8_eclass_scheme_t * ts,
                                    int length, t8_element_t ** elems);
void                t8_element_destroy (t8_eclass_scheme_t * ts,
                                        int length, t8_element_t ** elems);

T8_EXTERN_C_END ();

#endif /* !T8_ELEMENT_H */
