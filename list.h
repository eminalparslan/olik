#ifndef LIST_INCLUDE
#define LIST_INCLUDE
// Basic list (dynamic array) operations in header only format

/* Example:
 *   typedef struct {
 *     int *elems;
 *     size_t size;
 *     size_t capacity;
 *   } List;
 *
 *   List list;
 *   listAppend(list, 4);
 */

// TODO: define debug to insert assertions (like checking null after allocation)
// TODO: implement list print
// TODO: other operations like pop()?

// Initial capacity for the list
#ifndef LIST_INIT_CAPACITY
#define LIST_INIT_CAPACITY 4
#endif

#ifndef LIST_MEMMOVE
#define LIST_MEMMOVE memmove
#endif

#ifndef LIST_REALLOC
#define LIST_REALLOC realloc
#endif

// Name of the struct field containing the elements of the list
#ifndef LIST_ELEMS
#define LIST_ELEMS elems
#endif


#define listAppend(list, elem) do { \
  if ((list)->size >= (list)->capacity) { \
    (list)->capacity = (list)->capacity == 0 ? LIST_INIT_CAPACITY : (list)->capacity * 2; \
    (list)->LIST_ELEMS = LIST_REALLOC((list)->LIST_ELEMS, (list)->capacity * sizeof(*(list)->LIST_ELEMS)); \
  } \
  (list)->LIST_ELEMS[(list)->size++] = (elem); \
} while (0)

#define listPrepend(list, elem) do { \
  if ((list)->size >= (list)->capacity) { \
    (list)->capacity = (list)->capacity == 0 ? LIST_INIT_CAPACITY : (list)->capacity * 2; \
    (list)->LIST_ELEMS = LIST_REALLOC((list)->LIST_ELEMS, (list)->capacity * sizeof(*(list)->LIST_ELEMS)); \
  } \
  LIST_MEMMOVE(&(list)->LIST_ELEMS[1], (list)->LIST_ELEMS, (list)->size * sizeof(*(list)->LIST_ELEMS)); \
  (list)->LIST_ELEMS[0] = (elem); \
  (list)->size++; \
} while (0)

#define listExtend(list, elems, length) do { \
  if ((list)->size + (length) > (list)->capacity) { \
    (list)->capacity = (list)->capacity == 0 ? LIST_INIT_CAPACITY + length : ((list)->size + length) * 2; \
    (list)->LIST_ELEMS = LIST_REALLOC((list)->LIST_ELEMS, (list)->capacity * sizeof(*(list)->LIST_ELEMS)); \
  } \
  LIST_MEMMOVE(&(list)->LIST_ELEMS[(list)->size], (elems), length * sizeof(*(list)->LIST_ELEMS)); \
  (list)->size += length; \
} while (0)

#define listExtendLeft(list, elems, length) do { \
  if ((list)->size + (length) > (list)->capacity) { \
    (list)->capacity = (list)->capacity == 0 ? LIST_INIT_CAPACITY + length : ((list)->size + length) * 2; \
    (list)->LIST_ELEMS = LIST_REALLOC((list)->LIST_ELEMS, (list)->capacity * sizeof(*(list)->LIST_ELEMS)); \
  } \
  LIST_MEMMOVE(&(list)->LIST_ELEMS[length], (list)->LIST_ELEMS, (list)->size * sizeof(*(list)->LIST_ELEMS)); \
  LIST_MEMMOVE((list)->LIST_ELEMS, (elems), length * sizeof(*(list)->LIST_ELEMS)); \
  (list)->size += length; \
} while (0)

#define listInsert(list, elem, pos) do { \
  if ((list)->size >= (list)->capacity) { \
    (list)->capacity = (list)->capacity == 0 ? LIST_INIT_CAPACITY : (list)->capacity * 2; \
    (list)->LIST_ELEMS = LIST_REALLOC((list)->LIST_ELEMS, (list)->capacity * sizeof(*(list)->LIST_ELEMS)); \
  } \
  LIST_MEMMOVE(&(list)->LIST_ELEMS[(pos)+1], &(list)->LIST_ELEMS[(pos)], ((list)->size-(pos)) * sizeof(*(list)->LIST_ELEMS)); \
  (list)->LIST_ELEMS[(pos)] = (elem); \
  (list)->size++; \
} while (0)

#define listDelete(list, pos) do { \
  LIST_MEMMOVE(&(list)->LIST_ELEMS[(pos)], &(list)->LIST_ELEMS[(pos)+1], ((list)->size-(pos)-1) * sizeof(*(list)->LIST_ELEMS)); \
  (list)->size--; \
} while (0)

#define listClear(list) do { \
  (list)->size = 0; \
} while (0)

#define listPop(list) (list)->elems[((list)->size--)-1];

#define listPeek(list) (list)->elems[(list)->size-1]

#endif
