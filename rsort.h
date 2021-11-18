#ifndef _rsort_h_INCLUDED
#define _rsort_h_INCLUDED

/*------------------------------------------------------------------------*/

// Generic header only radix sort implementation ported originally from
// CaDiCaL in C++ to C in Kissat and now further improved.  It provides
// stable sorting for 'ranked' data. The ranking function 'RANK' is given as
// last argument and is supposed to map elements of type 'VTYPE' residing in
// the array 'V' of size 'N' to a number of the ranking type 'RTYPE'
// (could be 'unsigned' or 'uint64_t').

// The improvement compared to the original CaDiCaL version is to compute
// upper and lower bounds for all radix rounds once which allows to skip
// rounds with identical upper and lower bound for the current radix
// completely.  This is marked as 'NEW OPTIMIZATION' in the code below.

// Another new optimization is to skip a round partially if elements turn
// out to be sorted for the current radix.  Similar in spirit one could
// check in the first round whether all elements are completely sorted or
// remove a completely sorted initial prefix or suffix.  Allocation (1),
// deallocation (2) and copying (3) of the temporary space could be mostly
// avoided by assuming that we only sort stacks and keep a permanent
// temporary stack for each of them allocated.  Instead of copying we could
// just swap the 'begin' pointers of the array instead.

/*------------------------------------------------------------------------*/

#ifdef NDEBUG
#define CHECK_RANKED(...) do { } while (0)
#else
#define CHECK_RANKED(N,V,RANK) \
do { \
  assert (0 < (N)); \
  for (size_t I_CHECK_RANKED = 0; I_CHECK_RANKED < N-1; I_CHECK_RANKED++) \
    assert (RANK (V[I_CHECK_RANKED]) <= RANK (V[I_CHECK_RANKED + 1])); \
} while (0)
#endif

/*------------------------------------------------------------------------*/

#define RSORT(VTYPE,RTYPE,S,RANK) \
do { \
  const size_t N_RANK = SIZE_STACK (S); \
  if (N_RANK <= 1) \
    break; \
  \
  VTYPE * V_RANK = (S).begin; \
  \
  const size_t LENGTH_RANK = 8; \
  const size_t WIDTH_RANK = (1 << LENGTH_RANK); \
  const RTYPE MASK_RANK = WIDTH_RANK - 1; \
  \
  /* Keep 'WIDTH_RANK' small enough to allocate counters on the stack. */ \
  \
  size_t COUNT_RANK[WIDTH_RANK]; \
  \
  VTYPE * TMP_RANK = 0; \
  const size_t BYTES_TMP_RANK = N_RANK * sizeof (VTYPE); \
  \
  VTYPE * A_RANK = V_RANK; \
  VTYPE * B_RANK = 0; \
  VTYPE * C_RANK = A_RANK; \
  \
  RTYPE MLOWER_RANK = 0; \
  RTYPE MUPPER_RANK = MASK_RANK; \
  \
  bool BOUNDED_RANK = false; \
  RTYPE UPPER_RANK = 0; \
  RTYPE LOWER_RANK = ~UPPER_RANK; \
  RTYPE SHIFT_RANK = MASK_RANK; \
  \
  for (size_t I_RANK = 0; \
       I_RANK < 8 * sizeof (RTYPE); \
       I_RANK += LENGTH_RANK, \
       SHIFT_RANK <<= LENGTH_RANK) \
    { \
      /* Here is the NEW OPTIMIZATION we refer to above.  The first time we
       * go through all ranks and increment counters, we also compute a
       * global lower and upper bound in parallel for this and all the later
       * radix positions.  If in a later round we reach a radix position for
       * which upper and lower bound match (masked to the current radix
       * position) then we can completely skip this round. Previously we
       * only aborted the round after incrementing the counters and figuring
       * out that lower and upper bound match.  This however still acquires
       * accessing all the ranks and incrementing the counters which now can
       * be avoided.  This is actually a quite common case, when for
       * instance you sort 32-bit numbers which have similar upper bytes
       * (like all zero or just within a small range).
       */ \
      if (BOUNDED_RANK && \
	  (LOWER_RANK & SHIFT_RANK) == (UPPER_RANK & SHIFT_RANK)) \
	continue; \
      \
      /* Clear the counters from the previous round lazily (and thus avoid
       * the wasted effort to clear them if they are not needed anymore).
       */ \
      memset (COUNT_RANK + MLOWER_RANK, 0, \
              (MUPPER_RANK - MLOWER_RANK + 1) * sizeof *COUNT_RANK); \
      \
      VTYPE * END_RANK = C_RANK + N_RANK; \
      \
      bool SORTED_RANK = true; \
      RTYPE LAST_RANK = 0; \
      \
      for (VTYPE * P_RANK = C_RANK; P_RANK != END_RANK; P_RANK++) \
	{ \
	  RTYPE R_RANK = RANK (*P_RANK); \
	  if (!BOUNDED_RANK) \
	    { \
	      LOWER_RANK &= R_RANK; /* Radix wise minimum would be better */ \
	      UPPER_RANK |= R_RANK; /* Radix wise maximum would be better */ \
	    } \
	  RTYPE S_RANK = R_RANK >> I_RANK; \
	  RTYPE M_RANK = S_RANK & MASK_RANK; \
	  if (SORTED_RANK && LAST_RANK > M_RANK) \
	    SORTED_RANK = false; \
	  else \
	    LAST_RANK = M_RANK; \
	  COUNT_RANK[M_RANK]++; \
	} \
      \
      MLOWER_RANK = (LOWER_RANK >> I_RANK) & MASK_RANK; \
      MUPPER_RANK = (UPPER_RANK >> I_RANK) & MASK_RANK; \
      \
      /* Only for the first round this check is still necessary.
       */ \
      if (!BOUNDED_RANK) \
	{ \
	  BOUNDED_RANK = true;  \
	  if ((LOWER_RANK & SHIFT_RANK) == (UPPER_RANK & SHIFT_RANK)) \
	    continue; \
	} \
      \
      /* Skip rest of round if the ranks for this radix are already sorted.
       */ \
      if (SORTED_RANK) \
	continue; \
      \
      /* Another benefit of having bounds is that we only need to traverse
       * the counters from lower to upper bounds.  Here we compute the
       * starting position of all (counted) elements with the same rank for
       * the current radix position.
       */ \
      size_t POS_RANK = 0; \
      for (size_t J_RANK = MLOWER_RANK; J_RANK <= MUPPER_RANK; J_RANK++) \
	{ \
	  const size_t DELTA_RANK = COUNT_RANK[J_RANK]; \
	  COUNT_RANK[J_RANK] = POS_RANK; \
	  POS_RANK += DELTA_RANK; \
	} \
      \
      /* Only allocate the temporary copy space on-demand.
      */ \
      if (!TMP_RANK) \
	{ \
	  assert (C_RANK == A_RANK); \
	  TMP_RANK = malloc (BYTES_TMP_RANK); /*(1)*/ \
	  if (!TMP_RANK) \
	    out_of_memory (BYTES_TMP_RANK); \
	  B_RANK = TMP_RANK; \
	} \
      \
      assert (B_RANK == TMP_RANK); \
      \
      VTYPE * D_RANK = (C_RANK == A_RANK) ? B_RANK : A_RANK; \
      \
      for (VTYPE * P_RANK = C_RANK; P_RANK != END_RANK; P_RANK++) \
	{ \
	  RTYPE R_RANK = RANK (*P_RANK); \
	  RTYPE S_RANK = R_RANK >> I_RANK; \
	  RTYPE M_RANK = S_RANK & MASK_RANK; \
	  const size_t POS_RANK = COUNT_RANK[M_RANK]++; \
	  D_RANK[POS_RANK] = *P_RANK; \
	} \
      \
      C_RANK = D_RANK; \
    } \
  \
  if (C_RANK == B_RANK) \
    memcpy (A_RANK, B_RANK, N_RANK * sizeof *A_RANK); /*(3)*/ \
  \
  if (TMP_RANK) \
    free (TMP_RANK); /*(2)*/ \
  \
  CHECK_RANKED (N_RANK, V_RANK, RANK); \
} while (0)

/*------------------------------------------------------------------------*/


#endif
