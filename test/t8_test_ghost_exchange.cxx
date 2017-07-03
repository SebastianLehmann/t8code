/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element types in parallel.

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

#include <t8_eclass.h>
#include <t8_default_cxx.hxx>
#include <t8_forest.h>
#include <t8_forest/t8_forest_ghost.h>
#include <t8_forest/t8_forest_private.h>
#include <t8_cmesh.h>

/* This test program tests the forest ghost exchange routine.
 * Given a forest for which the ghost layer was created and an array
 * storing data for the local elements and the ghost elements, ghost_exchange
 * communicates the data of the local elements to the ghost entries of the
 * processes for which these elements are ghost.
 * We test the ghost exchange routine for several forests on different
 * coarse meshes.
 * One test is an integer entry '42' for each element,
 * in a second test, we store the element's linear id in the data array.
 */

static int
t8_test_exchange_adapt (t8_forest_t forest, t8_forest_t forest_from,
                        t8_locidx_t which_tree, t8_eclass_scheme_c * ts,
                        int num_elements, t8_element_t * elements[])
{
  uint64_t            eid;
  int                 level, maxlevel;

  /* refine every second element up to the maximum level */
  level = ts->t8_element_level (elements[0]);
  eid = ts->t8_element_get_linear_id (elements[0], level);
  maxlevel = *(int *) t8_forest_get_user_data (forest);

  if (eid % 2 && level < maxlevel) {
    return 1;
  }
  return 0;
}

/* Depending on an integer i create a different cmesh.
 * i = 0: cmesh_new_class
 * i = 1: cmesh_new_hypercube
 * i = 2: cmesh_new_bigmesh (100 trees)
 * else:  cmesh_new_class
 */
static              t8_cmesh_t
t8_test_create_cmesh (int i, t8_eclass_t eclass, sc_MPI_Comm comm)
{
  switch (i) {
  case 0:
    return t8_cmesh_new_from_class (eclass, comm);
  case 1:
    return t8_cmesh_new_hypercube (eclass, comm, 0, 0);
  case 2:
    return t8_cmesh_new_bigmesh (eclass, 100, comm);
  default:
    return t8_cmesh_new_from_class (eclass, comm);
  }
}

/* Construct a data array of uin64_t for all elements and all ghosts,
 * fill the element's entries with their linear id, perform the ghost exchange and
 * check whether the ghost's entries are their linear id.
 */
static void
t8_test_ghost_exchange_data_id (t8_forest_t forest)
{
  t8_eclass_scheme_c *ts;

  t8_locidx_t         num_elements, ielem, num_ghosts, itree;
  uint64_t            ghost_id, elem_id, ghost_entry;
  t8_element_t       *elem;
  size_t              array_pos = 0;
  sc_array_t          element_data;

  num_elements = t8_forest_get_num_element (forest);
  num_ghosts = t8_forest_get_num_ghosts (forest);
  /* Allocate a uin64_t as data for each element and each ghost */
  sc_array_init_size (&element_data, sizeof (uint64_t),
                      num_elements + num_ghosts);

  /* Fill the local element entries with their linear id */
  for (itree = 0; itree < t8_forest_get_num_local_trees (forest); itree++) {
    /* Get the eclass scheme for this tree */
    ts = t8_forest_get_eclass_scheme (forest,
                                      t8_forest_get_tree_class (forest,
                                                                itree));
    for (ielem = 0; ielem < t8_forest_get_tree_num_elements (forest, itree);
         ielem++) {
      /* Get a pointer to this element */
      elem = t8_forest_get_element_in_tree (forest, itree, ielem);
      /* Compute the linear id of this element */
      elem_id = ts->t8_element_get_linear_id (elem,
                                              ts->t8_element_level (elem));
      /* Store this id at the element's index in the array */
      *(uint64_t *) sc_array_index (&element_data, array_pos) = elem_id;
      array_pos++;
    }
  }

  /* Perform the data exchange */
  t8_forest_ghost_exchange_data (forest, &element_data);

  /* We now iterate over all ghost elements and check whether the correct
   * id was received */
  for (itree = 0; itree < t8_forest_get_num_ghost_trees (forest); itree++) {
    /* Get the eclass scheme of this ghost tree */
    ts =
      t8_forest_get_eclass_scheme (forest,
                                   t8_forest_ghost_get_tree_class (forest,
                                                                   itree));
    for (ielem = 0; ielem < t8_forest_ghost_tree_num_elements (forest, itree);
         ielem++) {
      /* Get a pointer to this ghost */
      elem = t8_forest_ghost_get_element (forest, itree, ielem);
      /* Compute its ghost_id */
      ghost_id =
        ts->t8_element_get_linear_id (elem, ts->t8_element_level (elem));
      /* Compare this id with the entry in the element_data array */
      ghost_entry = *(uint64_t *) sc_array_index (&element_data, array_pos);
      SC_CHECK_ABORT (ghost_id == ghost_entry,
                      "Error when exchanging ghost data. Received wrong element id.\n");
      /* Since array pos ended with the last element in the loop above, we can
       * continue counting for the ghost elements */
      array_pos++;
    }
  }
  /* clean-up */
  sc_array_reset (&element_data);
}

/* Construct a data array of ints for all elements and all ghosts,
 * fill the element's entries with '42', perform the ghost exchange and
 * check whether the ghost's entries are '42'.
 */
static void
t8_test_ghost_exchange_data_int (t8_forest_t forest)
{
  sc_array_t          element_data;
  t8_locidx_t         num_elements, ielem, num_ghosts;
  int                 ghost_int;

  num_elements = t8_forest_get_num_element (forest);
  num_ghosts = t8_forest_get_num_ghosts (forest);
  /* Allocate an integer as data for each element and each ghost */
  sc_array_init_size (&element_data, sizeof (int), num_elements + num_ghosts);

  /* Fill the local element entries with the integer 42 */
  for (ielem = 0; ielem < num_elements; ielem++) {
    *(int *) t8_sc_array_index_locidx (&element_data, ielem) = 42;
  }
  /* Perform the ghost data exchange */
  t8_forest_ghost_exchange_data (forest, &element_data);

  /* Check for the ghosts that we received the correct data */
  for (ielem = 0; ielem < num_ghosts; ielem++) {
    /* Get the integer for this ghost */
    ghost_int =
      *(int *) t8_sc_array_index_locidx (&element_data, num_elements + ielem);
    SC_CHECK_ABORT (ghost_int == 42,
                    "Error when exchanging ghost data. Received wrong data.\n");
  }
  /* clean-up */
  sc_array_reset (&element_data);
}

static void
t8_test_ghost_exchange ()
{
  int                 ctype, level, min_level, maxlevel;
  int                 eclass;
  t8_cmesh_t          cmesh;
  t8_forest_t         forest, forest_adapt;
  t8_scheme_cxx_t    *scheme;

  scheme = t8_scheme_new_default_cxx ();
  for (eclass = T8_ECLASS_QUAD; eclass < T8_ECLASS_PRISM; eclass++) {
    /* TODO: Activate the other eclass as soon as they support ghosts */
    for (ctype = 0; ctype < 3; ctype++) {
      /* Construct a cmesh */
      cmesh =
        t8_test_create_cmesh (ctype, (t8_eclass_t) eclass, sc_MPI_COMM_WORLD);
      min_level = t8_forest_min_nonempty_level (cmesh, scheme);
      t8_global_productionf
        ("Testing ghost exchange with eclass %s, start level %i\n",
         t8_eclass_to_string[eclass], min_level);
      for (level = min_level; level < min_level + 3; level++) {
        /* ref the scheme since we reuse it */
        t8_scheme_cxx_ref (scheme);
        /* ref the cmesh since we reuse it */
        t8_cmesh_ref (cmesh);
        /* Create a uniformly refined forest */
        forest = t8_forest_new_uniform (cmesh, scheme, level, 1,
                                        sc_MPI_COMM_WORLD);
        /* exchange ghost data */
        t8_test_ghost_exchange_data_int (forest);
        t8_test_ghost_exchange_data_id (forest);
        /* Adapt the forest and exchange data again */
        maxlevel = level + 2;
        forest_adapt = t8_forest_new_adapt (forest, t8_test_exchange_adapt,
                                            NULL, 1, 1, &maxlevel);
        t8_test_ghost_exchange_data_int (forest_adapt);
        t8_test_ghost_exchange_data_id (forest_adapt);
        t8_forest_unref (&forest_adapt);
      }
      t8_cmesh_destroy (&cmesh);
    }
  }
  t8_scheme_cxx_unref (&scheme);
}

int
main (int argc, char **argv)
{
  int                 mpiret;
  sc_MPI_Comm         mpic;

  mpiret = sc_MPI_Init (&argc, &argv);
  SC_CHECK_MPI (mpiret);

  mpic = sc_MPI_COMM_WORLD;
  sc_init (mpic, 1, 1, NULL, SC_LP_PRODUCTION);
  p4est_init (NULL, SC_LP_ESSENTIAL);
  t8_init (SC_LP_DEFAULT);

  t8_test_ghost_exchange ();

  sc_finalize ();

  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);

  return 0;
}