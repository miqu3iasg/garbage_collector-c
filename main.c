#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdarg.h> 

#define COLOR_RESET "\033[0m"
#define COLOR_INFO "\033[1;34m"
#define COLOR_DEBUG "\033[0;36m"
#define COLOR_WARN "\033[1;33m"
#define COLOR_ERROR "\033[1;31m"
#define COLOR_SUCCESS "\033[1;32m"
#define COLOR_TITLE "\033[1;35m"
#define COLOR_HEADER "\033[1;37m"
#define COLOR_OBJECT "\033[0;37m"

typedef enum
{
  GC_LOG_INFO,
  GC_LOG_DEBUG,
  GC_LOG_WARN,
  GC_LOG_ERROR
} GCLogLevel;

void gc_log(GCLogLevel level, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  const char *prefix_color;
  const char *prefix_text;

  switch (level){
  case GC_LOG_INFO:
    prefix_color = COLOR_INFO;
    prefix_text = "INFO";
    break;
  case GC_LOG_DEBUG:
    prefix_color = COLOR_DEBUG;
    prefix_text = "DEBUG";
    break;
  case GC_LOG_WARN:
    prefix_color = COLOR_WARN;
    prefix_text = "WARN";
    break;
  case GC_LOG_ERROR:
    prefix_color = COLOR_ERROR;
    prefix_text = "ERROR";
    break;
  default:
    prefix_color = COLOR_RESET;
    prefix_text = "LOG";
  }

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timebuf[9];
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

  printf("%s[%s] %-6s " COLOR_RESET, prefix_color, timebuf, prefix_text);
  vprintf(fmt, args);
  printf("\n");

  va_end(args);
}

typedef struct Object
{
  int id;
  bool marked;
  size_t number_of_references;
  struct Object *references[];
} Object;

typedef struct
{
  Object **items;
  size_t count;
  size_t capacity;
} RootSet;

typedef struct HeapNode
{
  Object *object;
  struct HeapNode *next;
} HeapNode;

typedef struct Heap
{
  HeapNode *head;
  RootSet roots;
} Heap;

Heap gc_heap;

void gc_init()
{
  gc_heap.head = NULL;
  gc_heap.roots.items = NULL;
  gc_heap.roots.count = 0;
  gc_heap.roots.capacity = 0;

  gc_log(GC_LOG_INFO, "Garbage collector initialized");
}

void gc_destroy()
{
  HeapNode *current = gc_heap.head;

  while (current != NULL)
  {
    HeapNode *next = current->next;
    free(current->object);
    free(current);
    current = next;
  }

  free(gc_heap.roots.items);
  gc_heap.roots.items = NULL;
  gc_heap.roots.count = 0;
  gc_heap.roots.capacity = 0;

  gc_log(GC_LOG_INFO, "Garbage collector destroyed");
}

void gc_create_new_object(Object **obj, int id, size_t num_references)
{
  size_t total = sizeof(Object) + sizeof(Object *) * num_references;
  Object *o = malloc(total);

  if (o == NULL)
  {
    gc_log(GC_LOG_ERROR, "Memory allocation failed while creating object #%d", id);
    exit(EXIT_FAILURE);
  }

  memset(((char *)o) + sizeof(Object), 0, sizeof(Object *) * num_references);

  o->id = id;
  o->marked = false;
  o->number_of_references = num_references;

  HeapNode *node = malloc(sizeof(HeapNode));
  if (node == NULL)
  {
    gc_log(GC_LOG_ERROR, "Memory allocation failed for heap node");
    free(o);
    exit(EXIT_FAILURE);
  }

  node->object = o;
  node->next = gc_heap.head;
  gc_heap.head = node;

  *obj = o;

  gc_log(GC_LOG_DEBUG, "Allocated Object #%d (refs=%zu, address=%p)", id, num_references, (void *)o);
}

void gc_add_root(Object *obj)
{
  if (obj == NULL)
  {
    gc_log(GC_LOG_WARN, "Attempted to add NULL root reference");
    return;
  }

  for (size_t i = 0; i < gc_heap.roots.count; i++)
    if (gc_heap.roots.items[i] == obj)
      return;

  if (gc_heap.roots.count == gc_heap.roots.capacity)
  {
    size_t new_capacity = gc_heap.roots.capacity == 0 ? 4 : gc_heap.roots.capacity * 2;

    Object **new_items = realloc(gc_heap.roots.items, new_capacity * sizeof(Object *));

    if (new_items == NULL)
    {
      gc_log(GC_LOG_ERROR, "Failed to expand root set");
      exit(EXIT_FAILURE);
    }

    gc_heap.roots.items = new_items;
    gc_heap.roots.capacity = new_capacity;
  }

  gc_heap.roots.items[gc_heap.roots.count++] = obj;
  gc_log(GC_LOG_DEBUG, "Added root Object #%d", obj->id);
}

void gc_remove_root(Object *obj)
{
  for (size_t i = 0; i < gc_heap.roots.count; i++)
  {
    if (gc_heap.roots.items[i] == obj)
    {
      gc_heap.roots.items[i] = gc_heap.roots.items[gc_heap.roots.count - 1];
      gc_heap.roots.count--;
      gc_log(GC_LOG_DEBUG, "Removed root Object #%d", obj->id);
      return;
    }
  }
}

void gc_mark(Object *obj)
{
  if (obj == NULL || obj->marked)
    return;

  obj->marked = true;

  gc_log(GC_LOG_DEBUG, COLOR_SUCCESS "Marked Object #%d" COLOR_RESET, obj->id);

  for (size_t i = 0; i < obj->number_of_references; i++)
    if (obj->references[i] != NULL)
      gc_mark(obj->references[i]);
}

void gc_mark_root_set()
{
  gc_log(GC_LOG_INFO, "Starting mark phase from root set (%zu roots)", gc_heap.roots.count);

  for (size_t i = 0; i < gc_heap.roots.count; i++)
    gc_mark(gc_heap.roots.items[i]);
}

void gc_sweep()
{
  gc_log(GC_LOG_INFO, "Starting sweep phase");

  HeapNode **current = &gc_heap.head;

  while (*current != NULL)
  {
    Object *obj = (*current)->object;

    if (!obj->marked)
    {
      gc_log(GC_LOG_WARN, "Collecting unreachable Object #%d", obj->id);

      HeapNode *unreached = *current;

      *current = unreached->next;

      for (size_t i = 0; i < unreached->object->number_of_references; i++)
        unreached->object->references[i] = NULL;

      free(unreached->object);
      free(unreached);
    }
    else
    {
      obj->marked = false;
      current = &(*current)->next;
    }
  }
}

void gc_print_heap_state(const char *title)
{
  printf("\n" COLOR_TITLE "[GC STATE] %s\n" COLOR_RESET, title);
  printf(COLOR_HEADER "-------------------------------------------------------------\n" COLOR_RESET);
  printf(COLOR_HEADER "  %-10s %-12s %-10s %-10s\n" COLOR_RESET, "Object ID", "Marked", "Refs", "Address");
  printf(COLOR_HEADER "-------------------------------------------------------------\n" COLOR_RESET);

  HeapNode *current = gc_heap.head;

  if (current == NULL)
  {
    printf(COLOR_OBJECT "  (heap is empty)\n" COLOR_RESET);
  }

  while (current != NULL)
  {
    Object *obj = current->object;
    const char *mark_color = obj->marked ? COLOR_SUCCESS : COLOR_ERROR;

    printf("  %-10d %s%-12s" COLOR_OBJECT " %-10zu %-10p\n" COLOR_RESET,
           obj->id,
           mark_color,
           obj->marked ? "true" : "false",
           obj->number_of_references,
           (void *)obj);

    current = current->next;
  }

  printf(COLOR_HEADER "-------------------------------------------------------------\n" COLOR_RESET);
}

int main(void)
{
  gc_init();

  Object *a, *b, *c, *d;

  gc_create_new_object(&a, 1, 2);
  gc_create_new_object(&b, 2, 1);
  gc_create_new_object(&c, 3, 0);
  gc_create_new_object(&d, 4, 0);

  a->references[0] = b;
  a->references[1] = c;
  b->references[0] = d;

  gc_add_root(a);

  gc_print_heap_state("Before first collection");
  gc_mark_root_set();
  gc_sweep();
  gc_print_heap_state("After first collection");

  gc_remove_root(a);
  gc_mark_root_set();
  gc_sweep();
  gc_print_heap_state("After removing root and collecting again");

  gc_destroy();
  return 0;
}
