/*   
 *   File: alistarh.c
 *   Author: Vincent Gramoli <vincent.gramoli@sydney.edu.au>, 
 *  	     Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *  	     Egeyar Bagcioglu <egeyar.bagcioglu@epfl.ch>
 *   Description:  
 *   alistarh.c is part of ASCYLIB
 *
 * Copyright (c) 2014 Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>,
 * 	     	      Tudor David <tudor.david@epfl.ch>
 *	      	      Distributed Programming Lab (LPD), EPFL
 *
 * ASCYLIB is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "alistarh.h"

RETRY_STATS_VARS;

#include "latency.h"
#if LATENCY_PARSING == 1
__thread size_t lat_parsing_get = 0;
__thread size_t lat_parsing_put = 0;
__thread size_t lat_parsing_rem = 0;
__thread size_t lat_parsing_deleteMin = 0;
#endif	/* LATENCY_PARSING == 1 */

extern ALIGNED(CACHE_LINE_SIZE) unsigned int levelmax;

#define FRASER_MAX_MAX_LEVEL 64 /* covers up to 2^64 elements */	//How is it different than the one above?

#define ALISTARH_NUMBER_OF_THREADS 			4	//p
#define ALISTARH_STARTING_HEIGHT_CONSTANT	(12-floor_log_2(ALISTARH_NUMBER_OF_THREADS))	//K
#define ALISTARH_JUMP_LENGTH_CONSTANT		3	//M
#define ALISTARH_CLEANER_PERCENTAGE			3

//How can make these evaluated only once??
inline int 
alistarh_levelsToDescend() //D
{
  int loglog = floor_log_2(floor_log_2(ALISTARH_NUMBER_OF_THREADS));
  return loglog>1 ? loglog : 1;
}

inline int
alistarh_startingHeight() //H
{
  int startingHeight = floor_log_2(ALISTARH_NUMBER_OF_THREADS)+ALISTARH_STARTING_HEIGHT_CONSTANT;
  return startingHeight-(startingHeight%alistarh_levelsToDescend());
}

inline long int
alistarh_maxJumpLength() //J
{
  return ALISTARH_JUMP_LENGTH_CONSTANT*floor_log_2(floor_log_2(floor_log_2(ALISTARH_NUMBER_OF_THREADS)));
}

int				
fraser_search(sl_intset_t *set, skey_t key, sl_node_t **left_list, sl_node_t **right_list)
{
  int i;
  sl_node_t *left, *left_next, *right = NULL, *right_next;

 retry:
  PARSE_TRY();

  left = set->head;
  for (i = levelmax - 1; i >= 0; i--)
    {
      left_next = left->next[i];
      if ((is_marked((uintptr_t)left_next)))
	{
	  goto retry;
	}
      /* Find unmarked node pair at this level */
      for (right = left_next; ; right = right_next)
	{
	  /* Skip a sequence of marked nodes */
	  right_next = right->next[i];
	  while ((is_marked((uintptr_t)right_next)))
	    {
	      right = (sl_node_t*)unset_mark((uintptr_t)right_next);
	      right_next = right->next[i];
	    }

	  if (right->key >= key)
	    {
	      break;
	    }
	  left = right; 
	  left_next = right_next;
	}
      /* Ensure left and right nodes are adjacent */
      if ((left_next != right) && (!ATOMIC_CAS_MB(&left->next[i], left_next, right)))
	{
	  goto retry;
	}

      if (left_list != NULL)
      	{
	  left_list[i] = left;
      	}
      if (right_list != NULL)
      	{
	  right_list[i] = right;
	}
    }
  return (right->key == key);
}

int				
fraser_search_no_cleanup(sl_intset_t *set, skey_t key, sl_node_t **left_list, sl_node_t **right_list)
{
  PARSE_TRY();
  int i;
  sl_node_t *left, *left_next, *right = NULL;

  left = set->head;
  for (i = levelmax - 1; i >= 0; i--)
    {
      left_next = GET_UNMARKED(left->next[i]);
      right = left_next;
      while (1)
	{
	  if (likely(!IS_MARKED(right->next[i])))
	    {
	      if (right->key >= key)
		{
		  break;
		}
	      left = right;
	    }
	  right = GET_UNMARKED(right->next[i]);
	}

      /* if (left_list != NULL) */
      /* 	{ */
	  left_list[i] = left;
      /* 	} */
      /* if (right_list != NULL)	 */
      /* 	{ */
	  right_list[i] = right;
	/* } */
    }
  return (right->key == key);
}

int				
fraser_search_no_cleanup_succs(sl_intset_t *set, skey_t key, sl_node_t **right_list)
{
  PARSE_TRY();

  int i;
  sl_node_t *left, *left_next, *right = NULL;

  left = set->head;
  for (i = levelmax - 1; i >= 0; i--)
    {
      left_next = GET_UNMARKED(left->next[i]);
      right = left_next;
      while (1)
	{
	  if (likely(!IS_MARKED(right->next[i])))
	    {
	      if (right->key >= key)
		{
		  break;
		}
	      left = right;
	    }
	  right = GET_UNMARKED(right->next[i]);
	}


      right_list[i] = right;
    }
  return (right->key == key);
}

static sl_node_t* 
fraser_left_search(sl_intset_t *set, skey_t key)
{
  PARSE_TRY();
  sl_node_t* left = NULL;
  sl_node_t* left_prev;
  
  left_prev = set->head;
  int lvl;  
  for (lvl = levelmax - 1; lvl >= 0; lvl--)
    {
      left = GET_UNMARKED(left_prev->next[lvl]);
      while(left->key < key || IS_MARKED(left->next[lvl]))
      	{
      	  if (!IS_MARKED(left->next[lvl]))
      	    {
      	      left_prev = left;
      	    }
      	  left = GET_UNMARKED(left->next[lvl]);
      	}

      if ((left->key == key))
	{
	  break;
	}
    }
  return left;
}


sval_t
fraser_find(sl_intset_t *set, skey_t key)
{
  printf("in find\n");
  sval_t result = 0;
  PARSE_START_TS(0);
  sl_node_t* left = fraser_left_search(set, key);
  PARSE_END_TS(0, lat_parsing_get++);

  if (left->key == key)
    {
      result = left->val;
    }
  printf("out of find\n");
  return result;
}

inline int
mark_node_ptrs(sl_node_t *n)
{
  int i, cas = 0;
  sl_node_t* n_next;
	
  for (i = n->toplevel - 1; i >= 0; i--)
    {
      do
      	{
      	  n_next = n->next[i];
      	  if (is_marked((uintptr_t)n_next))
      	    {
	      cas = 0;
      	      break;
      	    }
	  cas = ATOMIC_CAS_MB(&n->next[i], GET_UNMARKED(n_next), set_mark((uintptr_t)n_next));
      	} 
      while (!cas);
    }

  return (cas);	/* if I was the one that marked lvl 0 */
}

sval_t
fraser_remove(sl_intset_t *set, skey_t key)
{
  UPDATE_TRY();

  sl_node_t* succs[FRASER_MAX_MAX_LEVEL];
  sval_t result = 0;

  PARSE_START_TS(2);
  int found = fraser_search_no_cleanup_succs(set, key, succs);
  PARSE_END_TS(2, lat_parsing_rem++);

  if (!found)
    {
      return false;
    }

  sl_node_t* node_del = succs[0];
  int my_delete = mark_node_ptrs(node_del);

  if (my_delete)
    {
      result = node_del->val;
      fraser_search(set, key, NULL, NULL);
#if GC == 1
      ssmem_free(alloc, (void*) succs[0]);
#endif
    }
  return result;
}

inline sval_t
alistarh_cleaner(sl_intset_t *set)
{
  printf("in cleaner\n");
  sval_t result = 0;
  sl_node_t *preds[FRASER_MAX_MAX_LEVEL], *succs[FRASER_MAX_MAX_LEVEL];
  sl_node_t* node = GET_UNMARKED(set->head->next[0]);
  while(node->next[0]!=NULL)
  {
    if (!IS_MARKED(node->next[node->toplevel-1]))
    {
      int my_delete = mark_node_ptrs(node);
      if (my_delete)
      {
        result = node->val;
        fraser_search(set, node->key, preds, succs);
        break;
      }
    }
    node = GET_UNMARKED(node->next[0]);
  }
  
  #if GC == 1
    ssmem_free(alloc, (void*) succs[0]);
  #endif
  printf("out of cleaner\n");
  return result;
}

sval_t
alistarh_deleteMin(sl_intset_t *set)
{
  printf("in deleteMin\n");
 retry:
  UPDATE_TRY();
  
  PARSE_START_TS(3);		//Should I keep this??
  int i, level = alistarh_startingHeight();
  sval_t result = 0;
  printf("1\n");
  sl_node_t *next, *node = set->head;
  for(; level>=0; level--)//descend only one level
    {
	  printf("2\n");
	  i = (int)rand_range(alistarh_maxJumpLength+1)-1;
      for (; i>0; i--)
      {
		printf("3\n");
        next = GET_UNMARKED(node->next[level]);
        if (next==NULL || next->next[0]==NULL)
          break;
        printf("after if\n");
        node = next;
        printf("3,5\n");
      }
    }
  PARSE_END_TS(3, lat_parsing_deleteMin++);
  printf("4\n");
  int my_delete = mark_node_ptrs(node);
  
  if (my_delete)
    {
	  printf("5\n");
      result = node->val;
    }
    
  else
    {
	  printf("6\n");
	  if (unlikely(rand_range(100) < ALISTARH_CLEANER_PERCENTAGE))
		result = alistarh_cleaner(set);
	  else
	    goto retry;
	}
  printf("out deleteMin\n");
  return result;
}

int
fraser_insert(sl_intset_t *set, skey_t key, sval_t val) 
{
  printf("in insert\n");
  sl_node_t *new, *pred, *succ;
  sl_node_t *succs[FRASER_MAX_MAX_LEVEL], *preds[FRASER_MAX_MAX_LEVEL];
  int i, found;

  PARSE_START_TS(1);
 retry: 	
  UPDATE_TRY();

  found = fraser_search_no_cleanup(set, key, preds, succs);
  PARSE_END_TS(1, lat_parsing_put);

  if(found)
    {
      PARSE_END_INC(lat_parsing_put);
      printf("out of insert\n");
      return false;
    }


  new = sl_new_simple_node(key, val, get_rand_level(), 0);

  for (i = 0; i < new->toplevel; i++)
    {
      new->next[i] = succs[i];
    }

#if defined(__tile__)
  MEM_BARRIER;
#endif

  /* Node is visible once inserted at lowest level */
  if (!ATOMIC_CAS_MB(&preds[0]->next[0], GET_UNMARKED(succs[0]), new))
    {
      sl_delete_node(new);
      goto retry;
    }

  for (i = 1; i < new->toplevel; i++) 
    {
      while (1) 
	{
	  pred = preds[i];
	  succ = succs[i];
	  if (IS_MARKED(new->next[i]))
	    {
	      PARSE_END_INC(lat_parsing_put);
	      printf("out of insert\n");
	      return true;
	    }
	  if (ATOMIC_CAS_MB(&pred->next[i], succ, new))
	    break;

	  fraser_search(set, key, preds, succs);
	}
    }
  PARSE_END_INC(lat_parsing_put);
  printf("out of insert\n");
  return true;
}

