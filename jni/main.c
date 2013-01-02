#include <stdio.h>
#include "TPCircularBuffer.h"

int main(int argc, char **argv) {
    
    TPCircularBuffer cb;
    int i = 0;
    char data[100] = "0abcdefghijklmnopqrstuvwxyz";
    TPCircularBufferInit(&cb,1024);

    int available_bytes;
    char *head;
    while(head = (char*)TPCircularBufferHead(&cb,&available_bytes)){
      i++;
      data[0] = i;
      if(available_bytes > 100){
        memcpy(head, data, 100);
        TPCircularBufferProduce(&cb, 100);
        printf("wrote %d round\n",i);
      }else{
        break;
      }
    }
    char *tail;

    while(tail = (char*)TPCircularBufferTail(&cb,&available_bytes)){
      printf("%d:%d\n",tail[0],tail[1]);
      TPCircularBufferConsume(&cb,100);      
    }

    TPCircularBufferCleanup(&cb);

    return 0;
}