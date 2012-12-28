/* Circular buffer*/
 
#include <stdio.h>
#include <stdlib.h>
 
/* Opaque buffer element type.  This would be defined by the application. */
typedef struct { char m[1024]; } ElemType;
 
/* Circular buffer object */
typedef struct {
    int         size;   /* maximum number of elements           */
    int         start;  /* index of oldest element              */
    int         end;    /* index at which to write new element  */
    int         s_msb;
    int         e_msb;
    ElemType   *elems;  /* vector of elements                   */
} CircularBuffer;
 
void cbInit(CircularBuffer *cb, int size) {
    cb->size  = size;
    cb->start = 0;
    cb->end   = 0;
    cb->s_msb = 0;
    cb->e_msb = 0;
    cb->elems = (ElemType *)calloc(cb->size, sizeof(ElemType));
    printf("created with %u elements of size %lu\n",cb->size, sizeof(ElemType));
}
void cbFree(CircularBuffer *cb) {
    free(cb->elems); /* OK if null */ }
 

void cbPrint(CircularBuffer *cb) {
    printf("size=0x%x, start=%d, end=%d\n", cb->size, cb->start, cb->end);
}
 
int cbIsFull(CircularBuffer *cb) {
    return cb->end == cb->start && cb->e_msb != cb->s_msb; }
 
int cbIsEmpty(CircularBuffer *cb) {
    return cb->end == cb->start && cb->e_msb == cb->s_msb; }
 
void cbIncr(CircularBuffer *cb, int *p, int *msb) {
    *p = *p + 1;
    if (*p == cb->size) {
        *msb ^= 1;
        *p = 0;
    }
}
 
void cbWrite(CircularBuffer *cb, ElemType *elem) {
    cb->elems[cb->end] = *elem;
    if (cbIsFull(cb)) /* full, overwrite moves start pointer */
        cbIncr(cb, &cb->start, &cb->s_msb);
    cbIncr(cb, &cb->end, &cb->e_msb);
}
 
void cbRead(CircularBuffer *cb, ElemType *elem) {
    *elem = cb->elems[cb->start];
    cbIncr(cb, &cb->start, &cb->s_msb);
}

 
int main(int argc, char **argv) {
    CircularBuffer cb;
    ElemType elem;
    int i;
 
    int testBufferSize = 10; /* arbitrary size */
    cbInit(&cb, testBufferSize);
 
    /* Fill buffer with test elements 3 times */
    for (i =  0; i < 3 * testBufferSize; i++){
        elem.m[1023] = i*2;
        cbWrite(&cb, &elem);
        cbPrint(&cb);
    }
 
    cbPrint(&cb);
    /* Remove and print all elements */
    while (!cbIsEmpty(&cb)) {
        cbRead(&cb, &elem);
        printf("%d\n", elem.m[1023]);
    }
 
    cbFree(&cb);
    return 0;
}