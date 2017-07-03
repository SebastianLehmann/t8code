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

#include <t8_forest/t8_forest_balance.h>
#include <t8_forest/t8_forest_types.h>
#include <t8_forest/t8_forest_private.h>
#include <t8_forest/t8_forest_ghost.h>
#include <t8_forest.h>
#include <t8_element_cxx.hxx>

/* We want to export the whole implementation to be callable from "C" */
T8_EXTERN_C_BEGIN ();

/* This is the adapt function called during one round of balance.
 * We refine an element if it has any face neighbor with a level larger
 * than the element's level + 1.
 */
/* TODO: We currently do not adapt recursively since some functions such
 * as half neighbor computation require the forest to be committed. Thus,
 * we pass forest_from as a parameter. But doing so is not valid anymore
 * if we refine recursively. */
static int
t8_forest_balance_adapt (t8_forest_t forest, t8_forest_t forest_from,
                         t8_locidx_t ltree_id, t8_eclass_scheme_c * ts,
                         int num_elements, t8_element_t * elements[])
{
  int                *pdone, iface, num_faces, num_half_neighbors, ineigh;
  t8_gloidx_t         neighbor_tree;
  t8_eclass_t         neigh_class;
  t8_eclass_scheme_c *neigh_scheme;
  t8_element_t       *element = elements[0], **half_neighbors;

  pdone = (int *) forest->t8code_data;

  num_faces = ts->t8_element_num_faces (element);
  for (iface = 0; iface < num_faces; iface++) {
    /* Get the element class and scheme of the face neighbor */
    neigh_class = t8_forest_element_neighbor_eclass (forest_from,
                                                     ltree_id, element,
                                                     iface);
    neigh_scheme = t8_forest_get_eclass_scheme (forest_from, neigh_class);
    /* Allocate memory for the number of half face neighbors */
    num_half_neighbors = ts->t8_element_num_face_children (element, iface);
    half_neighbors = T8_ALLOC (t8_element_t *, num_half_neighbors);
    ts->t8_element_new (num_half_neighbors, half_neighbors);
    /* Compute the half face neighbors of element at this face */
    neighbor_tree = t8_forest_element_half_face_neighbors (forest_from,
                                                           ltree_id, element,
                                                           half_neighbors,
                                                           iface,
                                                           num_half_neighbors);
    if (neighbor_tree >= 0) {
      /* The face neighbors do exist, check for each one, whether it has
       * local or ghost leaf descendants in the forest.
       * If so, the element will be refined. */
      for (ineigh = 0; ineigh < num_half_neighbors; ineigh++) {
        if (t8_forest_element_has_leaf_desc (forest_from, neighbor_tree,
                                             half_neighbors[ineigh],
                                             neigh_scheme)) {
          /* This element should be refined */
          *pdone = 0;
          /* clean-up */
          ts->t8_element_destroy (1, half_neighbors);
          T8_FREE (half_neighbors);
          return 1;
        }
      }
    }
    /* clean-up */
    ts->t8_element_destroy (1, half_neighbors);
    T8_FREE (half_neighbors);
  }

  return 0;
}

void
t8_forest_balance (t8_forest_t forest, int repartition)
{
  t8_forest_t         forest_temp, forest_from, forest_partition;
  int                 done = 0, done_global = 0;
#ifdef T8_ENABLE_DEBUG
  int                 count = 0;
#endif

  t8_global_productionf
    ("Into t8_forest_balance with %lli global elements.\n",
     (long long) t8_forest_get_global_num_elements (forest->set_from));
  t8_log_indent_push ();

  if (forest->profile != NULL) {
    /* Profiling is enable, so we measure the runtime of balance */
    forest->profile->balance_runtime = -sc_MPI_Wtime ();
  }

  /* Use set_from as the first forest to adapt */
  forest_from = forest->set_from;
  /* This function is reference neutral regarding forest_from */
  t8_forest_ref (forest_from);

  if (forest->set_from->ghosts == NULL) {
    forest->set_from->ghost_type = T8_GHOST_FACES;
    t8_forest_ghost_create_topdown (forest->set_from);
  }
  while (!done_global) {
    done = 1;

    /* Initialize the temp forest to be adapted from forest_from */
    t8_forest_init (&forest_temp);
    t8_forest_set_adapt (forest_temp, forest_from, t8_forest_balance_adapt,
                         NULL, 0);
    t8_forest_set_ghost (forest_temp, 1, T8_GHOST_FACES);
    forest_temp->t8code_data = &done;
    /* Adapt the forest */
    t8_forest_commit (forest_temp);
    /* Compute the logical and of all process local done values, if this results
     * in 1 then all processes are finished */
    sc_MPI_Allreduce (&done, &done_global, 1, sc_MPI_INT, sc_MPI_LAND,
                      forest->mpicomm);

    if (repartition && !done_global) {
      /* If repartitioning is used, we partition the forest */
      t8_forest_init (&forest_partition);
      t8_forest_set_partition (forest_partition, forest_temp, 0);
      t8_forest_set_ghost (forest_partition, 1, T8_GHOST_FACES);
      t8_forest_commit (forest_partition);
      forest_temp = forest_partition;
      forest_partition = NULL;
    }
    /* Adapt forest_temp in the next round */
    forest_from = forest_temp;
#ifdef T8_ENABLE_DEBUG
    count++;
#endif
  }

  T8_ASSERT (t8_forest_is_balanced (forest_temp));
  /* Forest_temp is now balanced, we copy its trees and elements to forest */
  t8_forest_copy_trees (forest, forest_temp, 1);
  /* TODO: Also copy ghost elements if ghost creation is set */

  t8_log_indent_pop ();
  t8_global_productionf
    ("Done t8_forest_balance with %lli global elements.\n",
     (long long) t8_forest_get_global_num_elements (forest_temp));
#ifdef T8_ENABLE_DEBUG
  t8_debugf ("[H] Balance needed %i rounds.\n", count);
#endif
  /* clean-up */
  t8_forest_unref (&forest_temp);

  if (forest->profile != NULL) {
    /* Profiling is enabled, so we measure the runtime of balance. */
    forest->profile->balance_runtime += sc_MPI_Wtime ();
  }
}

/* Check whether the local elements of a forest are balanced. */
int
t8_forest_is_balanced (t8_forest_t forest)
{
  t8_forest_t         forest_from;
  t8_locidx_t         num_trees, num_elements;
  t8_locidx_t         itree, ielem;
  t8_element_t       *element;
  t8_eclass_scheme_c *ts;

  T8_ASSERT (t8_forest_is_committed (forest));

  /* temporarily save forest_from */
  forest_from = forest->set_from;

  forest->set_from = forest;

  num_trees = t8_forest_get_num_local_trees (forest);
  /* Iterate over all trees */
  for (itree = 0; itree < num_trees; itree++) {
    num_elements = t8_forest_get_tree_num_elements (forest, itree);
    ts =
      t8_forest_get_eclass_scheme (forest,
                                   t8_forest_get_tree_class (forest, itree));
    /* Iterate over all elements of this tree */
    for (ielem = 0; ielem < num_elements; ielem++) {
      element = t8_forest_get_element_in_tree (forest, itree, ielem);
      /* Test if this element would need to be refined in the balance step.
       * If so, the forest is not balanced locally. */
      if (t8_forest_balance_adapt (forest, forest, itree, ts, 1, &element)) {
        forest->set_from = forest_from;
        return 0;
      }
    }
  }
  forest->set_from = forest_from;
  return 1;
}

T8_EXTERN_C_END ();