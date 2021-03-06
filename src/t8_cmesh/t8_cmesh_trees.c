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

/** \file t8_cmesh_trees.c
 *
 * TODO: document this file
 */

#include "t8_cmesh_stash.h"
#include "t8_cmesh_trees.h"

/* This struct is needed as a key to search
 * for an argument in the arguments array of a tree */
struct t8_key_id_pair
{
  int                 key;
  int                 package_id;
};

t8_part_tree_t
t8_cmesh_trees_get_part (t8_cmesh_trees_t trees, int proc)
{
  T8_ASSERT (trees != NULL);
  return (t8_part_tree_t) sc_array_index_int (trees->from_proc, proc);
}

void
t8_cmesh_trees_init (t8_cmesh_trees_t * ptrees, int num_procs,
                     t8_locidx_t num_trees, t8_locidx_t num_ghosts)
{
  t8_cmesh_trees_t    trees;

  T8_ASSERT (ptrees != NULL);
  T8_ASSERT (num_procs >= 0);
  T8_ASSERT (num_trees >= 0);
  T8_ASSERT (num_ghosts >= 0);

  trees = *ptrees = T8_ALLOC (t8_cmesh_trees_struct_t, 1);
  trees->from_proc = sc_array_new_size (sizeof (t8_part_tree_struct_t),
                                        num_procs);
  trees->tree_to_proc = T8_ALLOC_ZERO (int, num_trees);
  trees->ghost_to_proc = num_ghosts > 0 ? T8_ALLOC_ZERO (int, num_ghosts)
  :                   NULL;
}

void
t8_cmesh_trees_add_tree (t8_cmesh_trees_t trees, t8_locidx_t ltree_id,
                         int proc, t8_eclass_t eclass)
{
  t8_part_tree_t      part;
  t8_ctree_t          tree;
  T8_ASSERT (trees != NULL);
  T8_ASSERT (proc >= 0);
  T8_ASSERT (ltree_id >= 0);

  part = t8_cmesh_trees_get_part (trees, proc);
  tree = &((t8_ctree_t) part->first_tree)[ltree_id - part->first_tree_id];
  SC_CHECK_ABORTF ((int) tree->eclass == 0 && tree->treeid == 0,
                   "A duplicate treeid (%li) was found.\n", (long) ltree_id);
  tree->eclass = eclass;
  tree->treeid = ltree_id;
  tree->neigh_offset = 0;
  tree->att_offset = 0;
  tree->num_attributes = 0;
  trees->tree_to_proc[ltree_id] = proc;
}

void
t8_cmesh_trees_add_ghost (t8_cmesh_trees_t trees, t8_locidx_t lghost_index,
                          t8_gloidx_t gtree_id, int proc, t8_eclass_t eclass)
{
  t8_part_tree_t      part;
  t8_cghost_t         ghost;

  T8_ASSERT (trees != NULL);
  T8_ASSERT (proc >= 0);
  T8_ASSERT (gtree_id >= 0);
  T8_ASSERT (lghost_index >= 0);

  part = t8_cmesh_trees_get_part (trees, proc);
  T8_ASSERT (lghost_index < part->num_ghosts);
  /* From first tree we have to go num_trees to get to the first ghost.
   * From the first ghost we go by ghost_index to get to the desired ghost */
  ghost = &((t8_cghost_t) (((t8_ctree_struct_t *) part->first_tree) +
                           part->num_trees))[lghost_index];
  SC_CHECK_ABORTF ((int) ghost->eclass == 0 && ghost->treeid == 0,
                   "A duplicate ghostid (%li) was found.\n",
                   (long) lghost_index);
  ghost->eclass = eclass;
  ghost->treeid = gtree_id;
  ghost->neigh_offset = 0;
  trees->ghost_to_proc[lghost_index] = proc;
}

#ifdef T8_ENABLE_DEBUG

static int
t8_cmesh_trees_get_num_procs (t8_cmesh_trees_t trees)
{
  T8_ASSERT (trees != NULL);
  T8_ASSERT (trees->from_proc != NULL);
  return trees->from_proc->elem_count;
}

#endif

/* Get a tree form a part given its local id */
static              t8_ctree_t
t8_part_tree_get_tree (t8_part_tree_t P, t8_locidx_t tree_id)
{
  T8_ASSERT (0 <= tree_id);
  return ((t8_ctree_t) P->first_tree) + tree_id - P->first_tree_id;
}

/* get a ghost from a part given its local id */
static              t8_cghost_t
t8_part_tree_get_ghost (t8_part_tree_t P, t8_locidx_t ghost_id)
{
  t8_cghost_t         first_ghost;
  t8_locidx_t         ghost_offset;

  ghost_offset = ghost_id - P->first_ghost_id;
  T8_ASSERT (ghost_offset >= 0 && ghost_offset < P->num_ghosts);
  first_ghost = (t8_cghost_t)
    (P->first_tree + (P->num_trees * sizeof (t8_ctree_struct_t)));
  return first_ghost + ghost_offset;
}

void
t8_cmesh_trees_start_part (t8_cmesh_trees_t trees, int proc,
                           t8_locidx_t lfirst_tree, t8_locidx_t num_trees,
                           t8_locidx_t lfirst_ghost, t8_locidx_t num_ghosts,
                           int alloc)
{
  t8_part_tree_t      part;
  T8_ASSERT (trees != NULL);
  T8_ASSERT (proc >= 0 && proc < t8_cmesh_trees_get_num_procs (trees));
  T8_ASSERT (num_trees >= 0);

  part = (t8_part_tree_t) sc_array_index_int (trees->from_proc, proc);
  part->num_ghosts = num_ghosts;
  part->num_trees = num_trees;
  /* it is important to zero the memory here in order to check
   * two arrays for equality using memcmp.
   * (since we store structs, we would not have control of the padding bytes
   * otherwise) */
  if (alloc) {
    part->first_tree =
      T8_ALLOC_ZERO (char,
                     num_trees * sizeof (t8_ctree_struct_t) +
                     num_ghosts * sizeof (t8_cghost_struct_t));
  }
  else {
    part->first_tree = NULL;
  }
  part->first_tree_id = lfirst_tree;
  part->first_ghost_id = lfirst_ghost;
}

/* After all classes of trees and ghosts have been set and after the
 * number of tree attributes  was set and their total size (per tree)
 * stored temporarily in the att_offset variable
 * we grow the part array by the needed amount of memory and set the
 * offsets appropiately */
/* The workflow can be: call start_part, set tree and ghost classes maually, call
 * init_attributes, call finish_part, successively call add_attributes
 * and also set all face neighbors (TODO: write function)*/
void
t8_cmesh_trees_finish_part (t8_cmesh_trees_t trees, int proc)
{
  t8_part_tree_t      part;
  t8_ctree_t          tree;
  t8_cghost_t         ghost;
  size_t              attr_bytes, face_neigh_bytes, temp_offset,
    first_face, num_attributes;
  t8_attribute_info_struct_t *attr;
  t8_locidx_t         it;
#ifndef SC_ENABLE_REALLOC
  char               *temp;
#endif

  T8_ASSERT (trees != NULL);
  part = t8_cmesh_trees_get_part (trees, proc);
  T8_ASSERT (part != NULL);

  attr_bytes = face_neigh_bytes = 0;
  /* The offset of the first ghost */
  temp_offset = part->num_trees * sizeof (t8_ctree_struct_t);
  /* The offset of the first ghost face */
  first_face = temp_offset + part->num_ghosts * sizeof (t8_cghost_struct_t);
  for (it = 0; it < part->num_ghosts; it++) {
    ghost = t8_part_tree_get_ghost (part, it + part->first_ghost_id);
    ghost->neigh_offset = first_face + face_neigh_bytes - temp_offset;
    /* Add space for storing the gloid's of the neighbors plus the tree_to_face
     * values of the neighbors */
    face_neigh_bytes += t8_eclass_num_faces[ghost->eclass] *
      (sizeof (t8_gloidx_t) + sizeof (int8_t));
    /* This is for padding, such that face_neigh_bytes %4 == 0 */
    face_neigh_bytes += T8_ADD_PADDING (face_neigh_bytes);
    T8_ASSERT (face_neigh_bytes % T8_PADDING_SIZE == 0);
    temp_offset += sizeof (t8_cghost_struct_t);
  }
  /* TODO: passing through trees twice is not optimal. Can we do it all in one round?
     Currently we need the first one to compute the total number of face bytes */
  /* First pass through trees to set the face neighbor offsets */
  temp_offset = 0;
  num_attributes = 0;
  for (it = 0; it < part->num_trees; it++) {
    tree = t8_part_tree_get_tree (part, it + part->first_tree_id);
    tree->neigh_offset = first_face + face_neigh_bytes - temp_offset;
    face_neigh_bytes += t8_eclass_num_faces[tree->eclass] *
      (sizeof (t8_locidx_t) + sizeof (int8_t));
    num_attributes += tree->num_attributes;
    face_neigh_bytes += T8_ADD_PADDING (face_neigh_bytes);
    /* This is for padding, such that face_neigh_bytes %4 == 0 */
    T8_ASSERT (face_neigh_bytes % T8_PADDING_SIZE == 0);
    temp_offset += sizeof (t8_ctree_struct_t);
  }
#if 0
  num_attributes++;
#endif
  /* Second pass through trees to set attribute offsets */
  temp_offset = 0;
  num_attributes = 0;
  for (it = 0; it < part->num_trees; it++) {
    tree = t8_part_tree_get_tree (part, it + part->first_tree_id);
    attr_bytes += tree->att_offset;     /* att_offset stored the total size of the attributes */
    /* The att_offset of the tree is the first_face plus the number of attribute
     * bytes used by previous trees minus the temp_offset */
    tree->att_offset = first_face - temp_offset + face_neigh_bytes +
      num_attributes * sizeof (t8_attribute_info_struct_t);
    num_attributes += tree->num_attributes;
    temp_offset += sizeof (t8_ctree_struct_t);
  }
#if 0
  num_attributes++;             /* Add one attribute at the end */
#endif
  attr_bytes += num_attributes * sizeof (t8_attribute_info_struct_t);
  /* Done setting all tree and ghost offsets */
  /* Allocate memory, first_face + attr_bytes gives the new total byte count */
  /* TODO: Since we use realloc and padding, memcmp will not work, solved with memset */
  first_face = part->num_trees * sizeof (t8_ctree_struct_t) + part->num_ghosts * sizeof (t8_cghost_struct_t);   /* Total number of bytes in first_tree */
#ifdef SC_ENABLE_REALLOC
  SC_REALLOC (part->first_tree, char, first_face + attr_bytes
              + face_neigh_bytes);
  memset (part->first_tree + first_face, 0, attr_bytes + face_neigh_bytes)
#else
  temp = T8_ALLOC_ZERO (char, first_face + attr_bytes + face_neigh_bytes);
  memcpy (temp, part->first_tree, first_face);
  T8_FREE (part->first_tree);
  part->first_tree = temp;
#endif
  /* Set attribute first offset, works even if there are no attributes */
  /* TODO: It does not! This is a bug that should be fixed soon */
  if (num_attributes > 0) {
    attr = (t8_attribute_info_struct_t *) (part->first_tree + first_face
                                           + face_neigh_bytes);
    attr->attribute_offset =
      num_attributes * sizeof (t8_attribute_info_struct_t);
  }
}

void
t8_cmesh_trees_set_all_boundary (t8_cmesh_t cmesh, t8_cmesh_trees_t trees)
{
  t8_locidx_t         ltree, lghost;
  t8_cghost_t         ghost;
  t8_ctree_t          tree;
  t8_locidx_t        *face_neighbor;
  t8_gloidx_t        *gface_neighbor;
  int                 iface;

  for (ltree = 0; ltree < cmesh->num_local_trees; ltree++) {
    tree = t8_cmesh_trees_get_tree_ext (trees, ltree, &face_neighbor, NULL);
    for (iface = 0; iface < t8_eclass_num_faces[tree->eclass]; iface++) {
      face_neighbor[iface] = ltree;
    }
  }
  for (lghost = 0; lghost < cmesh->num_ghosts; lghost++) {
    ghost =
      t8_cmesh_trees_get_ghost_ext (trees, lghost, &gface_neighbor, NULL);
    for (iface = 0; iface < t8_eclass_num_faces[ghost->eclass]; iface++) {
      gface_neighbor[iface] = ghost->treeid;
    }
  }
}

/* return the total size of a trees face_neighbor entries, including padding */
static              size_t
t8_cmesh_trees_neighbor_bytes (t8_ctree_t tree)
{
  size_t              total_size;
  total_size =
    t8_eclass_num_faces[tree->eclass] * (sizeof (t8_locidx_t) +
                                         sizeof (int8_t));
  total_size += T8_ADD_PADDING (total_size);
  return total_size;
}

/* return the total size of a ghosts face_neighbor entries, including padding */
static              size_t
t8_cmesh_trees_gneighbor_bytes (t8_cghost_t tree)
{
  size_t              total_size;
  total_size =
    t8_eclass_num_faces[tree->eclass] * (sizeof (t8_gloidx_t) +
                                         sizeof (int8_t));
  total_size += T8_ADD_PADDING (total_size);
  return total_size;
}

/* return the total size of attributes of a tree */
size_t
t8_cmesh_trees_attribute_size (t8_ctree_t tree)
{
  t8_attribute_info_struct_t *attr_info;
  int                 i;
  size_t              total = 0;

  for (i = 0; i < tree->num_attributes; i++) {
    attr_info = T8_TREE_ATTR_INFO (tree, i);
    total += attr_info->attribute_size;
  }
  return total;
}

static              size_t
t8_cmesh_trees_get_part_alloc (t8_cmesh_trees_t trees, t8_part_tree_t part)
{
  size_t              byte_alloc;
  t8_locidx_t         ltree, lghost;
  t8_ctree_t          tree;
  t8_cghost_t         ghost;

  byte_alloc = part->num_trees * sizeof (t8_ctree_struct_t)
    + part->num_ghosts * sizeof (t8_cghost_struct_t);
  for (ltree = 0; ltree < part->num_trees; ltree++) {
    tree = t8_cmesh_trees_get_tree (trees, ltree + part->first_tree_id);
    byte_alloc += t8_cmesh_trees_attribute_size (tree);
    byte_alloc += tree->num_attributes * sizeof (t8_attribute_info_struct_t);
    byte_alloc += t8_cmesh_trees_neighbor_bytes (tree);
  }
  for (lghost = 0; lghost < part->num_ghosts; lghost++) {
    ghost = t8_cmesh_trees_get_ghost (trees, lghost + part->first_ghost_id);
    byte_alloc += t8_cmesh_trees_gneighbor_bytes (ghost);
  }
  return byte_alloc;
}

void
t8_cmesh_trees_get_part_data (t8_cmesh_trees_t trees, int proc,
                              t8_locidx_t * first_tree,
                              t8_locidx_t * num_trees,
                              t8_locidx_t * first_ghost,
                              t8_locidx_t * num_ghosts)
{
  t8_part_tree_t      part;

  part = t8_cmesh_trees_get_part (trees, proc);
  *first_tree = part->first_tree_id;
  *num_trees = part->num_trees;
  *first_ghost = part->first_ghost_id;
  *num_ghosts = part->num_ghosts;
}

void
t8_cmesh_trees_copy_part (t8_cmesh_trees_t trees_dest, int part_dest,
                          t8_cmesh_trees_t trees_src, int part_src)
{
  t8_part_tree_t      partD, partS;
  size_t              byte_count;

  partD = t8_cmesh_trees_get_part (trees_dest, part_dest);
  partS = t8_cmesh_trees_get_part (trees_src, part_src);
  T8_ASSERT (partD->first_tree == NULL);
  byte_count = t8_cmesh_trees_get_part_alloc (trees_src, partS);
  partD->first_tree = T8_ALLOC_ZERO (char, byte_count);
  memcpy (partD->first_tree, partS->first_tree, byte_count);
}

t8_ctree_t
t8_cmesh_trees_get_tree (t8_cmesh_trees_t trees, t8_locidx_t ltree)
{
  int                 proc;
  T8_ASSERT (trees != NULL);
  T8_ASSERT (ltree >= 0);
  proc = trees->tree_to_proc[ltree];
  T8_ASSERT (proc >= 0 && proc < t8_cmesh_trees_get_num_procs (trees));

  return t8_part_tree_get_tree (t8_cmesh_trees_get_part (trees, proc), ltree);
}

t8_ctree_t
t8_cmesh_trees_get_tree_ext (t8_cmesh_trees_t trees, t8_locidx_t ltree_id,
                             t8_locidx_t ** face_neigh, int8_t ** ttf)
{
  t8_ctree_t          tree;
  tree = t8_cmesh_trees_get_tree (trees, ltree_id);
  if (face_neigh != NULL) {
    *face_neigh = (t8_locidx_t *) T8_TREE_FACE (tree);
  }
  if (ttf != NULL) {
    *ttf = (int8_t *) T8_TREE_TTF (tree);
  }
  return tree;
}

t8_cghost_t
t8_cmesh_trees_get_ghost (t8_cmesh_trees_t trees, t8_locidx_t lghost)
{
  int                 proc;
  T8_ASSERT (trees != NULL);
  T8_ASSERT (lghost >= 0);
  proc = trees->ghost_to_proc[lghost];
  T8_ASSERT (proc >= 0 && proc < t8_cmesh_trees_get_num_procs (trees));

  return t8_part_tree_get_ghost (t8_cmesh_trees_get_part (trees, proc),
                                 lghost);
}

t8_cghost_t
t8_cmesh_trees_get_ghost_ext (t8_cmesh_trees_t trees, t8_locidx_t lghost_id,
                              t8_gloidx_t ** face_neigh, int8_t ** ttf)
{
  t8_cghost_t         ghost;

  ghost = t8_cmesh_trees_get_ghost (trees, lghost_id);
  if (face_neigh != NULL) {
    *face_neigh = (t8_gloidx_t *) T8_GHOST_FACE (ghost);
  }
  if (ttf != NULL) {
    *ttf = (int8_t *) T8_GHOST_TTF (ghost);
  }
  return ghost;
}

size_t
t8_cmesh_trees_size (t8_cmesh_trees_t trees)
{
  size_t              total_bytes = 0;
  t8_part_tree_t      part;
  int                 ipart;

  T8_ASSERT (trees != NULL);
  if (trees->from_proc == NULL) {
    /* This tree struct is empty */
    return 0;
  }
  /* For each part, calculate its memory usage */
  for (ipart = 0; ipart < (int) trees->from_proc->elem_count; ipart++) {
    part = t8_cmesh_trees_get_part (trees, ipart);
    total_bytes += t8_cmesh_trees_get_part_alloc (trees, part);
  }
  return total_bytes;
}

void
t8_cmesh_trees_copy_toproc (t8_cmesh_trees_t trees_dest,
                            t8_cmesh_trees_t trees_src,
                            t8_locidx_t lnum_trees, t8_locidx_t lnum_ghosts)
{
  memcpy (trees_dest->tree_to_proc, trees_src->tree_to_proc, sizeof (int) *
          lnum_trees);
  memcpy (trees_dest->ghost_to_proc, trees_src->ghost_to_proc, sizeof (int) *
          lnum_ghosts);
}

void
t8_cmesh_trees_init_attributes (t8_cmesh_trees_t trees, t8_locidx_t ltree_id,
                                size_t num_attributes, size_t attr_bytes)
{
  int                 proc;
  t8_ctree_t          tree;

  T8_ASSERT (trees != NULL);
  T8_ASSERT (ltree_id >= 0);
  proc = trees->tree_to_proc[ltree_id];
  T8_ASSERT (proc >= 0 && proc < t8_cmesh_trees_get_num_procs (trees));
  tree = t8_part_tree_get_tree (t8_cmesh_trees_get_part (trees, proc),
                                ltree_id);

  tree->att_offset = attr_bytes;        /* This is only temporary until t8_cmesh_trees_finish_part
                                           is called */
  tree->num_attributes = num_attributes;
}

/* TODO: comment.
 * add a new attribute to a tree, the number of already added attributes is
 * temporarily stored in tree->num_attributes.
 */
/* last is true if this is the last attribute of that tree */
/* TODO: instead of all these arguments we could accept a stash_attribute_struct */
/* By adding successively we save us the step of sorting the attribute array by tree_id,
 * which is expansive */
/* TODO: This is not the final version, currently we still need the attributes
 * array to be sorted! */
void
t8_cmesh_trees_add_attribute (t8_cmesh_trees_t trees, int proc,
                              t8_stash_attribute_struct_t * attr,
                              t8_locidx_t tree_id, size_t index)
{
  t8_part_tree_t      part;
  t8_ctree_t          tree;
  char               *new_attr;
  t8_attribute_info_struct_t *attr_info;
  size_t              offset;

  T8_ASSERT (trees != NULL);
  T8_ASSERT (attr != NULL || attr->attr_size == 0);
  T8_ASSERT (attr->id >= 0);

  part = t8_cmesh_trees_get_part (trees, proc);
  tree = t8_part_tree_get_tree (part, tree_id);

  attr_info = T8_TREE_ATTR_INFO (tree, index);
  new_attr = T8_TREE_ATTR (tree, attr_info);

  memcpy (new_attr, attr->attr_data, attr->attr_size);

  /* Set new values */
  attr_info->key = attr->key;
  attr_info->package_id = attr->package_id;
  attr_info->attribute_size = attr->attr_size;
  /* Store offset */
  offset = attr_info->attribute_offset;
  /* Get next attribute and set its offset */
  if (!(index == (size_t) tree->num_attributes - 1 &&
        part->num_trees == tree_id + 1 - part->first_tree_id)) {
    attr_info = attr_info + 1;
    attr_info->attribute_offset = offset + attr->attr_size;
    if (index == (size_t) tree->num_attributes - 1) {
      attr_info->attribute_offset -= tree->num_attributes *
        sizeof (t8_attribute_info_struct_t);
    }
  }
}

#if 0
/* TODO: Are the tree following function needed? */

/* Gets two attribute_info structs and compares their package id and key */
static int
t8_cmesh_trees_compare_attributes (const void *A1, const void *A2)
{
  t8_attribute_info_struct_t *attr1, *attr2;

  attr1 = (t8_attribute_info_struct_t *) A1;
  attr2 = (t8_attribute_info_struct_t *) A2;

  if (attr1->package_id < attr2->package_id) {
    return -1;
  }
  else if (attr1->package_id > attr2->package_id) {
    return 1;
  }
  else {
    return attr1->key < attr2->key ? -1 : attr1->key != attr2->key;
  }
}

static void
t8_cmesh_part_attribute_info_sort (t8_part_tree_t P)
{
  t8_locidx_t         itree;
  t8_ctree_t          tree;
  sc_array_t          tree_attr;

  T8_ASSERT (P != NULL);

  for (itree = 0; itree < P->num_trees; itree++) {
    tree = t8_part_tree_get_tree (P, P->first_tree_id + itree);
    sc_array_init_data (&tree_attr, (char *) tree + tree->att_offset,
                        sizeof (t8_attribute_info_struct_t),
                        tree->num_attributes);
    sc_array_sort (&tree_attr, t8_cmesh_trees_compare_attributes);
  }
}

void
t8_cmesh_trees_attribute_info_sort (t8_cmesh_trees_t trees)
{
  int                 iproc;

  T8_ASSERT (trees != NULL);
  for (iproc = 0; iproc < (int) trees->from_proc->elem_count; iproc++) {
    t8_cmesh_part_attribute_info_sort (t8_cmesh_trees_get_part
                                       (trees, iproc));
  }
}
#endif

/* gets a key_id_pair as first argument and an attribute as second */
static int
t8_cmesh_trees_compare_keyattr (const void *A1, const void *A2)
{
  t8_attribute_info_struct_t *attr;
  int                 key, package_id;

  key = ((struct t8_key_id_pair *) A1)->key;
  package_id = ((struct t8_key_id_pair *) A1)->package_id;
  attr = (t8_attribute_info_struct_t *) A2;

  if (package_id < attr->package_id) {
    return -1;
  }
  else if (package_id > attr->package_id) {
    return 1;
  }
  else {
    /* both attributes have the same package_id */
    return key < attr->key ? -1 : key != attr->key;
    /* -1 if key < attr_key, 0 if key == attr_key, +1 if key > attr_key */
  }
}

/* The size of the attribute is not returned, but would be accesible */
void               *
t8_cmesh_trees_get_attribute (t8_cmesh_trees_t trees, t8_locidx_t ltree_id,
                              int package_id, int key, size_t * size)
{
  int                 proc;
  t8_ctree_t          tree;
  t8_attribute_info_struct_t *attr_info;
  ssize_t             index;
  sc_array_t          attr_array;
  struct t8_key_id_pair key_id;
  T8_ASSERT (trees != NULL);
  T8_ASSERT (ltree_id >= 0);
  proc = trees->tree_to_proc[ltree_id];
  T8_ASSERT (proc >= 0 && proc < t8_cmesh_trees_get_num_procs (trees));
  tree = t8_part_tree_get_tree (t8_cmesh_trees_get_part (trees, proc),
                                ltree_id);

  key_id.key = key;
  key_id.package_id = package_id;

  if (tree->num_attributes <= 0) {
    /* TODO: Error handling if attribute not found */
    t8_global_errorf ("Attribute with package id %i and key %i not found"
                      " on tree %li. This tree has no attributes at all.\n",
                      package_id, key, (long) ltree_id);
    return NULL;
  }

  sc_array_init_data (&attr_array, T8_TREE_FIRST_ATT (tree),
                      sizeof (t8_attribute_info_struct_t),
                      tree->num_attributes);
  index = sc_array_bsearch (&attr_array, &key_id,
                            t8_cmesh_trees_compare_keyattr);

  if (index < 0) {
    /* TODO: Error handling if attribute not found */
    t8_global_errorf ("Attribute with package id %i and key %i not found"
                      " on tree %li.\n", package_id, key, (long) ltree_id);
    return NULL;
  }
  attr_info = (t8_attribute_info_struct_t *)
    sc_array_index (&attr_array, index);
  if (size != NULL) {
    *size = attr_info->attribute_size;
  }
  return T8_TREE_ATTR (tree, attr_info);
}

size_t
t8_cmesh_trees_get_numproc (t8_cmesh_trees_t trees)
{
  return trees->from_proc->elem_count;
}

void
t8_cmesh_trees_print (t8_cmesh_t cmesh, t8_cmesh_trees_t trees)
{
#ifdef T8_ENABLE_DEBUG
  t8_locidx_t         itree, ighost;
  t8_locidx_t        *tree_neighbor;
  t8_gloidx_t         tree_neighbor_global, *ghost_neighbor;
  t8_ctree_t          tree;
  t8_cghost_t         ghost;
  int8_t             *ttf;
  int                 iface, F;
  t8_eclass_t         eclass;
  char                buf[BUFSIZ];

  t8_debugf ("Trees (local/global): %s\n",
             cmesh->num_local_trees == 0 ? "None" : "");
  F = t8_eclass_max_num_faces[cmesh->dimension];
  for (itree = 0; itree < cmesh->num_local_trees; itree++) {
    tree = t8_cmesh_trees_get_tree_ext (trees, itree, &tree_neighbor, &ttf);
    eclass = tree->eclass;
    snprintf (buf, BUFSIZ, "%li/%lli (%s):  \t|", (long) itree,
              (long long) itree + cmesh->first_tree,
              t8_eclass_to_string[eclass]);
    for (iface = 0; iface < t8_eclass_num_faces[eclass]; iface++) {
      tree_neighbor_global =
        t8_cmesh_get_global_id (cmesh, tree_neighbor[iface]);
      snprintf (buf + strlen (buf), BUFSIZ - strlen (buf), " %2li (%i) |",
                tree_neighbor_global, ttf[iface] % F);
    }
    t8_debugf ("%s\n", buf);
  }
  t8_debugf ("Ghosts (local/global): %s\n",
             cmesh->num_ghosts == 0 ? "None" : "");
  for (ighost = 0; ighost < cmesh->num_ghosts; ighost++) {
    ghost =
      t8_cmesh_trees_get_ghost_ext (trees, ighost, &ghost_neighbor, &ttf);
    eclass = ghost->eclass;
    snprintf (buf, BUFSIZ, "%li/%lli (%s):  |",
              (long) ighost + cmesh->num_local_trees,
              (long long) ghost->treeid, t8_eclass_to_string[eclass]);
    for (iface = 0; iface < t8_eclass_num_faces[eclass]; iface++) {
      snprintf (buf + strlen (buf), BUFSIZ - strlen (buf), " %li (%i) |",
                ghost_neighbor[iface], ttf[iface] % F);
    }
    t8_debugf ("%s\n", buf);
  }
#else
  return;
#endif
}

#if 0
/* compare two arrays of face_neighbors for equality */
static int
t8_cmesh_face_n_is_equal (t8_ctree_t tree_a, t8_ctree_t tree_b, int num_neigh)
{
  /* TODO: is topidx_t still used here? */
  return memcmp (T8_TREE_FACE (tree_a), T8_TREE_FACE (tree_b),
                 num_neigh * sizeof (t8_topidx_t)) ||
    memcmp (T8_TREE_TTF (tree_a), T8_TREE_TTF (tree_b),
            num_neigh * sizeof (int8_t)) ? 0 : 1;
}

/* TODO: hide this function, is used by t8_cmesh_trees_is_equal */
static int
t8_cmesh_ctree_is_equal (t8_ctree_t tree_a, t8_ctree_t tree_b)
{
  int                 is_equal;
  T8_ASSERT (tree_a != NULL && tree_b != NULL);

  is_equal = tree_a->treeid != tree_b->treeid ||
    tree_a->eclass != tree_b->eclass;
  if (is_equal != 0) {
    return 0;
  }
  if (!t8_cmesh_face_n_is_equal
      (tree_a, tree_b, t8_eclass_num_faces[tree_a->eclass])) {
    return 0;
  }

  /* TODO check attributes */

  return 1;
}
#endif

/* Given a global tree id find out whether the tree is a local ghost.
 * If it is we return its local ghost id otherwise we return -1.
 * This function just does a linear search on the ghost array and its runtime is
 * thus O(number of local ghosts).
 */
static              t8_locidx_t
t8_cmesh_trees_ghost_id (t8_cmesh_t cmesh, t8_cmesh_trees_t trees,
                         t8_gloidx_t gghost_id)
{
  t8_locidx_t         ghost_id;
  t8_cghost_t         ghost;

  if (cmesh->num_ghosts == 0) {
    return -1;
  }

  /* Since the ghost are not sorted in any way, we have no change than
   * doing a linear search. */
  for (ghost_id = 0; ghost_id < cmesh->num_ghosts; ghost_id++) {
    ghost = t8_cmesh_trees_get_ghost (trees, ghost_id);
    if (gghost_id == ghost->treeid) {
      return ghost_id;
    }
  }
  return -1;
}

/* Check whether for each tree its neighbors are set consistently, that means that
 * if tree1 lists tree2 as neighbor at face i with ttf entries (or,face j),
 * then tree2 must list tree1 as neighbor at face j with ttf entries (or, face i).
 */
int
t8_cmesh_trees_is_face_consistend (t8_cmesh_t cmesh, t8_cmesh_trees_t trees)
{
  t8_locidx_t         ltree, lghost;
  t8_ctree_t          tree1;
  t8_cghost_t         ghost1;
  t8_locidx_t        *faces1, *faces2, neigh1;
  t8_gloidx_t        *gfaces1, *gfaces2, gneigh1;
  int8_t             *ttf1, *ttf2;
  int                 ret = 1, iface, face1, F, orientation;

  F = t8_eclass_max_num_faces[cmesh->dimension];
  /* First we check the face connections of each local tree */
  for (ltree = 0; ltree < cmesh->num_local_trees && ret == 1; ltree++) {
    tree1 = t8_cmesh_trees_get_tree_ext (trees, ltree, &faces1, &ttf1);
    for (iface = 0; iface < t8_eclass_num_faces[tree1->eclass]; iface++) {
      neigh1 = faces1[iface];
      face1 = ttf1[iface] % F;
      orientation = ttf1[iface] / F;
      if (neigh1 == ltree) {
        /* This face is a boundary and therefor we do not check anything */
        continue;
      }
      if (neigh1 < cmesh->num_local_trees) {
        /* Neighbor is a local tree */
        (void) t8_cmesh_trees_get_tree_ext (trees, neigh1, &faces2, &ttf2);
        /* Check whether the face_neighbor entry of tree2 is correct */
        ret = ret && faces2[face1] == ltree;
        /* Check whether the ttf entry of neighbor is correct */
        ret = ret && ttf2[face1] % F == iface
          && ttf2[face1] / F == orientation;
      }
      else {
        /* Neighbor is a ghost */
        (void) t8_cmesh_trees_get_ghost_ext (trees,
                                             neigh1 - cmesh->num_local_trees,
                                             &gfaces2, &ttf2);
        /* Check whether the face_neighbor entry of tree2 is correct */
        ret = gfaces2[face1] == ltree + cmesh->num_local_trees;
        /* Check whether the ttf entry of neighbor is correct */
        ret = ttf2[face1] % F == iface && ttf2[face1] / F == orientation;
      }
#ifdef T8_ENABLE_DEBUG
      if (ret != 1) {
        t8_debugf ("Face connection missmatch at tree %i face %i\n", ltree,
                   iface);
      }
#endif
    }
  }
  /* Now we check the face_connections of each local ghost.
   * Here we can only check the connection to local trees and local ghosts */
  for (lghost = 0; lghost < cmesh->num_ghosts && ret == 1; lghost++) {
    ghost1 = t8_cmesh_trees_get_ghost_ext (trees, lghost, &gfaces1, &ttf1);
    for (iface = 0; iface < t8_eclass_num_faces[ghost1->eclass]; iface++) {
      gneigh1 = gfaces1[iface];
      face1 = ttf1[iface] % F;
      orientation = ttf1[iface] / F;
      if (gneigh1 == ghost1->treeid) {
        /* This face is a boundary and we do not check anything */
        continue;
      }
      if (cmesh->first_tree <= gneigh1 &&
          gneigh1 < cmesh->first_tree + cmesh->num_local_trees) {
        /* This neighbor is a local tree */
        /* Neighbor is a local tree */
        (void) t8_cmesh_trees_get_tree_ext (trees,
                                            gneigh1 - cmesh->first_tree,
                                            &faces2, &ttf2);
        /* Check whether the face_neighbor entry of tree2 is correct */
        ret = ret && faces2[face1] == lghost + cmesh->num_local_trees;
        /* Check whether the ttf entry of neighbor is correct */
        ret = ret && ttf2[face1] % F == iface
          && ttf2[face1] / F == orientation;
      }
      else if ((neigh1 = t8_cmesh_trees_ghost_id (cmesh, trees, gneigh1)) >=
               0) {
        /* This neighbor is a local ghost, its ghost id is stored in neigh1 */
        (void) t8_cmesh_trees_get_ghost_ext (trees, neigh1, &gfaces2, &ttf2);
        /* Check whether the face_neighbor entry of tree2 is correct */
        ret = ret && gfaces2[face1] == ghost1->treeid;
        /* Check whether the ttf entry of neighbor is correct */
        ret = ret && ttf2[face1] % F == iface
          && ttf2[face1] / F == orientation;
      }
#ifdef T8_ENABLE_DEBUG
      if (ret != 1) {
        t8_debugf ("Face connection missmatch at ghost %i face %i\n", lghost,
                   iface);
      }
#endif
    }
  }
  return ret;
}

int
t8_cmesh_trees_is_equal (t8_cmesh_t cmesh, t8_cmesh_trees_t trees_a,
                         t8_cmesh_trees_t trees_b)
{
  int                 is_equal;
  t8_locidx_t         num_trees, num_ghost;
  size_t              it;
  t8_part_tree_t      part_a, part_b;

  T8_ASSERT (cmesh != NULL);
  if (trees_a == trees_b) {
    /* also returns true if both are NULL */
    return 1;
  }
  if (trees_a == NULL || trees_b == NULL) {
    return 0;
  }
  num_trees = cmesh->num_trees;
  num_ghost = cmesh->num_ghosts;
  is_equal = memcmp (trees_a->tree_to_proc, trees_b->tree_to_proc,
                     num_trees * sizeof (int))
    || memcmp (trees_a->ghost_to_proc, trees_b->ghost_to_proc,
               num_ghost * sizeof (int));
  if (is_equal != 0) {
    return 0;
  }
  /* compare entries of from_proc array */
  /* we can't use sc_array_is_equal because we store structs in the array
   * and don't have any control over the padding in these structs.
   */
  for (it = 0; it < trees_a->from_proc->elem_count; it++) {
    if (it >= trees_b->from_proc->elem_count) {
      return 0;
    }
    part_a = (t8_part_tree_t) sc_array_index (trees_a->from_proc, it);
    part_b = (t8_part_tree_t) sc_array_index (trees_b->from_proc, it);
    is_equal = part_a->first_tree_id != part_b->first_tree_id
      || part_a->num_ghosts != part_b->num_ghosts
      || part_a->num_trees != part_b->num_trees
      || part_a->first_ghost_id != part_b->first_ghost_id;
    if (is_equal != 0) {
      return 0;
    }
    if (memcmp (part_a->first_tree, part_b->first_tree,
                part_a->num_trees * sizeof (t8_ctree_struct_t)
                + part_a->num_ghosts * sizeof (t8_cghost_struct_t))) {
      return 0;
    }
    /* TODO: compare attributes */
  }
  return 1;

  /*TODO: implement */
  SC_ABORTF ("Comparison of cmesh_trees not implemented %s\n", "yet");
}

void
t8_cmesh_trees_destroy (t8_cmesh_trees_t * ptrees)
{
  size_t              proc;
  t8_cmesh_trees_t    trees = *ptrees;
  t8_part_tree_t      part;

  for (proc = 0; proc < trees->from_proc->elem_count; proc++) {
    part = t8_cmesh_trees_get_part (trees, proc);
    T8_FREE (part->first_tree);
  }
  T8_FREE (trees->ghost_to_proc);
  T8_FREE (trees->tree_to_proc);
  sc_array_destroy (trees->from_proc);
  T8_FREE (trees);
  ptrees = NULL;
}
