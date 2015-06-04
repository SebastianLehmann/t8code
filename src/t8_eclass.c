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

#include <t8_eclass.h>

/* *INDENT-OFF* */
const int t8_eclass_to_dimension[T8_ECLASS_LAST] =
  { 0, 1, 2, 2, 3, 3, 3, 3 };

const int t8_eclass_num_faces[T8_ECLASS_LAST] =
  { 0, 2, 4, 3, 6, 4, 5, 5 };

const int    t8_eclass_num_vertices[T8_ECLASS_LAST] =
  { 1, 2, 4, 3, 8, 4, 6, 5 };

const int t8_eclass_num_children[T8_ECLASS_LAST] =
  { 0, 2, 4, 4, 8, 8, 8, 10 };

const int t8_eclass_face_types[T8_ECLASS_LAST][T8_ECLASS_MAX_FACES] =
  {{ -1, -1, -1, -1, -1, -1 },
   {  0,  0, -1, -1, -1, -1 },
   {  1,  1,  1,  1, -1, -1 },
   {  1,  1,  1, -1, -1, -1 },
   {  2,  2,  2,  2,  2,  2 },
   {  3,  3,  3,  3, -1, -1 },
   {  2,  2,  2,  3,  3, -1 },
   {  3,  3,  3,  3,  2, -1 }};

const int t8_eclass_boundary_count[T8_ECLASS_LAST][T8_ECLASS_LAST] =
  {{ 0,  0, 0, 0, 0, 0, 0, 0 },
   { 2,  0, 0, 0, 0, 0, 0, 0 },
   { 4,  4, 0, 0, 0, 0, 0, 0 },
   { 3,  3, 0, 0, 0, 0, 0, 0 },
   { 8, 12, 6, 0, 0, 0, 0, 0 },
   { 4,  6, 0, 4, 0, 0, 0, 0 },
   { 6,  9, 3, 2, 0, 0, 0, 0 },
   { 5,  8, 1, 4, 0, 0, 0, 0 }};
/* *INDENT-ON* */

int
t8_eclass_count_boundary (t8_eclass_t theclass, int min_dim, int *per_eclass)
{
  int                 t;
  int                 sum;

  sum = 0;
  for (t = 0; t < T8_ECLASS_LAST; ++t) {
    if (t8_eclass_to_dimension[t] >= min_dim) {
      sum += (per_eclass[t] = t8_eclass_boundary_count[theclass][t]);
    }
    else {
      per_eclass[t] = 0;
    }
  }

  return sum;
}

t8_gloidx_t
t8_eclass_count_leaf (t8_eclass_t theclass, int level)
{
  if (theclass != T8_ECLASS_PYRAMID) {
    /* For each eclass that is not the pyramid the number of leafs
     * is dim^level.
     */
    return 1 << t8_eclass_to_dimension[theclass] * level;
  }
  else {
    /* For the eclass pyramid the number of leafs is
     * 6^level + 4 * \sum_{i=1}^l 6^{l-i}8^{i-1}
     */
    t8_locidx_t         power_of_6 = 1;
    t8_locidx_t         six_to_level;
    t8_locidx_t         power_of_8 = 1;
    t8_locidx_t         number_of_leafs = 0;
    t8_locidx_t         li;

    /* compute 6^level */
    for (li = 0; li < level; li++) {
      power_of_6 *= 6;
    }
    six_to_level = power_of_6;
    T8_ASSERT (six_to_level > 0);
    for (li = 0; li < level; li++) {
      power_of_6 /= 6;
      number_of_leafs += power_of_8 * power_of_6;
      power_of_8 *= 8;
    }
    number_of_leafs *= 4;
    number_of_leafs += six_to_level;
    T8_ASSERT (number_of_leafs > 0);
    return number_of_leafs;
  }
}