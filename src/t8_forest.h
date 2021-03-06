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

/** \file t8_forest.h
 * We define the forest of trees in this file.
 */

/* TODO: begin documenting this file: make doxygen 2>&1 | grep t8_forest */

#ifndef T8_FOREST_H
#define T8_FOREST_H

#include <t8_cmesh.h>
#include <t8_element.h>

/** Opaque pointer to a forest implementation. */
typedef struct t8_forest *t8_forest_t;
typedef struct t8_tree *t8_tree_t;

T8_EXTERN_C_BEGIN ();

/* TODO: There is no user_data yet */
/* TODO: if eclass is a vertex then num_outgoing/num_incoming are always
 *       1 and it is not possible to decide whether we are rfining or coarsening.
 *       Is this an issue? */
/** Callback function prototype to replace one set of elements with another.
 *
 * This is used by the adapt routine when the elements of an existing, valid
 * forest are changed.  The callback allows the user to make changes to newly
 *
 * initialized elements before the elements that they replace are destroyed.
 *
 * \param [in] forest      the forest
 * \param [in] which_tree  the local tree containing \a outgoing and \a incoming
 * \param [in] ts          the eclass scheme of the tree
 * \param [in] num_outgoing The number of outgoing elements.
 * \param [in] outgoing     The outgoing elements: after the callback, the
 *                          user_data will be destroyed. (at the current state there is no user data)
 * \param [in] num_incoming The number of incoming elements.
 * \param [in,out] incoming The incoming elements: prior to the callback,
 *                          the user_data is allocated, and the forest_init_t callback,
 *                          if it has been provided, will be called.
 *
 * If an element is being refined, num_outgoing will be 1 and num_incoming will
 * be the number of children, and vice versa if a family is being coarsened.
 */
typedef void        (*t8_forest_replace_t) (t8_forest_t forest,
                                            t8_locidx_t which_tree,
                                            t8_eclass_scheme_t * ts,
                                            int num_outgoing,
                                            t8_element_t * outgoing[],
                                            int num_incoming,
                                            t8_element_t * incoming[]);

/** Callback function prototype to decide for refining and coarsening.
 * If the \a num_elements equals the number of children then the elements
 * form a family and we decide whether this family should be coarsened
 * or only the first element should be refined.
 * Otherwise \num_elements must equal one and we consider the first entry
 * of the element array for refinement. In this case the other entries of
 * the element array are undefined.
 * \param [in] forest      the forest
 * \param [in] which_tree  the local tree containing \a elements
 * \param [in] ts          the eclass scheme of the tree
 * \param [in] num_elements the number of entries in \a elements
 * \param [in] elements    Pointers to a family or, if second entry is NULL,
 *                         pointer to one element.
 * \return greater zero if the first entry in \a elements should be refined
 *         smaller zero if the family \a elements shall be coarsened
 *         zero else.
 */
typedef int         (*t8_forest_adapt_t) (t8_forest_t forest,
                                          t8_locidx_t which_tree,
                                          t8_eclass_scheme_t * ts,
                                          int num_elements,
                                          t8_element_t * elements[]);

  /** Create a new forest with reference count one.
 * This forest needs to be specialized with the t8_forest_set_* calls.
 * Currently it is manatory to either call the functions \ref
 * t8_forest_set_mpicomm, \ref t8_forest_set_cmesh, and \ref t8_forest_set_scheme,
 * or to call one of \ref t8_forest_set_copy, \ref t8_forest_set_adapt, or
 * \ref t8_forest_set_partition.  It is illegal to mix these calls, or to
 * call more than one of the three latter functions
 * Then it needs to be set up with \ref t8_forest_commit.
 * \param [in,out] pforest      On input, this pointer must be non-NULL.
 *                              On return, this pointer set to the new forest.
 */
void                t8_forest_init (t8_forest_t * pforest);

/** Check whether a forest is not NULL, initialized and not committed.
 * In addition, it asserts that the forest is consistent as much as possible.
 * \param [in] forest           This forest is examined.  May be NULL.
 * \return                      True if forest is not NULL,
 *                              \ref t8_forest_init has been called on it,
 *                              but not \ref t8_forest_commit.
 *                              False otherwise.
 */
int                 t8_forest_is_initialized (t8_forest_t forest);

/** Check whether a forest is not NULL, initialized and committed.
 * In addition, it asserts that the forest is consistent as much as possible.
 * \param [in] forest           This forest is examined.  May be NULL.
 * \return                      True if forest is not NULL and
 *                              \ref t8_forest_init has been called on it
 *                              as well as \ref t8_forest_commit.
 *                              False otherwise.
 */
int                 t8_forest_is_committed (t8_forest_t forest);

/** Set the cmesh associated to a forest.
 * By default, the forest takes ownership of the cmesh such that it will be
 * destroyed when the forest is destroyed.  To keep ownership of the cmesh,
 * call \ref t8_cmesh_ref before passing it to \ref t8_forest_set_cmesh.
 * This means that it is ILLEGAL to continue using cmesh or dereferencing it
 * UNLESS it is referenced directly before passing it into this function.
 * \param [in,out] forest       The forest whose cmesh variable will be set.
 * \param [in]     cmesh        The cmesh to be set.  We take ownership.
 *                              This can be prevented by referencing \b cmesh.
 */
void                t8_forest_set_cmesh (t8_forest_t forest,
                                         t8_cmesh_t cmesh, sc_MPI_Comm comm);

/** Set the element scheme associated to a forest.
 * By default, the forest takes ownership of the scheme such that it will be
 * destroyed when the forest is destroyed.  To keep ownership of the scheme, call
 * \ref t8_scheme_ref before passing it to \ref t8_forest_set_scheme.
 * This means that it is ILLEGAL to continue using scheme or dereferencing it
 * UNLESS it is referenced directly before passing it into this function.
 * \param [in,out] forest       The forest whose scheme variable will be set.
 * \param [in]     scheme       The scheme to be set.  We take ownership.
 *                              This can be prevented by referencing \b scheme.
 */
void                t8_forest_set_scheme (t8_forest_t forest,
                                          t8_scheme_t * scheme);

/** Set the initial refinement level to be used when \b forest is commited.
 * \param [in,out] forest      The forest whose level will be set.
 * \param [in]     level       The initial refinement level of \b forest, when
 *                             it is commited.
 */
void                t8_forest_set_level (t8_forest_t forest, int level);

/** Set a forest as source for copying on commiting.
 * By default, the forest takes ownership of the source \b from such that it will
 * be destroyed on calling \ref t8_forest_commit.  To keep ownership of \b
 * from, call \ref t8_forest_ref before passing it into this function.
 * This means that it is ILLEGAL to continue using \b from or dereferencing it
 * UNLESS it is referenced directly before passing it into this function.
 */
void                t8_forest_set_copy (t8_forest_t forest,
                                        const t8_forest_t from);

/** Set a source forest with an adapt function to be adapted on commiting.
 * By default, the forest takes ownership of the source \b set_from such that it will
 * be destroyed on calling \ref t8_forest_commit.  To keep ownership of \b
 * set_from, call \ref t8_forest_ref before passing it into this function.
 * This means that it is ILLEGAL to continue using \b set_from or dereferencing it
 * UNLESS it is referenced directly before passing it into this function.
 * \param [in,out] forest   The forest
 * \param [in] set_from     The source forest from which \b forest will be adapted.
 *                          We take ownership. This can be prevented by
 *                          referencing \b set_from.
 * \param [in] adapt_fn     The adapt function used on commiting.
 * \param [in] replace_fn   The replace function to be used in \b adapt_fn.
 * \param [in] recursive    A flag specifying whether adaptation is to be done recursively6
 *                          or not. If the value is zero, adaptation is not recursive
 *                          and it is recursive otherwise.
 */
void                t8_forest_set_adapt (t8_forest_t forest,
                                         const t8_forest_t set_from,
                                         t8_forest_adapt_t adapt_fn,
                                         t8_forest_replace_t replace_fn,
                                         int recursive);

/** Set the user data of a forest. This can i.e. be used to pass user defined
 * arguments to the adapt routine.
 * \param [in,out] forest   The forest
 * \param [in]     data     A pointer to user data. t8code will never touch the data.
 * The forest must not be committed before calling this function.
 * \see t8_forest_get_user_data
 */
void                t8_forest_set_user_data (t8_forest_t forest, void *data);

/** Return the user data pointer associated with a forest.
 * \param [in]     forest   The forest.
 * \return                  The user data pointer of \a forest.
 * \see t8_forest_set_user_data
 */
void               *t8_forest_get_user_data (t8_forest_t forest);

/* TODO: define weight callback function */
void                t8_forest_set_partition (t8_forest_t forest,
                                             const t8_forest_t from,
                                             int set_for_coarsening);

void                t8_forest_set_balance (t8_forest_t forest,
                                           int do_balance);
void                t8_forest_set_ghost (t8_forest_t forest, int do_ghost);

/* TODO: use assertions and document that the forest_set (..., from) and
 *       set_load are mutually exclusive. */
void                t8_forest_set_load (t8_forest_t forest,
                                        const char *filename);

/** Compute the global number of elements in a forest as the sum
 *  of the local element counts.
 *  \param [in] forest    The forest.
 */
void                t8_forest_comm_global_num_elements (t8_forest_t forest);

/** After allocating and adding properties to a forest, commit the changes.
 * This call sets up the internal state of the forest.
 * \param [in,out] forest       Must be created with \ref t8_forest_init and
 *                              specialized with t8_forest_set_* calls first.
 */
void                t8_forest_commit (t8_forest_t forest);

t8_locidx_t         t8_forest_get_num_element (t8_forest_t forest);

/** Return the element class of a forest local tree.
 *  \param [in] forest    The forest.
 *  \param [in] ltreeid   The local id of a tree in the forest.
 * \return  The element class of the tree \a ltreeid.
 * \a forest must be committed before calling this function.
 */
t8_eclass_t         t8_forest_get_eclass (t8_forest_t forest,
                                          t8_locidx_t ltreeid);

/** Given the local id of a tree in a forest, compute the tree's local id
 * in the associated cmesh.
 *  \param [in] forest    The forest.
 *  \param [in] ltreeid   The local id of a tree in the forest.
 * \return  The local id of the tree in the cmesh associated with the forest.
 * \a forest must be committed before calling this function.
 */
t8_locidx_t         t8_forest_ltreeid_to_cmesh_ltreeid (t8_forest_t forest,
                                                        t8_locidx_t ltreeid);

/** Enable or disable profiling for a forest. If profiling is enabled, runtimes
 * and statistics are collected during forest_commit.
 * \param [in,out] forest        The forest to be updated.
 * \param [in]     set_profiling If true, profiling will be enabled, if false
 *                              disabled.
 *
 * Profiling is disabled by default.
 * The forest must not be committed before calling this function.
 * \see t8_forest_print_profile
 */
void                t8_forest_set_profiling (t8_forest_t forest,
                                             int set_profiling);

/** Print the collected statistics from a forest profile.
 * \param [in]    forest        The forest.
 *
 * \a forest must be committed before calling this function.
 * \see t8_forest_set_profiling
 */
void                t8_forest_print_profile (t8_forest_t forest);

/** Change the cmesh associated to a forest to a partitioned cmesh that
 * is partitioned according to the tree distribution in the forest.
 * \param [in,out]   forest The forest.
 * \param [in]       comm   The MPI communicator that is used to partition
 *                          and commit the cmesh.
 * \param [in]       set_profiling If true, profiling for the new cmesh
 *                          will be enabled. \see t8_cmesh_set_profiling, \see t8_cmesh_print_profile
 *  \see t8_cmesh.h
 */
void                t8_forest_partition_cmesh (t8_forest_t forest,
                                               sc_MPI_Comm comm,
                                               int set_profiling);

/** Return the number of local trees of a given forest.
 * \param [in]      forest      The forest.
 * \return          The number of local trees of that forest.
 */
t8_locidx_t         t8_forest_get_num_local_trees (t8_forest_t forest);

/** Return a pointer to a tree in a forest.
 * \param [in]      forest      The forest.
 * \param [in]      ltree_id    The local id of the tree.
 * \return                      A pointer to the tree with local id \a ltree_id.
 * \a forest must be committed before calling this function.
 */
t8_tree_t           t8_forest_get_tree (t8_forest_t forest,
                                        t8_locidx_t ltree_id);

/** Return a cmesh associated to a forest.
 * \param [in]      forest      The forest.
 * \a forest must be committed before calling this function.
 * \return          The cmesh associated to the forest.
 */
t8_cmesh_t          t8_forest_get_cmesh (t8_forest_t forest);

/** Return the number of elements of a tree.
 * \param [in]      tree       A tree in a forest.
 * \return                     The number of elements of that tree.
 */
t8_locidx_t         t8_forest_get_tree_element_count (t8_tree_t tree);

/** Compute the global index of the first local element of a forest.
 * This function is collective.
 * \param [in]     forest       A committed forest, whose first element's index is computed.
 * \return         The global index of \a forest's first local element.
 * Forest must be committed when calling this function.
 * This function is collective and must be called on each process.
 */
t8_gloidx_t         t8_forest_get_first_local_element_id (t8_forest_t forest);

void                t8_forest_save (t8_forest_t forest);
void                t8_forest_write_vtk (t8_forest_t forest,
                                         const char *filename);

void                t8_forest_iterate (t8_forest_t forest);

/** Increase the reference counter of a forest.
 * \param [in,out] forest       On input, this forest must exist with positive
 *                              reference count.  It may be in any state.
 */
void                t8_forest_ref (t8_forest_t forest);

/** Decrease the reference counter of a forest.
 * If the counter reaches zero, this forest is destroyed.
 * In this case, the forest dereferences its cmesh and scheme members.
 * \param [in,out] pforest      On input, the forest pointed to must exist
 *                              with positive reference count.  It may be in
 *                              any state.  If the reference count reaches
 *                              zero, the forest is destroyed and this pointer
 *                              set to NULL.
 *                              Otherwise, the pointer is not changed and
 *                              the forest is not modified in other ways.
 */
void                t8_forest_unref (t8_forest_t * pforest);

T8_EXTERN_C_END ();

#endif /* !T8_FOREST_H */
